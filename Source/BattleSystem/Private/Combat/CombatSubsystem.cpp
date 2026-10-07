// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatSubsystem.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Combat/CombatBatch.h"
#include "Combat/CombatCommandScript.h"
#include "Combat/CombatCueTable.h"
#include "Combat/CombatGrid.h"
#include "Combat/CombatNavigation.h"
#include "Combat/CombatPieces.h"
#include "Combat/CombatProjectileActor.h"
#include "Combat/CombatSettings.h"
#include "Combat/CombatTags.h"
#include "Combat/CombatUnitActor.h"
#include "Combat/CombatUnitData.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "NiagaraFunctionLibrary.h"

DEFINE_LOG_CATEGORY(LogCombat);

static TAutoConsoleVariable<int32> CVarCombatDebug(
	TEXT("Combat.Debug"),
	0,
	TEXT("Combat debug drawing. 0 = off, 1 = lines to target and steer point (yellow), 2 = also team 0's distance map in cells. ")
	TEXT("Target line color = reason: team color = nearest, cyan = visible (ranged), orange = threat, magenta = taunt."));

static TAutoConsoleVariable<int32> CVarCombatShowRanges(
	TEXT("Combat.ShowRanges"),
	0,
	TEXT("1 = draw the range of area attacks (taunt) around the units that have one."));

void UCombatSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UpdateWalls();

	if (!Simulation)
	{
		return;
	}

	const double FixedDt = Simulation->GetFixedDt();
	if (!Simulation->IsFinished())
	{
		// Engine DeltaTime (with pause and speed) only decides how many fixed steps run; it never enters the simulation.
		if (!bPaused)
		{
			Accumulator += DeltaTime * TimeScale;
		}
		int32 Steps = 0;
		while (Accumulator >= FixedDt && Steps < MaxStepsPerFrame && !Simulation->IsFinished())
		{
			Simulation->Step();
			DispatchEvents();
			UpdateCheckpoints();
			Accumulator -= FixedDt;
			++Steps;
		}

		// Drop any backlog beyond the per-frame maximum instead of catching up later.
		Accumulator = FMath::Min(Accumulator, FixedDt);

		if (Simulation->IsFinished())
		{
			ReportResult();
			Accumulator = FixedDt;
		}
	}

	// A selected unit that died is deselected.
	if (SelectedUnitId != INDEX_NONE && !Simulation->GetUnits()[SelectedUnitId].bAlive)
	{
		SelectUnit(INDEX_NONE);
	}

	const float Alpha = FMath::Clamp(static_cast<float>(Accumulator / FixedDt), 0.f, 1.f);
	UpdateActors(Alpha);
	DrawDebug(Alpha);
	DrawAreaRanges(Alpha);
	DrawPendingAreas();
}

TStatId UCombatSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCombatSubsystem, STATGROUP_Tickables);
}

void UCombatSubsystem::Deinitialize()
{
	DestroyDesignPreviews();
	StopFight();
	Super::Deinitialize();
}

bool UCombatSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

FString FCombatFightSource::GetName() const
{
	if (Level.IsSet())
	{
		return TEXT("Level: ") + Level->Name;
	}
	return FString();
}

bool UCombatSubsystem::StartFightFromSource(int32 Seed, const FCombatFightSource& Source, const FCombatSimSettings& SimSettings,
	TConstArrayView<FCombatCommand> Commands)
{
	StopFight();
	bIsReplay = false;
	ReplayVerdict.Reset();
	Checkpoints.Reset();
	FirstDifferentTick = INDEX_NONE;
	++FightSerial;
	CheckpointInterval = FMath::Max(GetDefault<UCombatSettings>()->ReplayCheckpointInterval, 1);

	if (!Source.IsValid())
	{
		UE_LOG(LogCombat, Error, TEXT("StartFight: no level."));
		return false;
	}
	bDesignMode = false;
	DestroyDesignPreviews();

	FCombatSimConfig Config;
	TArray<TSharedPtr<const FCombatUnitType>> Types;
	TArray<int32> Rotations;
	FCombatUnitCatalog Units;
	if (!BuildSimConfigFromSource(GetWorld(), Seed, Source, SimSettings, Config, GridOrigin, &Types, &Rotations, &Units))
	{
		return false;
	}
	ShowSourceInArena(Source);

	Config.Commands = Commands;
	Simulation = MakeUnique<FCombatSimulation>(Config);
	CueTable = GetDefault<UCombatSettings>()->CueTable.LoadSynchronous();
	MaxStepsPerFrame = FMath::Max(GetDefault<UCombatSettings>()->MaxStepsPerFrame, 1);
	Accumulator = 0.0;
	bPaused = false;
	CurrentSeed = Seed;
	CurrentSourceName = Source.GetName();
	CurrentLevel = Source.Level;
	CurrentUnits = MoveTemp(Units);
	CurrentSettings = SimSettings;

	SourceTypes = MoveTemp(Types);
	SourceRotations = MoveTemp(Rotations);
	for (const FCombatUnit& Unit : Simulation->GetUnits())
	{
		SpawnUnitActor(Unit);
	}

	UE_LOG(LogCombat, Display, TEXT("Fight started: seed %d, %s, %d units."), Seed, *Source.GetName(), UnitActors.Num());
	return true;
}

bool UCombatSubsystem::SaveReplay(FString& OutMessage) const
{
	if (!Simulation || !Simulation->IsFinished())
	{
		OutMessage = TEXT("No finished fight to save.");
		return false;
	}

	FCombatReplay Replay;
	Replay.SavedAt = FDateTime::Now().ToString();
	Replay.BuildVersion = FApp::GetBuildVersion();
	Replay.Level = CurrentLevel.GetValue();
	Replay.Units = CurrentUnits;
	Replay.Seed = CurrentSeed;
	Replay.Settings = CurrentSettings;
	Replay.Ticks = Simulation->GetTick();
	Replay.Outcome = FCombatSimulation::OutcomeToString(Simulation->GetOutcome());
	Replay.WinningTeam = Simulation->GetWinningTeam();
	Replay.FinalChecksum = CombatReplay::ChecksumToString(Simulation->GetChecksum());
	Replay.Commands = Simulation->GetCommandLog();
	Replay.CommandDelayTicks = GetDefault<UCombatSettings>()->CommandDelayTicks;
	Replay.CheckpointInterval = CheckpointInterval;
	Replay.Checkpoints = Checkpoints;

	const FString FileName = FString::Printf(TEXT("%s_%s_%d.json"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")), *CurrentSourceName, CurrentSeed);
	const FString Path = CombatReplay::GetReplayDirectory() / FileName;
	if (!CombatReplay::SaveToFile(Replay, Path))
	{
		OutMessage = FString::Printf(TEXT("Could not write %s."), *Path);
		return false;
	}

	OutMessage = FString::Printf(TEXT("Saved replay %s"), *FileName);
	UE_LOG(LogCombat, Display, TEXT("%s"), *OutMessage);
	return true;
}

bool UCombatSubsystem::PlayReplay(const FString& FileOrPath, FString& OutMessage)
{
	FCombatReplay Replay;
	const FString Path = CombatReplay::ResolvePath(FileOrPath);
	if (!CombatReplay::LoadFromFile(Path, Replay))
	{
		OutMessage = FString::Printf(TEXT("Could not read replay %s."), *Path);
		UE_LOG(LogCombat, Error, TEXT("%s"), *OutMessage);
		return false;
	}

	// Before version 4 replays could come from a setup asset instead of a level, before 5 they named unit definition
	// assets, before 6 their rows had tags for skill types and look slots, before 7 they had no recovery or stagger;
	// those are gone.
	if (Replay.FormatVersion < FCombatReplay().FormatVersion)
	{
		OutMessage = FString::Printf(TEXT("Replay %s is from an older format (version %d) and cannot be played."), *Path, Replay.FormatVersion);
		UE_LOG(LogCombat, Error, TEXT("%s"), *OutMessage);
		return false;
	}

	FCombatFightSource Source;
	Source.Level = Replay.Level;
	Source.Level->Normalize();
	Source.Units = Replay.Units;
	if (!StartFightFromSource(Replay.Seed, Source, Replay.Settings, Replay.Commands))
	{
		OutMessage = FString::Printf(TEXT("Could not start the replay (level %s)."), *Replay.Level.Name);
		UE_LOG(LogCombat, Error, TEXT("%s"), *OutMessage);
		return false;
	}

	bIsReplay = true;
	PlayingReplay = Replay;
	if (Replay.CheckpointInterval > 0)
	{
		CheckpointInterval = Replay.CheckpointInterval;
	}

	TArray<FString> Warnings;
	if (Replay.BuildVersion != FApp::GetBuildVersion())
	{
		Warnings.Add(FString::Printf(TEXT("other build (%s)"), *Replay.BuildVersion));
	}

	OutMessage = FString::Printf(TEXT("Playing replay %s, seed %d, %d commands"), *Source.GetName(), Replay.Seed, Replay.Commands.Num());
	if (!Warnings.IsEmpty())
	{
		OutMessage += TEXT(" - warning: ") + FString::Join(Warnings, TEXT(", "));
	}
	UE_LOG(LogCombat, Display, TEXT("%s"), *OutMessage);
	return true;
}

bool UCombatSubsystem::RunBatch(const FCombatFightSource& Source, int32 Count, int32 StartSeed, bool bWriteCsv, FString& OutSummary)
{
	if (!Source.IsValid())
	{
		OutSummary = TEXT("No level for the batch.");
		return false;
	}
	const bool bOk = RunBatchInWorld(GetWorld(), Source, Count, StartSeed, GetCurrentSimSettings(), bWriteCsv, OutSummary, {});
	LastBatchSummary = OutSummary;
	return bOk;
}

bool UCombatSubsystem::RunBatchInWorld(UWorld* World, const FCombatFightSource& Source, int32 Count, int32 StartSeed,
	const FCombatSimSettings& Settings, bool bWriteCsv, FString& OutSummary, TConstArrayView<FCombatCommand> Commands)
{
	FCombatSimConfig Config;
	FVector GridOrigin;
	TArray<TSharedPtr<const FCombatUnitType>> Types;
	if (!BuildSimConfigFromSource(World, StartSeed, Source, Settings, Config, GridOrigin, &Types))
	{
		OutSummary = FString::Printf(TEXT("Could not build %s."), *Source.GetName());
		return false;
	}

	// The same commands on every seed: what does this player input do on average?
	Config.Commands = Commands;

	TArray<FString> UnitTypeNames;
	for (const TSharedPtr<const FCombatUnitType>& Type : Types)
	{
		UnitTypeNames.Add(Type->Name.ToString());
	}

	const FCombatBatchResult Result = CombatBatch::Run(Config, UnitTypeNames, FMath::Max(Count, 1), StartSeed);
	const FString SourceName = Source.GetName();
	OutSummary = Result.ToSummary(SourceName, Settings.TickRate);
	if (!Commands.IsEmpty())
	{
		OutSummary += FString::Printf(TEXT("  With %d scripted commands on every fight.\n"), Commands.Num());
	}

	if (bWriteCsv)
	{
		const FString BasePath = FPaths::ProjectSavedDir() / TEXT("CombatBatch")
			/ FString::Printf(TEXT("%s_%s"), *SourceName.Replace(TEXT("Level: "), TEXT("Level_")), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
		OutSummary += Result.WriteCsv(BasePath, Settings.TickRate)
			? FString::Printf(TEXT("  CSV: %s_fights.csv / _units.csv\n"), *FPaths::GetCleanFilename(BasePath))
			: TEXT("  CSV could not be written.\n");
	}

	UE_LOG(LogCombat, Display, TEXT("%s"), *OutSummary);
	return true;
}

void UCombatSubsystem::StopFight()
{
	SelectedUnitId = INDEX_NONE;
	bAwaitingMoveTarget = false;
	++FightSerial;

	for (ACombatUnitActor* Actor : UnitActors)
	{
		if (IsValid(Actor))
		{
			Actor->Destroy();
		}
	}
	UnitActors.Reset();
	UnitTypes.Reset();
	SourceTypes.Reset();
	SourceRotations.Reset();

	for (const TPair<int32, TObjectPtr<ACombatProjectileActor>>& Pair : ProjectileActors)
	{
		if (IsValid(Pair.Value))
		{
			Pair.Value->Destroy();
		}
	}
	ProjectileActors.Reset();

	Simulation.Reset();
}

void UCombatSubsystem::DispatchEvents()
{
	const TArray<FCombatUnit>& Units = Simulation->GetUnits();
	for (const FCombatEvent& Event : Simulation->GetEvents())
	{
		switch (Event.Type)
		{
		case ECombatEventType::Attack:
			if (ACombatUnitActor* Actor = UnitActors[Event.SourceId])
			{
				// Area attacks have no target: no lunge direction.
				const FCombatAttackStats& Attack = Units[Event.SourceId].Stats.GetAttack(Event.AttackIndex);
				Actor->OnAttack(Event.TargetId != INDEX_NONE ? SimToWorld(Units[Event.TargetId].Position) : Actor->GetActorLocation(),
					GetAttackAnimationTag(Event.SourceId, Event.AttackIndex), static_cast<float>(Attack.WindupTicks) / FMath::Max(CurrentSettings.TickRate, 1));
			}
			break;
		case ECombatEventType::Hit:
			if (ACombatUnitActor* Actor = UnitActors[Event.TargetId])
			{
				Actor->OnHit(Event.Amount);
				// Area attacks play their cue once, where the area goes off.
				if (!Units[Event.SourceId].Stats.GetAttack(Event.AttackIndex).IsArea())
				{
					PlayCue(Event.Cue, Actor->GetActorLocation());
				}
			}
			break;
		case ECombatEventType::Death:
			if (ACombatUnitActor* Actor = UnitActors[Event.TargetId])
			{
				Actor->OnDeath();
			}
			break;
		case ECombatEventType::AreaAttackFired:
			OnAreaFired(Event);
			break;
		case ECombatEventType::ProjectileSpawned:
			SpawnProjectileActor(Event);
			break;
		case ECombatEventType::UnitSpawned:
			SpawnUnitActor(Units[Event.SourceId]);
			break;
		case ECombatEventType::WaveStarted:
		{
			const FString Message = FString::Printf(TEXT("Wave %d/%d"), Event.WaveIndex + 1, Simulation->GetWaveCount());
			UE_LOG(LogCombat, Display, TEXT("%s started at tick %d."), *Message, Simulation->GetTick());
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(INDEX_NONE, 3.f, FColor::Orange, Message);
			}
			break;
		}
		case ECombatEventType::ProjectileEnded:
		{
			TObjectPtr<ACombatProjectileActor> Actor;
			if (ProjectileActors.RemoveAndCopyValue(Event.ProjectileId, Actor) && IsValid(Actor))
			{
				Actor->Destroy();
			}
			break;
		}
		}
	}
}

namespace CombatSubsystemPrivate
{
	/** Seed for a unit's look: the same fight seed and unit ID give the same figure (fights, replays, previews). */
	static int32 GetLookSeed(int32 FightSeed, int32 UnitId)
	{
		return static_cast<int32>(HashCombine(GetTypeHash(FightSeed), GetTypeHash(UnitId)));
	}
}

namespace CombatSubsystemPrivate
{
	/** Index of the unit or spawn in Cell whose position is nearest to Local (grid-local cm), or INDEX_NONE. */
	template <typename TEntry>
	int32 FindNearestInCell(const TArray<TEntry>& Entries, const FIntPoint& Cell, const FVector2D& Local, float CellSize)
	{
		int32 Found = INDEX_NONE;
		double Best = TNumericLimits<double>::Max();
		for (int32 Index = 0; Index < Entries.Num(); ++Index)
		{
			if (Entries[Index].Cell == Cell)
			{
				const FVector2D Spot = CellSize * FVector2D(Cell.X + 0.5, Cell.Y + 0.5) + CombatLevels::GetUnitPositionOffset(Entries[Index].Position, CellSize);
				const double Distance = FVector2D::DistSquared(Spot, Local);
				if (Distance < Best)
				{
					Best = Distance;
					Found = Index;
				}
			}
		}
		return Found;
	}
}

void UCombatSubsystem::SpawnUnitActor(const FCombatUnit& Unit)
{
	// Unit IDs grow by one with every spawn, so the actor arrays stay indexed by unit ID.
	check(UnitActors.Num() == Unit.Id);
	const TSharedPtr<const FCombatUnitType> Type = SourceTypes.IsValidIndex(Unit.SourceIndex) ? SourceTypes[Unit.SourceIndex] : nullptr;
	UClass* ActorClass = Type ? Type->Unit.LoadActorClass() : ACombatUnitActor::StaticClass();

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// Units of a level start in their rotation (presentation only); they turn as soon as they move or have a target.
	const FRotator StartRotation(0.0, SourceRotations.IsValidIndex(Unit.SourceIndex) ? CombatLevels::GetUnitYaw(SourceRotations[Unit.SourceIndex]) : 0.0, 0.0);
	ACombatUnitActor* Actor = GetWorld()->SpawnActor<ACombatUnitActor>(ActorClass, SimToWorld(Unit.Position), StartRotation, SpawnParams);
	if (Actor)
	{
		const bool bRanged = Unit.Stats.Attacks.ContainsByPredicate([](const FCombatAttackStats& Attack) { return Attack.IsRanged(); });
		Actor->InitUnit(Unit.Id, Unit.Team, Unit.Stats.Radius, GetDefault<UCombatSettings>()->GetTeamColor(Unit.Team), bRanged);
		if (Type)
		{
			// Same seed and unit ID, same look: a replay shows the same figures.
			Actor->InitLook(Type->Unit.Look, CombatSubsystemPrivate::GetLookSeed(CurrentSeed, Unit.Id));
			Actor->SetLocomotionTuning(Type->Unit.MoveSpeed, Type->Unit.LocomotionRate);
		}
	}
	UnitActors.Add(Actor);
	UnitTypes.Add(Type);
}

void UCombatSubsystem::SpawnProjectileActor(const FCombatEvent& Event)
{
	// The projectile actor class comes from the skill the simulation attack was made from.
	UClass* ActorClass = ACombatProjectileActor::StaticClass();
	const FCombatUnit& Source = Simulation->GetUnits()[Event.SourceId];
	const int32 SourceIndex = Source.Stats.GetAttack(Event.AttackIndex).SourceIndex;
	const FCombatUnitType* Type = GetUnitType(Event.SourceId);
	if (Type && Type->Attacks.IsValidIndex(SourceIndex))
	{
		ActorClass = Type->Attacks[SourceIndex].LoadProjectileActorClass();
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACombatProjectileActor* Actor = GetWorld()->SpawnActor<ACombatProjectileActor>(ActorClass, SimToWorld(Source.Position), FRotator::ZeroRotator, SpawnParams);
	if (Actor)
	{
		Actor->InitProjectile(Event.ProjectileId, GetDefault<UCombatSettings>()->GetTeamColor(Source.Team));
		ProjectileActors.Add(Event.ProjectileId, Actor);
	}
}

void UCombatSubsystem::UpdateActors(float Alpha)
{
	const TArray<FCombatUnit>& Units = Simulation->GetUnits();
	for (const FCombatUnit& Unit : Units)
	{
		ACombatUnitActor* Actor = UnitActors[Unit.Id];
		if (!Actor || !Unit.bAlive)
		{
			continue;
		}

		const FVector2D Position = FMath::Lerp(Unit.PreviousPosition, Unit.Position, Alpha);

		// The figure faces what it means to do, not every push: its target while it stands or heads straight for it,
		// else the point it steers to (its route or move order), else it keeps its facing. A push by separation then
		// shows as a step sideways or backwards (LocalVelocity in the AnimBP).
		FVector2D Facing = FVector2D::ZeroVector;
		const bool bSteering = !Unit.SteerPoint.Equals(Unit.PreviousPosition, 1.0);
		const bool bHasTarget = Unit.TargetId != INDEX_NONE && Units[Unit.TargetId].bAlive;
		if (bHasTarget && (!bSteering || Unit.SteerPoint.Equals(Units[Unit.TargetId].PreviousPosition)))
		{
			const FCombatUnit& Target = Units[Unit.TargetId];
			Facing = FMath::Lerp(Target.PreviousPosition, Target.Position, Alpha) - Position;
		}
		else if (bSteering)
		{
			Facing = Unit.SteerPoint - Position;
		}

		Actor->SetHealth(Unit.Stats.MaxHP > 0.f ? Unit.HP / Unit.Stats.MaxHP : 0.f);

		Actor->SetStatusEffects(GetStatusDisplays(Unit));
		Actor->SetAnimationState(FVector(Unit.Velocity, 0.0), bPaused ? 0.f : TimeScale);
		if (Actor->HasLookOverrides())
		{
			FGameplayTagContainer Tags = Unit.Stats.Tags;
			for (const FCombatActiveEffect& Active : Unit.Effects.GetEffects())
			{
				Tags.AddTag(Active.Effect.EffectTag);
				Tags.AppendTags(Active.Effect.GrantedTags);
			}
			Actor->SetActiveTags(Tags);
		}
		Actor->SetSelected(Unit.Id == SelectedUnitId);
		Actor->UpdatePresentation(SimToWorld(Position), FVector(Facing.X, Facing.Y, 0.0));
		// After the actor moved, so the line starts at its new position.
		Actor->SetMoveTarget(Unit.bHasMoveOrder, SimToWorld(Simulation->GetGrid().CellToLocal(Unit.MoveTargetCell)));
	}

	for (const FCombatProjectile& Projectile : Simulation->GetProjectiles())
	{
		if (const TObjectPtr<ACombatProjectileActor>* Actor = ProjectileActors.Find(Projectile.Id))
		{
			const FVector2D Position = FMath::Lerp(Projectile.PreviousPosition, Projectile.Position, Alpha);
			const FVector2D Direction = Projectile.Position - Projectile.PreviousPosition;
			(*Actor)->UpdatePresentation(SimToWorld(Position), FVector(Direction.X, Direction.Y, 0.0));
		}
	}
}

void UCombatSubsystem::DrawDebug(float Alpha) const
{
	const int32 Level = CVarCombatDebug.GetValueOnGameThread();
	if (Level <= 0)
	{
		return;
	}

	UWorld* World = GetWorld();
	const UCombatSettings* Settings = GetDefault<UCombatSettings>();
	const FVector Lift(0.0, 0.0, 30.0);
	const TArray<FCombatUnit>& Units = Simulation->GetUnits();

	for (const FCombatUnit& Unit : Units)
	{
		if (!Unit.bAlive)
		{
			continue;
		}

		const FVector Position = SimToWorld(FMath::Lerp(Unit.PreviousPosition, Unit.Position, Alpha)) + Lift;
		if (Unit.TargetId != INDEX_NONE && Units[Unit.TargetId].bAlive)
		{
			const FCombatUnit& Target = Units[Unit.TargetId];
			const FVector TargetPosition = SimToWorld(FMath::Lerp(Target.PreviousPosition, Target.Position, Alpha)) + Lift;
			FColor ReasonColor = Settings->GetTeamColor(Unit.Team).ToFColor(true);
			switch (Unit.TargetReason)
			{
			case ECombatTargetReason::Visible: ReasonColor = FColor::Cyan; break;
			case ECombatTargetReason::Threat: ReasonColor = FColor::Orange; break;
			case ECombatTargetReason::Taunt: ReasonColor = FColor::Magenta; break;
			default: break;
			}
			DrawDebugLine(World, Position, TargetPosition, ReasonColor, false, -1.f, 0, 3.f);

			if (!Unit.SteerPoint.Equals(Target.PreviousPosition))
			{
				DrawDebugLine(World, Position, SimToWorld(Unit.SteerPoint) + Lift, FColor::Yellow, false, -1.f, 0, 2.f);
			}
		}
	}

	// Team 0's distance map on the nav grid of the smallest class (sub-cells).
	const FCombatDistanceMap* Map = Level >= 2 ? Simulation->GetDistanceMap(0, 0) : nullptr;
	if (Map)
	{
		const FCombatGridData& Grid = Simulation->GetNavGrid(0);
		for (int32 Y = 0; Y < Grid.Height; ++Y)
		{
			for (int32 X = 0; X < Grid.Width; ++X)
			{
				const int32 Distance = Map->GetDistance(FIntPoint(X, Y));
				if (Distance != FCombatDistanceMap::Unreachable)
				{
					DrawDebugString(World, SimToWorld(Grid.CellToLocal(FIntPoint(X, Y))) + FVector(0.0, 0.0, 10.0),
						FString::Printf(TEXT("%.1f"), Distance / static_cast<float>(FCombatDistanceMap::StraightCost)),
						nullptr, FColor::White, 0.f, false, 1.f);
				}
			}
		}
	}
}

void UCombatSubsystem::DrawAreaRanges(float Alpha) const
{
	if (CVarCombatShowRanges.GetValueOnGameThread() <= 0)
	{
		return;
	}

	UWorld* World = GetWorld();
	const FColor RangeColor(255, 0, 255, 120);
	for (const FCombatUnit& Unit : Simulation->GetUnits())
	{
		if (!Unit.bAlive)
		{
			continue;
		}

		const FVector Center = SimToWorld(FMath::Lerp(Unit.PreviousPosition, Unit.Position, Alpha)) + FVector(0.0, 0.0, 10.0);
		for (const FCombatAttackStats& Attack : Unit.Stats.Attacks)
		{
			if (Attack.AreaShape == ECombatAreaShape::CircleAroundSelf)
			{
				// Reach from the center: a unit is hit when its edge is inside this circle.
				DrawDebugCircle(World, Center, Attack.AreaRadius, 64, RangeColor, false, -1.f, 0, 2.f,
					FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), false);
			}
		}
	}
}

void UCombatSubsystem::DrawPendingAreas() const
{
	const FColor Color = GetDefault<UCombatSettings>()->TelegraphColor.ToFColor(true);
	for (const FCombatPendingArea& Pending : Simulation->GetPendingAreas())
	{
		DrawArea(Pending.Area, Color, -1.f, 3.f);
		const float Progress = 1.f - static_cast<float>(Pending.RemainingTicks) / FMath::Max(Pending.TotalTicks, 1);
		DrawArea(Pending.Area, Color, -1.f, 1.5f, Progress);
	}
}

void UCombatSubsystem::OnAreaFired(const FCombatEvent& Event)
{
	const UCombatSettings* Settings = GetDefault<UCombatSettings>();
	const FCombatAttackStats& Attack = Simulation->GetUnits()[Event.SourceId].Stats.GetAttack(Event.AttackIndex);

	FLinearColor Color = Settings->DefaultAreaColor;
	if (const FCombatCue* Cue = CueTable ? CueTable->FindCue(Event.Cue) : nullptr)
	{
		Color = Cue->DebugColor;
	}
	else if (Attack.Type == ECombatSkillType::Taunt)
	{
		Color = Settings->TauntColor;
	}

	DrawArea(Event.Area, Color.ToFColor(true), Settings->AreaPulseDuration, 6.f);
	PlayCue(Event.Cue, SimToWorld(Event.Area.Center));

	if (ACombatUnitActor* Actor = UnitActors[Event.SourceId])
	{
		Actor->OnAreaAttack(Event.Area.Radius);
	}
}

void UCombatSubsystem::PlayCue(const FGameplayTag& Cue, const FVector& Location) const
{
	const FCombatCue* Entry = CueTable ? CueTable->FindCue(Cue) : nullptr;
	if (!Entry)
	{
		return;
	}
	if (Entry->Niagara)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), Entry->Niagara, Location);
	}
	if (Entry->Sound)
	{
		UGameplayStatics::PlaySoundAtLocation(GetWorld(), Entry->Sound, Location);
	}
}

void UCombatSubsystem::DrawArea(const FCombatArea& Area, const FColor& Color, float Duration, float Thickness, float Scale) const
{
	UWorld* World = GetWorld();
	const FVector Center = SimToWorld(Area.Center) + FVector(0.0, 0.0, 15.0);
	const FVector XAxis(1.0, 0.0, 0.0);
	const FVector YAxis(0.0, 1.0, 0.0);

	// Every shape reaches from its center.
	const float Reach = Area.Radius * Scale;
	if (Reach <= 1.f)
	{
		return;
	}

	if (Area.Shape != ECombatAreaShape::Cone)
	{
		DrawDebugCircle(World, Center, Reach, 64, Color, false, Duration, 0, Thickness, XAxis, YAxis, false);
		return;
	}

	const float HalfAngle = FMath::Acos(FMath::Clamp(Area.ConeCosHalfAngle, -1.f, 1.f));
	const float Facing = FMath::Atan2(Area.Direction.Y, Area.Direction.X);
	constexpr int32 Segments = 24;
	FVector Previous = Center + FVector(FMath::Cos(Facing - HalfAngle), FMath::Sin(Facing - HalfAngle), 0.0) * Reach;
	DrawDebugLine(World, Center, Previous, Color, false, Duration, 0, Thickness);
	for (int32 Segment = 1; Segment <= Segments; ++Segment)
	{
		const float Angle = Facing - HalfAngle + 2.f * HalfAngle * Segment / Segments;
		const FVector Point = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0) * Reach;
		DrawDebugLine(World, Previous, Point, Color, false, Duration, 0, Thickness);
		Previous = Point;
	}
	DrawDebugLine(World, Center, Previous, Color, false, Duration, 0, Thickness);
}

void UCombatSubsystem::ReportResult()
{
	FString Result = FCombatSimulation::OutcomeToString(Simulation->GetOutcome());
	if (Simulation->GetOutcome() == ECombatOutcome::TeamWon)
	{
		Result += FString::Printf(TEXT(" (team %d)"), Simulation->GetWinningTeam());
	}

	const FString Checksum = CombatReplay::ChecksumToString(Simulation->GetChecksum());
	FString Message = FString::Printf(TEXT("Fight over: %s after %d ticks, checksum %s"), *Result, Simulation->GetTick(), *Checksum);

	if (bIsReplay)
	{
		const bool bIdentical = Checksum == PlayingReplay.FinalChecksum && Simulation->GetTick() == PlayingReplay.Ticks
			&& FirstDifferentTick == INDEX_NONE;
		ReplayVerdict = bIdentical
			? FString::Printf(TEXT("Replay identical (checksum %s)"), *Checksum)
			: FString::Printf(TEXT("Replay DIFFERENT: recorded %s after %d ticks, now %s after %d ticks"),
				*PlayingReplay.FinalChecksum, PlayingReplay.Ticks, *Checksum, Simulation->GetTick());
		if (FirstDifferentTick != INDEX_NONE)
		{
			ReplayVerdict += FString::Printf(TEXT("; first different at checkpoint tick %d"), FirstDifferentTick);
		}
		Message += TEXT(". ") + ReplayVerdict;
	}

	UE_LOG(LogCombat, Display, TEXT("%s"), *Message);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(INDEX_NONE, 10.f, bIsReplay && !ReplayVerdict.StartsWith(TEXT("Replay identical")) ? FColor::Red : FColor::Green, Message);
	}
}

int32 UCombatSubsystem::GetPlayerTeam() const
{
	return GetDefault<UCombatSettings>()->PlayerTeam;
}

void UCombatSubsystem::SelectUnit(int32 UnitId)
{
	const bool bValid = Simulation && Simulation->GetUnits().IsValidIndex(UnitId)
		&& Simulation->GetUnits()[UnitId].bAlive && Simulation->GetUnits()[UnitId].Team == GetPlayerTeam();
	SelectedUnitId = bValid ? UnitId : INDEX_NONE;
	bAwaitingMoveTarget = false;
}

void UCombatSubsystem::HandleArenaClick(const FVector& WorldPoint)
{
	if (!Simulation)
	{
		return;
	}

	const FVector2D Local(WorldPoint.X - GridOrigin.X, WorldPoint.Y - GridOrigin.Y);
	if (bAwaitingMoveTarget && SelectedUnitId != INDEX_NONE)
	{
		FCombatCommand Command;
		Command.Type = ECombatCommandType::Move;
		Command.UnitId = SelectedUnitId;
		Command.TargetCell = Simulation->GetGrid().LocalToCell(Local);
		IssueCommand(Command);
		bAwaitingMoveTarget = false;
		return;
	}

	// Select the nearest own unit under the click (a little margin around its body), or deselect.
	int32 BestId = INDEX_NONE;
	double BestDistance = TNumericLimits<double>::Max();
	for (const FCombatUnit& Unit : Simulation->GetUnits())
	{
		const double Distance = FVector2D::Distance(Local, Unit.Position);
		if (Unit.bAlive && Unit.Team == GetPlayerTeam() && Distance <= Unit.Stats.Radius + 30.0 && Distance < BestDistance)
		{
			BestDistance = Distance;
			BestId = Unit.Id;
		}
	}
	SelectUnit(BestId);
}

void UCombatSubsystem::HandleArenaCancel()
{
	if (bAwaitingMoveTarget)
	{
		bAwaitingMoveTarget = false;
	}
	else
	{
		SelectUnit(INDEX_NONE);
	}
}

const FCombatUnitType* UCombatSubsystem::GetUnitType(int32 UnitId) const
{
	return UnitTypes.IsValidIndex(UnitId) ? UnitTypes[UnitId].Get() : nullptr;
}

FGameplayTag UCombatSubsystem::GetAttackAnimationTag(int32 UnitId, int32 AttackIndex) const
{
	const FCombatUnitStats& Stats = Simulation->GetUnits()[UnitId].Stats;
	const FCombatAttackStats& Attack = Stats.GetAttack(AttackIndex);
	if (const FCombatUnitType* Type = GetUnitType(UnitId))
	{
		// Player abilities come after the attacks; SourceIndex counts within their own list.
		const TArray<FCombatSkillRow>& Skills = AttackIndex < Stats.Attacks.Num() ? Type->Attacks : Type->PlayerAbilities;
		if (Skills.IsValidIndex(Attack.SourceIndex) && Skills[Attack.SourceIndex].AnimationTag.IsValid())
		{
			return Skills[Attack.SourceIndex].AnimationTag;
		}
	}
	// No tag: no montage, the body lunges.
	return FGameplayTag();
}

ACombatUnitActor* UCombatSubsystem::GetUnitActor(int32 UnitId) const
{
	return UnitActors.IsValidIndex(UnitId) ? UnitActors[UnitId].Get() : nullptr;
}

FText UCombatSubsystem::GetAbilityName(int32 UnitId, int32 AbilityIndex) const
{
	const FCombatUnitType* Type = GetUnitType(UnitId);
	if (!Type || !Type->PlayerAbilities.IsValidIndex(AbilityIndex))
	{
		return FText::FromString(FString::Printf(TEXT("Ability %d"), AbilityIndex));
	}
	return FText::FromString(Type->PlayerAbilities[AbilityIndex].GetDisplayName());
}

FString UCombatSubsystem::GetOrderText(int32 UnitId) const
{
	if (!Simulation || !Simulation->GetUnits().IsValidIndex(UnitId))
	{
		return FString();
	}

	const FCombatUnit& Unit = Simulation->GetUnits()[UnitId];
	if (!Unit.bAlive)
	{
		return TEXT("Dead");
	}

	for (const FCombatCommand& Command : Simulation->GetPendingCommands())
	{
		if (Command.UnitId == UnitId)
		{
			return Command.Type == ECombatCommandType::Move
				? FString::Printf(TEXT("Queued: Move (%d,%d)"), Command.TargetCell.X, Command.TargetCell.Y)
				: TEXT("Queued: ") + GetAbilityName(UnitId, Command.AbilityIndex).ToString();
		}
	}

	if (Unit.bHasMoveOrder)
	{
		return FString::Printf(TEXT("Moving to (%d,%d)"), Unit.MoveTargetCell.X, Unit.MoveTargetCell.Y);
	}
	return FString();
}

TArray<FCombatStatusDisplay> UCombatSubsystem::GetStatusDisplays(const FCombatUnit& Unit)
{
	const UCombatSettings* Settings = GetDefault<UCombatSettings>();
	TArray<FCombatStatusDisplay> StatusIcons;
	for (const FCombatActiveEffect& Active : Unit.Effects.GetEffects())
	{
		FCombatStatusDisplay& Icon = StatusIcons.AddDefaulted_GetRef();
		const FCombatStatusIcon* Entry = Settings->StatusIcons.FindByPredicate([&Active](const FCombatStatusIcon& Candidate)
		{
			return Candidate.EffectTag == Active.Effect.EffectTag;
		});
		if (Entry)
		{
			Icon.Label = Entry->Label;
			Icon.Color = Entry->Color;
		}
		else
		{
			// "Effect.Burn" -> "B"
			FString Name = Active.Effect.EffectTag.GetTagName().ToString();
			Name.Split(TEXT("."), nullptr, &Name, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
			Icon.Label = Name.Left(1).ToUpper();
		}
		if (Active.Stacks > 1)
		{
			Icon.Label += FString::FromInt(Active.Stacks);
		}
	}
	return StatusIcons;
}

bool UCombatSubsystem::IssueCommand(FCombatCommand Command)
{
	if (!Simulation || Simulation->IsFinished() || bIsReplay)
	{
		return false;
	}

	// CallWave is for the whole fight, not for a unit.
	const TArray<FCombatUnit>& Units = Simulation->GetUnits();
	const bool bValidUnit = Units.IsValidIndex(Command.UnitId) && Units[Command.UnitId].Team == GetDefault<UCombatSettings>()->PlayerTeam;
	if (Command.Type == ECombatCommandType::CallWave)
	{
		Command.UnitId = INDEX_NONE;
	}
	else if (!bValidUnit)
	{
		return false;
	}

	Command.Tick = Simulation->GetTick() + FMath::Max(GetDefault<UCombatSettings>()->CommandDelayTicks, 1);
	return Simulation->QueueCommand(Command);
}

bool UCombatSubsystem::CallWave()
{
	if (!CanCallWave())
	{
		return false;
	}
	FCombatCommand Command;
	Command.Type = ECombatCommandType::CallWave;
	return IssueCommand(Command);
}

bool UCombatSubsystem::CanCallWave() const
{
	if (!Simulation || Simulation->IsFinished() || bIsReplay || Simulation->GetWavesStarted() >= Simulation->GetWaveCount())
	{
		return false;
	}
	// One call per wave: not while a call is still queued.
	return !Simulation->GetPendingCommands().ContainsByPredicate([](const FCombatCommand& Command) { return Command.Type == ECombatCommandType::CallWave; });
}

FString UCombatSubsystem::GetWaveText() const
{
	if (!Simulation || Simulation->GetWaveCount() == 0)
	{
		return FString();
	}

	const int32 Count = Simulation->GetWaveCount();
	const int32 Started = Simulation->GetWavesStarted();
	FString Text = Started > 0 ? FString::Printf(TEXT("Wave %d/%d"), Started, Count) : FString::Printf(TEXT("Wave 0/%d"), Count);
	if (Simulation->GetNextWaveTick() != INDEX_NONE && Started < Count)
	{
		const float Seconds = (Simulation->GetNextWaveTick() - Simulation->GetTick()) * Simulation->GetFixedDt();
		Text += FString::Printf(TEXT(", next in %.1f s"), FMath::Max(Seconds, 0.f));
	}
	else if (Started < Count)
	{
		Text += TEXT(", next after clear");
	}
	else if (Simulation->HasWavesLeft())
	{
		Text += TEXT(", spawning");
	}
	else
	{
		Text += TEXT(", last wave");
	}
	return Text;
}

void UCombatSubsystem::UpdateCheckpoints()
{
	const int32 Tick = Simulation->GetTick();
	if (Tick % CheckpointInterval != 0)
	{
		return;
	}

	const FString Checksum = CombatReplay::ChecksumToString(Simulation->GetChecksum());
	if (bIsReplay && FirstDifferentTick == INDEX_NONE)
	{
		const int32 Index = Checkpoints.Num();
		if (PlayingReplay.Checkpoints.IsValidIndex(Index) && PlayingReplay.Checkpoints[Index] != Checksum)
		{
			FirstDifferentTick = Tick;
			UE_LOG(LogCombat, Warning, TEXT("Replay differs from tick %d on (recorded %s, now %s)."), Tick, *PlayingReplay.Checkpoints[Index], *Checksum);
		}
	}
	Checkpoints.Add(Checksum);
}

bool UCombatSubsystem::BuildSimConfigFromSource(UWorld* World, int32 Seed, const FCombatFightSource& Source, const FCombatSimSettings& Settings,
	FCombatSimConfig& OutConfig, FVector& OutGridOrigin, TArray<TSharedPtr<const FCombatUnitType>>* OutTypes, TArray<int32>* OutRotations,
	FCombatUnitCatalog* OutUnits)
{
	if (!Source.Level.IsSet())
	{
		return false;
	}
	const FCombatLevel& Level = Source.Level.GetValue();

	// The rows of every type the level names, from the replay or from the tables. A type that is not in the table is
	// skipped by BuildConfig (with a warning); a type that names a missing skill stops the fight.
	FCombatUnitCatalog Units;
	if (Source.Units.IsSet())
	{
		Units = Source.Units.GetValue();
	}
	else
	{
		const UDataTable* UnitTable = CombatUnits::GetUnitTable();
		const UDataTable* SkillTable = CombatUnits::GetSkillTable();
		if (!UnitTable || !SkillTable)
		{
			UE_LOG(LogCombat, Error, TEXT("The unit and skill tables are not set (Project Settings > Game > Combat > Units)."));
			return false;
		}
		TArray<FString> TypeNames;
		for (const FCombatLevelUnit& Entry : Level.Units)
		{
			TypeNames.AddUnique(Entry.Type);
		}
		for (const FCombatLevelWave& Wave : Level.Waves)
		{
			for (const FCombatLevelSpawn& Entry : Wave.Spawns)
			{
				TypeNames.AddUnique(Entry.Type);
			}
		}
		for (const FString& TypeName : TypeNames)
		{
			FString Error;
			if (UnitTable->FindRowUnchecked(FName(*TypeName)) && !Units.AddFromTables(FName(*TypeName), *UnitTable, *SkillTable, Error))
			{
				UE_LOG(LogCombat, Error, TEXT("Level %s: %s"), *Level.Name, *Error);
				return false;
			}
		}
	}

	// One type per name, shared by all units of that type.
	TMap<FName, TSharedPtr<const FCombatUnitType>> Resolved;
	auto Resolve = [&Units, &Resolved](const FString& TypeName) -> TSharedPtr<const FCombatUnitType>
	{
		const FName Name(*TypeName);
		if (const TSharedPtr<const FCombatUnitType>* Found = Resolved.Find(Name))
		{
			return *Found;
		}
		return Resolved.Add(Name, Units.Resolve(Name));
	};

	const ACombatGrid* Grid = ACombatGrid::Find(World);
	OutGridOrigin = Grid ? Grid->GetActorLocation() : FVector::ZeroVector;
	OutConfig.Seed = Seed;
	OutConfig.TickRate = Settings.TickRate;
	if (!CombatLevels::BuildConfig(Level, Settings.TickRate, Resolve, OutConfig, OutTypes, OutRotations))
	{
		return false;
	}
	Settings.ApplyTo(OutConfig);
	if (OutUnits)
	{
		*OutUnits = MoveTemp(Units);
	}
	return true;
}

bool UCombatSubsystem::ResolveSource(const FString& Name, FCombatFightSource& OutSource)
{
	OutSource = FCombatFightSource();
	FString LevelName = Name.StartsWith(TEXT("Level: ")) ? Name.RightChop(7) : Name;
	if (LevelName.IsEmpty())
	{
		LevelName = GetDefault<UCombatSettings>()->DefaultLevel;
	}
	FCombatLevel Level;
	if (LevelName.IsEmpty() || !CombatLevels::Load(LevelName, Level))
	{
		UE_LOG(LogCombat, Error, TEXT("Level '%s' could not be loaded from %s."), *LevelName, *CombatLevels::GetDirectory());
		return false;
	}
	OutSource.Level = Level;
	return true;
}

void UCombatSubsystem::ShowSourceInArena(const FCombatFightSource& Source)
{
	ACombatGrid* Grid = ACombatGrid::Find(GetWorld());
	if (!Grid)
	{
		return;
	}
	if (Source.Level.IsSet())
	{
		Grid->ApplyLevel(Source.Level.GetValue());
		ShowOverviewIfChanged(true);
	}
	else
	{
		Grid->ClearLevel();
		ShowOverviewIfChanged(false);
	}
}

ACameraActor* UCombatSubsystem::GetArenaCamera()
{
	APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
	ACameraActor* Camera = PlayerController ? Cast<ACameraActor>(PlayerController->GetViewTarget()) : nullptr;
	if (Camera && ArenaCamera.Get() != Camera)
	{
		ArenaCamera = Camera;
		OriginalCameraTransform = Camera->GetActorTransform();
	}
	if (Camera && !bHasOverview)
	{
		// The map's own camera is the first overview, so moving it before the first fight is kept too.
		ACombatGrid* Grid = ACombatGrid::Find(GetWorld());
		bHasOverview = true;
		bOverviewIsLevel = false;
		OverviewGridSize = Grid ? Grid->GetShownGridData().GetLocalSize() : FVector2D::ZeroVector;
	}
	return Camera;
}

FBox UCombatSubsystem::GetCameraBounds() const
{
	const UCombatSettings* Settings = GetDefault<UCombatSettings>();
	ACombatGrid* Grid = ACombatGrid::Find(GetWorld());
	const FVector Origin = Grid ? Grid->GetActorLocation() : GridOrigin;
	const FVector2D Size = Grid ? Grid->GetShownGridData().GetLocalSize() : FVector2D::ZeroVector;
	const double Margin = Settings->CameraBoundsMargin;
	return FBox(
		Origin + FVector(-Margin, -Margin, Settings->CameraMinHeight),
		Origin + FVector(Size.X + Margin, Size.Y + Margin, FMath::Max(Settings->CameraMaxHeight, Settings->CameraMinHeight)));
}

void UCombatSubsystem::ResetCameraToOverview()
{
	ShowOverview(bHasOverview && bOverviewIsLevel);
}

void UCombatSubsystem::ShowOverview(bool bLevel)
{
	if (bLevel)
	{
		FitCameraToShownGrid();
	}
	else
	{
		RestoreCamera();
	}
	ACombatGrid* Grid = ACombatGrid::Find(GetWorld());
	bHasOverview = true;
	bOverviewIsLevel = bLevel;
	OverviewGridSize = Grid ? Grid->GetShownGridData().GetLocalSize() : FVector2D::ZeroVector;
}

void UCombatSubsystem::ShowOverviewIfChanged(bool bLevel)
{
	ACombatGrid* Grid = ACombatGrid::Find(GetWorld());
	const FVector2D GridSize = Grid ? Grid->GetShownGridData().GetLocalSize() : FVector2D::ZeroVector;
	if (!bHasOverview || bOverviewIsLevel != bLevel || !OverviewGridSize.Equals(GridSize))
	{
		ShowOverview(bLevel);
	}
}

void UCombatSubsystem::FitCameraToShownGrid()
{
	ACameraActor* Camera = GetArenaCamera();
	ACombatGrid* Grid = ACombatGrid::Find(GetWorld());
	if (!Camera || !Grid)
	{
		return;
	}

	const FCombatGridData& Data = Grid->GetShownGridData();
	const FVector2D Size = Data.GetLocalSize();
	const FVector Center = Grid->GetActorLocation() + FVector(Size.X * 0.5, Size.Y * 0.5, 0.0);

	// Straight down with grid X (width) to the right and Y (height) downwards, like the rows of a level file.
	Camera->SetActorRotation(FRotator(-90.0, -90.0, 0.0));

	// Half extents of the grid along the camera's screen axes.
	const FVector Right = Camera->GetActorRightVector();
	const FVector Up = Camera->GetActorUpVector();
	double HalfRight = 0.0;
	double HalfUp = 0.0;
	for (const FVector2D Corner : { FVector2D(-0.5, -0.5), FVector2D(0.5, -0.5), FVector2D(-0.5, 0.5), FVector2D(0.5, 0.5) })
	{
		const FVector Offset(Corner.X * Size.X, Corner.Y * Size.Y, 0.0);
		HalfRight = FMath::Max(HalfRight, FMath::Abs(FVector::DotProduct(Offset, Right)));
		HalfUp = FMath::Max(HalfUp, FMath::Abs(FVector::DotProduct(Offset, Up)));
	}

	FVector2D ViewportSize(16.0, 9.0);
	if (GetWorld()->GetGameViewport())
	{
		GetWorld()->GetGameViewport()->GetViewportSize(ViewportSize);
	}
	const double Aspect = ViewportSize.Y > 0.0 ? ViewportSize.X / ViewportSize.Y : 16.0 / 9.0;
	const double TanHalfFov = FMath::Tan(FMath::DegreesToRadians(Camera->GetCameraComponent()->FieldOfView * 0.5));
	const double Margin = 1.12;
	const double Distance = Margin * FMath::Max(HalfRight / TanHalfFov, HalfUp * Aspect / TanHalfFov);

	Camera->SetActorLocation(Center - Camera->GetActorForwardVector() * Distance);
}

void UCombatSubsystem::RestoreCamera()
{
	if (AActor* Camera = ArenaCamera.Get(); Camera && OriginalCameraTransform.IsSet())
	{
		Camera->SetActorTransform(OriginalCameraTransform.GetValue());
	}
}

void UCombatSubsystem::EnterDesignMode()
{
	if (bDesignMode)
	{
		return;
	}

	// Start from the level of the fight on screen, if it had one; else from what was edited before, else the default level.
	const TOptional<FCombatLevel> LastLevel = Simulation ? CurrentLevel : TOptional<FCombatLevel>();
	StopFight();
	bDesignMode = true;
	DesignSpawnMove.Reset();
	ResetDesignHistory();
	if (LastLevel.IsSet())
	{
		DesignLevel = LastLevel.GetValue();
		bHasDesignLevel = true;
	}
	EnsureDesignLevel();
	if (DesignUnitType.IsEmpty())
	{
		const TArray<FString> Types = CombatUnits::GetAllTypeNames();
		DesignUnitType = Types.IsEmpty() ? FString() : Types[0];
	}
	SetDesignWave(DesignWave);
	RefreshDesignView(true);
}

void UCombatSubsystem::ExitDesignMode()
{
	if (!bDesignMode)
	{
		return;
	}
	bDesignMode = false;
	DesignSpawnMove.Reset();
	DestroyDesignPreviews();
	HideDesignPiecePreview();
	if (ACombatGrid* Grid = ACombatGrid::Find(GetWorld()))
	{
		Grid->ClearLevel();
	}
	ShowOverviewIfChanged(false);
	++FightSerial;
}

void UCombatSubsystem::EnsureDesignLevel()
{
	if (bHasDesignLevel)
	{
		return;
	}
	bHasDesignLevel = true;
	const FString& Name = GetDefault<UCombatSettings>()->DefaultLevel;
	FString Message;
	if (Name.IsEmpty() || !LoadDesignLevel(Name, Message))
	{
		NewDesignLevel();
	}
}

void UCombatSubsystem::NewDesignLevel()
{
	DesignLevel = FCombatLevel::MakeEmpty(TEXT("NewLevel"), 20, 12);
	DesignWave = INDEX_NONE;
	DesignSpawnMove.Reset();
	if (DesignUnitType.IsEmpty())
	{
		const TArray<FString> Types = CombatUnits::GetAllTypeNames();
		DesignUnitType = Types.IsEmpty() ? FString() : Types[0];
	}
	if (bDesignMode)
	{
		RefreshDesignView(true);
	}
}

bool UCombatSubsystem::LoadDesignLevel(const FString& Name, FString& OutMessage)
{
	FCombatLevel Loaded;
	if (!CombatLevels::Load(Name, Loaded))
	{
		OutMessage = FString::Printf(TEXT("Could not load level %s."), *Name);
		return false;
	}
	DesignLevel = Loaded;
	DesignWave = DesignLevel.Waves.IsEmpty() ? INDEX_NONE : 0;
	DesignSpawnMove.Reset();
	if (bDesignMode)
	{
		RefreshDesignView(true);
	}
	OutMessage = FString::Printf(TEXT("Loaded level %s."), *Name);
	return true;
}

bool UCombatSubsystem::SaveDesignLevel(const FString& Name, FString& OutMessage)
{
	const FString Clean = CombatLevels::CleanName(Name);
	if (Clean.IsEmpty())
	{
		OutMessage = TEXT("Give the level a name (letters, digits, - or _).");
		return false;
	}

	DesignLevel.Name = Clean;
	if (!CombatLevels::Save(DesignLevel))
	{
		OutMessage = FString::Printf(TEXT("Could not save %s."), *Clean);
		return false;
	}
	OutMessage = FString::Printf(TEXT("Saved Levels/%s.json"), *Clean);
	UE_LOG(LogCombat, Display, TEXT("%s"), *OutMessage);
	return true;
}

bool UCombatSubsystem::RenameLevelFile(const FString& OldName, const FString& NewName, FString& OutMessage)
{
	const FString Clean = CombatLevels::CleanName(NewName);
	if (Clean.IsEmpty())
	{
		OutMessage = TEXT("Type the new name in the Name field (letters, digits, - or _).");
		return false;
	}
	if (Clean == OldName)
	{
		OutMessage = FString::Printf(TEXT("Level %s already has that name."), *OldName);
		return false;
	}
	if (CombatLevels::Exists(Clean))
	{
		OutMessage = FString::Printf(TEXT("Level %s already exists."), *Clean);
		return false;
	}
	if (!CombatLevels::Rename(OldName, Clean))
	{
		OutMessage = FString::Printf(TEXT("Could not rename %s."), *OldName);
		return false;
	}

	if (DesignLevel.Name == OldName)
	{
		DesignLevel.Name = Clean;
	}
	OutMessage = FString::Printf(TEXT("Renamed %s to %s."), *OldName, *Clean);
	UE_LOG(LogCombat, Display, TEXT("%s"), *OutMessage);
	return true;
}

bool UCombatSubsystem::DeleteLevelFile(const FString& Name, FString& OutMessage)
{
	if (!CombatLevels::Delete(Name))
	{
		OutMessage = FString::Printf(TEXT("Could not delete %s."), *Name);
		return false;
	}
	OutMessage = DesignLevel.Name == Name
		? FString::Printf(TEXT("Deleted Levels/%s.json; the open level is now unsaved."), *Name)
		: FString::Printf(TEXT("Deleted Levels/%s.json."), *Name);
	UE_LOG(LogCombat, Display, TEXT("%s"), *OutMessage);
	return true;
}

bool UCombatSubsystem::PlayDesignLevel(int32 Seed)
{
	FCombatFightSource Source;
	Source.Level = DesignLevel;
	return StartFightFromSource(Seed, Source, GetCurrentSimSettings());
}

void UCombatSubsystem::SetDesignSize(int32 Width, int32 Height)
{
	if (Width == DesignLevel.Width && Height == DesignLevel.Height)
	{
		return;
	}
	DesignLevel.Resize(Width, Height);
	DesignSpawnMove.Reset();
	if (bDesignMode)
	{
		RefreshDesignView(true);
	}
}

void UCombatSubsystem::DesignPaint(const FVector& WorldPoint, bool bErase, bool bStroke)
{
	if (!bDesignMode)
	{
		return;
	}

	// Everything this press and the stroke after it change is one undo step.
	if (!bStroke)
	{
		++DesignStrokeSerial;
	}
	ActiveDesignStroke = DesignStrokeSerial;
	ON_SCOPE_EXIT { ActiveDesignStroke = INDEX_NONE; };

	const ACombatGrid* Grid = ACombatGrid::Find(GetWorld());
	const FVector Origin = Grid ? Grid->GetActorLocation() : FVector::ZeroVector;
	const FIntPoint Cell(FMath::FloorToInt32((WorldPoint.X - Origin.X) / DesignLevel.CellSize),
		FMath::FloorToInt32((WorldPoint.Y - Origin.Y) / DesignLevel.CellSize));
	if (!DesignLevel.IsInBounds(Cell))
	{
		return;
	}

	// Eyedropper: this click only takes the color and piece of the tintable floor under it.
	if (bDesignEyedropper)
	{
		if (!bStroke)
		{
			bDesignEyedropper = false;
			const int32 FloorIndex = DesignLevel.FindPieceAt(ECombatPieceLayer::Floor, Cell);
			const UCombatPieceCatalog* Catalog = GetPieceCatalog();
			const FCombatPieceDefinition* Definition = FloorIndex != INDEX_NONE && Catalog ? Catalog->Find(DesignLevel.Pieces[FloorIndex].Id) : nullptr;
			if (Definition && Definition->bTintable)
			{
				DesignPieceColor = DesignLevel.Pieces[FloorIndex].Color;
				SetDesignPiece(Definition->Id);
			}
		}
		return;
	}

	// The unit position (one of 9) in the cell.
	int32 Position = CombatLevels::MiddlePosition;
	{
		FIntPoint SpotCell;
		GetDesignSpot(WorldPoint, SpotCell, Position);
	}

	// Moving a spawn from the spawn list: this click only places it (or cancels).
	if (DesignSpawnMove.IsSet())
	{
		if (bStroke)
		{
			return;
		}
		const FIntPoint Move = DesignSpawnMove.GetValue();
		DesignSpawnMove.Reset();
		if (!bErase && Move.X == INDEX_NONE && DesignLevel.Units.IsValidIndex(Move.Y))
		{
			FCombatLevelUnit Unit = DesignLevel.Units[Move.Y];
			Unit.Cell = Cell;
			Unit.Position = Position;
			SetDesignUnit(Move.Y, Unit);
		}
		else if (!bErase && DesignLevel.Waves.IsValidIndex(Move.X) && DesignLevel.Waves[Move.X].Spawns.IsValidIndex(Move.Y))
		{
			FCombatLevelSpawn Spawn = DesignLevel.Waves[Move.X].Spawns[Move.Y];
			Spawn.Cell = Cell;
			Spawn.Position = Position;
			SetDesignSpawn(Move.X, Move.Y, Spawn);
		}
		return;
	}

	const int32 UnitIndex = DesignLevel.FindUnitAt(Cell, Position);
	const int32 SpawnIndex = DesignLevel.FindSpawnAt(DesignWave, Cell, Position);
	bool bChanged = false;

	if (DesignTool == ECombatDesignTool::Build)
	{
		// Erasing removes the pieces of the selected piece's layer that it would cover; placing happens on a press only.
		FCombatLevelPiece Piece;
		const FCombatPieceDefinition* Definition = nullptr;
		if (!GetDesignPiecePlacement(WorldPoint, Piece, Definition))
		{
			// Nothing selected: erasing removes the topmost piece under the cursor.
			const ACombatGrid* PieceGrid = ACombatGrid::Find(GetWorld());
			const int32 Under = bErase ? DesignLevel.FindPieceUnder(FVector2D(WorldPoint - (PieceGrid ? PieceGrid->GetActorLocation() : FVector::ZeroVector))) : INDEX_NONE;
			if (Under != INDEX_NONE)
			{
				DesignLevel.Pieces.RemoveAt(Under);
				RefreshDesignView(false);
			}
			return;
		}
		if (bErase)
		{
			bChanged = DesignLevel.Pieces.RemoveAll([&Piece](const FCombatLevelPiece& Other) { return Other.Overlaps(Piece); }) > 0;
		}
		else if (!bStroke && DesignLevel.PlacePiece(Piece))
		{
			// Putting down a moved piece completes the move: one undo step with its pick-up.
			if (DesignMovingPiece.IsSet())
			{
				ActiveDesignStroke = DesignMoveStroke;
				DesignMovingPiece.Reset();
				DesignMoveStroke = INDEX_NONE;
			}
			// A piece that blocks walking cannot stand on a unit or a spawn (of any wave).
			if (Piece.Layer == ECombatPieceLayer::Cell && Piece.bBlocksWalking)
			{
				TArray<FIntPoint> Cells;
				Piece.GetCells(Cells);
				for (const FIntPoint& Covered : Cells)
				{
					DesignLevel.Units.RemoveAll([&Covered](const FCombatLevelUnit& Unit) { return Unit.Cell == Covered; });
					DesignLevel.RemoveSpawnsAt(Covered);
				}
			}
			bChanged = true;
		}
	}
	else if (bErase)
	{
		// The unit (and the selected wave's spawn) on the position, else the one in the cell nearest to the point.
		const FVector2D Local(WorldPoint.X - Origin.X, WorldPoint.Y - Origin.Y);
		auto Nearest = [this, &Cell, &Local](const auto& Entries) { return CombatSubsystemPrivate::FindNearestInCell(Entries, Cell, Local, DesignLevel.CellSize); };
		const int32 EraseUnit = UnitIndex != INDEX_NONE ? UnitIndex : Nearest(DesignLevel.Units);
		if (EraseUnit != INDEX_NONE)
		{
			DesignLevel.Units.RemoveAt(EraseUnit);
			bChanged = true;
		}
		const int32 EraseSpawn = SpawnIndex != INDEX_NONE ? SpawnIndex
			: DesignLevel.Waves.IsValidIndex(DesignWave) ? Nearest(DesignLevel.Waves[DesignWave].Spawns) : INDEX_NONE;
		if (EraseSpawn != INDEX_NONE)
		{
			DesignLevel.Waves[DesignWave].Spawns.RemoveAt(EraseSpawn);
			bChanged = true;
		}
	}
	else if (DesignTool == ECombatDesignTool::Unit)
	{
		// One unit per position, only where its size fits; placing on a unit replaces it. A moved unit keeps its team.
		const bool bWalkable = IsDesignSpotFree(Cell, Position, DesignUnitType);
		if (bStroke || !bWalkable || DesignUnitType.IsEmpty() || !(bDesignUnitSelected || DesignMovingUnit.IsSet()))
		{
			return;
		}
		FCombatLevelUnit Unit = DesignMovingUnit.IsSet() ? DesignMovingUnit.GetValue() : FCombatLevelUnit();
		Unit.Type = DesignUnitType;
		Unit.Team = DesignMovingUnit.IsSet() ? Unit.Team : DesignUnitTeam;
		Unit.Cell = Cell;
		Unit.Position = Position;
		Unit.Rotation = DesignUnitRotation;
		// Putting down a moved unit completes the move: one undo step with its pick-up.
		if (DesignMovingUnit.IsSet())
		{
			ActiveDesignStroke = DesignMoveStroke;
			DesignMovingUnit.Reset();
			DesignMoveStroke = INDEX_NONE;
		}
		if (UnitIndex != INDEX_NONE)
		{
			DesignLevel.Units[UnitIndex] = Unit;
		}
		else
		{
			DesignLevel.Units.Add(Unit);
		}
		bChanged = true;
	}
	else if (DesignTool == ECombatDesignTool::Spawn)
	{
		// One spawn per position and wave, only where its size fits; placing on a spawn replaces it. Without waves, wave 1
		// is made. A moved spawn keeps its time and goes back into its own wave.
		const bool bWalkable = IsDesignSpotFree(Cell, Position, DesignUnitType);
		if (bStroke || !bWalkable || DesignUnitType.IsEmpty() || !(bDesignUnitSelected || DesignMovingSpawn.IsSet()))
		{
			return;
		}
		if (DesignMovingSpawn.IsSet() && DesignLevel.Waves.IsValidIndex(DesignMovingSpawnWave))
		{
			DesignWave = DesignMovingSpawnWave;
		}
		if (!DesignLevel.Waves.IsValidIndex(DesignWave))
		{
			AddDesignWave();
		}
		FCombatLevelSpawn Spawn = DesignMovingSpawn.IsSet() ? DesignMovingSpawn.GetValue() : FCombatLevelSpawn();
		Spawn.Type = DesignUnitType;
		Spawn.Cell = Cell;
		Spawn.Time = DesignMovingSpawn.IsSet() ? Spawn.Time : DesignSpawnTime;
		Spawn.Position = Position;
		Spawn.Rotation = DesignUnitRotation;
		if (DesignMovingSpawn.IsSet())
		{
			ActiveDesignStroke = DesignMoveStroke;
			DesignMovingSpawn.Reset();
			DesignMovingSpawnWave = INDEX_NONE;
			DesignMoveStroke = INDEX_NONE;
		}
		const int32 Occupant = DesignLevel.FindSpawnAt(DesignWave, Cell, Position);
		TArray<FCombatLevelSpawn>& Spawns = DesignLevel.Waves[DesignWave].Spawns;
		if (Occupant != INDEX_NONE)
		{
			Spawns[Occupant] = Spawn;
		}
		else
		{
			Spawns.Add(Spawn);
		}
		bChanged = true;
	}
	if (bChanged)
	{
		RefreshDesignView(false);
	}
}

void UCombatSubsystem::RecordDesignChange()
{
	if (!bHasDesignRecorded)
	{
		DesignRecorded = DesignLevel;
		bHasDesignRecorded = true;
		return;
	}
	// Compared as JSON without the name, so Save (which sets the name) is not a change.
	auto ToComparable = [](FCombatLevel Level)
	{
		Level.Name.Reset();
		FString Json;
		CombatLevels::ToJson(Level, Json);
		return Json;
	};
	if (ToComparable(DesignLevel) == ToComparable(DesignRecorded))
	{
		return;
	}
	const bool bSameStroke = ActiveDesignStroke != INDEX_NONE && ActiveDesignStroke == LastRecordedStroke;
	if (!bSameStroke)
	{
		DesignUndo.Add(DesignRecorded);
		if (DesignUndo.Num() > MaxDesignUndo)
		{
			DesignUndo.RemoveAt(0);
		}
	}
	DesignRedo.Reset();
	LastRecordedStroke = ActiveDesignStroke;
	DesignRecorded = DesignLevel;
}

void UCombatSubsystem::ResetDesignHistory()
{
	DesignMovingPiece.Reset();
	DesignMovingUnit.Reset();
	DesignMovingSpawn.Reset();
	DesignMoveStroke = INDEX_NONE;
	DesignUndo.Reset();
	DesignRedo.Reset();
	bHasDesignRecorded = false;
	LastRecordedStroke = INDEX_NONE;
}

void UCombatSubsystem::UndoDesign()
{
	if (CanUndoDesign())
	{
		DesignRedo.Add(DesignLevel);
		RestoreDesignLevel(DesignUndo.Pop());
	}
}

void UCombatSubsystem::RedoDesign()
{
	if (CanRedoDesign())
	{
		DesignUndo.Add(DesignLevel);
		RestoreDesignLevel(DesignRedo.Pop());
	}
}

void UCombatSubsystem::RestoreDesignLevel(FCombatLevel Level)
{
	Level.Name = DesignLevel.Name;
	DesignLevel = MoveTemp(Level);
	DesignRecorded = DesignLevel;
	LastRecordedStroke = INDEX_NONE;
	DesignSpawnMove.Reset();
	DesignMovingPiece.Reset();
	DesignMovingUnit.Reset();
	DesignMovingSpawn.Reset();
	DesignMoveStroke = INDEX_NONE;
	if (!DesignLevel.Waves.IsValidIndex(DesignWave))
	{
		DesignWave = DesignLevel.Waves.Num() - 1;
	}
	// Refits the camera only if the size changed.
	RefreshDesignView(true);
}

void UCombatSubsystem::CycleWallMode()
{
	WallMode = static_cast<ECombatWallMode>((static_cast<uint8>(WallMode) + 1) % 3);
}

FText UCombatSubsystem::GetWallModeText() const
{
	switch (WallMode)
	{
	case ECombatWallMode::Up: return INVTEXT("Walls: Up (V)");
	case ECombatWallMode::Cutaway: return INVTEXT("Walls: Cutaway (V)");
	default: return INVTEXT("Walls: Down (V)");
	}
}

void UCombatSubsystem::UpdateWalls()
{
	ACombatGrid* Grid = ACombatGrid::Find(GetWorld());
	const APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
	if (!Grid || !PlayerController || !PlayerController->PlayerCameraManager)
	{
		return;
	}
	// Cutaway targets: a point in each visible unit (or LevelDesigner preview).
	TArray<FVector> Targets;
	if (WallMode == ECombatWallMode::Cutaway)
	{
		const FVector Lift(0.0, 0.0, GetDefault<UCombatSettings>()->CutawayTargetHeight);
		for (const TArray<TObjectPtr<ACombatUnitActor>>* Actors : { &UnitActors, &DesignPreviews })
		{
			for (const ACombatUnitActor* Actor : *Actors)
			{
				if (Actor && !Actor->IsHidden())
				{
					Targets.Add(Actor->GetActorLocation() + Lift);
				}
			}
		}
		if (DesignGhost && !DesignGhost->IsHidden())
		{
			Targets.Add(DesignGhost->GetActorLocation() + Lift);
		}
	}
	Grid->UpdateWalls(WallMode, PlayerController->PlayerCameraManager->GetCameraLocation(), Targets);
}

const UCombatPieceCatalog* UCombatSubsystem::GetPieceCatalog()
{
	if (!PieceCatalog)
	{
		PieceCatalog = GetDefault<UCombatSettings>()->PieceCatalog.LoadSynchronous();
	}
	return PieceCatalog;
}

bool UCombatSubsystem::PickDesignPiece(const FVector& WorldPoint)
{
	const ACombatGrid* Grid = ACombatGrid::Find(GetWorld());
	const FVector Origin = Grid ? Grid->GetActorLocation() : FVector::ZeroVector;
	CancelDesignPieceMove();
	const int32 Index = bDesignMode ? DesignLevel.FindPieceUnder(FVector2D(WorldPoint - Origin)) : INDEX_NONE;
	if (Index == INDEX_NONE)
	{
		return false;
	}
	const FCombatLevelPiece Piece = DesignLevel.Pieces[Index];
	DesignTool = ECombatDesignTool::Build;
	DesignPieceId = Piece.Id;
	// The rotation is kept in eighths; a wall item keeps its tilt and height.
	if (Piece.Layer == ECombatPieceLayer::Wall)
	{
		DesignWallItemTilt = Piece.Rotation;
		DesignWallItemHeight = Piece.Height;
	}
	else
	{
		DesignPieceRotation = Piece.Layer == ECombatPieceLayer::Detail ? Piece.Rotation : Piece.Rotation * 2;
	}

	// The pick-up is a stroke of its own; putting it down joins it, so both are one undo step.
	DesignMovingPiece = Piece;
	DesignMoveStroke = ++DesignStrokeSerial;
	ActiveDesignStroke = DesignMoveStroke;
	DesignLevel.Pieces.RemoveAt(Index);
	RefreshDesignView(false);
	ActiveDesignStroke = INDEX_NONE;
	return true;
}

bool UCombatSubsystem::PickDesignUnit(const FVector& WorldPoint)
{
	if (!bDesignMode || DesignTool == ECombatDesignTool::Build)
	{
		return false;
	}
	CancelDesignPieceMove();
	CancelDesignUnitMove();
	DesignSpawnMove.Reset();

	FIntPoint Cell;
	int32 Position = CombatLevels::MiddlePosition;
	GetDesignSpot(WorldPoint, Cell, Position);
	const ACombatGrid* Grid = ACombatGrid::Find(GetWorld());
	const FVector Origin = Grid ? Grid->GetActorLocation() : FVector::ZeroVector;
	const FVector2D Local(WorldPoint.X - Origin.X, WorldPoint.Y - Origin.Y);

	// The pick-up is a stroke of its own; putting it down joins it, so both are one undo step.
	if (DesignTool == ECombatDesignTool::Unit)
	{
		int32 Index = DesignLevel.FindUnitAt(Cell, Position);
		Index = Index != INDEX_NONE ? Index : CombatSubsystemPrivate::FindNearestInCell(DesignLevel.Units, Cell, Local, DesignLevel.CellSize);
		if (Index == INDEX_NONE)
		{
			return false;
		}
		const FCombatLevelUnit Unit = DesignLevel.Units[Index];
		DesignUnitType = Unit.Type;
		DesignUnitTeam = Unit.Team;
		DesignUnitRotation = Unit.Rotation;
		DesignMovingUnit = Unit;
		DesignMoveStroke = ++DesignStrokeSerial;
		ActiveDesignStroke = DesignMoveStroke;
		DesignLevel.Units.RemoveAt(Index);
	}
	else
	{
		if (!DesignLevel.Waves.IsValidIndex(DesignWave))
		{
			return false;
		}
		TArray<FCombatLevelSpawn>& Spawns = DesignLevel.Waves[DesignWave].Spawns;
		int32 Index = DesignLevel.FindSpawnAt(DesignWave, Cell, Position);
		Index = Index != INDEX_NONE ? Index : CombatSubsystemPrivate::FindNearestInCell(Spawns, Cell, Local, DesignLevel.CellSize);
		if (Index == INDEX_NONE)
		{
			return false;
		}
		const FCombatLevelSpawn Spawn = Spawns[Index];
		DesignUnitType = Spawn.Type;
		DesignUnitRotation = Spawn.Rotation;
		DesignMovingSpawn = Spawn;
		DesignMovingSpawnWave = DesignWave;
		DesignMoveStroke = ++DesignStrokeSerial;
		ActiveDesignStroke = DesignMoveStroke;
		Spawns.RemoveAt(Index);
	}
	bDesignUnitSelected = true;
	RefreshDesignView(false);
	ActiveDesignStroke = INDEX_NONE;
	return true;
}

void UCombatSubsystem::CancelDesignUnitMove()
{
	if (!IsMovingDesignUnit())
	{
		return;
	}
	const bool bPickUpRecorded = LastRecordedStroke == DesignMoveStroke;
	const TOptional<FCombatLevelUnit> Unit = DesignMovingUnit;
	const TOptional<FCombatLevelSpawn> Spawn = DesignMovingSpawn;
	const int32 SpawnWave = DesignMovingSpawnWave;
	DesignMovingUnit.Reset();
	DesignMovingSpawn.Reset();
	DesignMovingSpawnWave = INDEX_NONE;
	DesignMoveStroke = INDEX_NONE;
	// As for pieces: undo the pick-up if it is the last step, else add it again.
	if (bPickUpRecorded)
	{
		UndoDesign();
		DesignRedo.Reset();
		return;
	}
	if (Unit.IsSet())
	{
		DesignLevel.Units.Add(Unit.GetValue());
	}
	else if (Spawn.IsSet() && DesignLevel.Waves.IsValidIndex(SpawnWave))
	{
		DesignLevel.Waves[SpawnWave].Spawns.Add(Spawn.GetValue());
	}
	RefreshDesignView(false);
}

void UCombatSubsystem::CancelDesignPieceMove()
{
	if (!DesignMovingPiece.IsSet())
	{
		return;
	}
	const bool bPickUpRecorded = LastRecordedStroke == DesignMoveStroke;
	const FCombatLevelPiece Original = DesignMovingPiece.GetValue();
	DesignMovingPiece.Reset();
	DesignMoveStroke = INDEX_NONE;
	// If the last undo step is the level before the pick-up, going back to it puts the piece back; after other edits
	// it is placed again instead.
	if (bPickUpRecorded)
	{
		UndoDesign();
		DesignRedo.Reset();
	}
	else if (DesignLevel.PlacePiece(Original))
	{
		RefreshDesignView(false);
	}
}

void UCombatSubsystem::DesignRightClick(const FVector& WorldPoint, bool bHasPoint)
{
	if (bDesignEyedropper)
	{
		bDesignEyedropper = false;
		return;
	}
	if (DesignMovingPiece.IsSet())
	{
		CancelDesignPieceMove();
		return;
	}
	if (IsMovingDesignUnit())
	{
		CancelDesignUnitMove();
		return;
	}
	if (DesignTool == ECombatDesignTool::Build && HasDesignPieceSelected())
	{
		DesignPieceId.Reset();
		HideDesignPiecePreview();
		return;
	}
	if (DesignTool != ECombatDesignTool::Build && bDesignUnitSelected)
	{
		bDesignUnitSelected = false;
		HideDesignUnitGhost();
		return;
	}
	if (bHasPoint)
	{
		DesignPaint(WorldPoint, true, false);
	}
}

void UCombatSubsystem::SetDesignPiece(const FString& Id)
{
	if (DesignMovingPiece.IsSet() && Id != DesignPieceId)
	{
		CancelDesignPieceMove();
	}
	if (Id != DesignPieceId)
	{
		const UCombatPieceCatalog* Catalog = GetPieceCatalog();
		const FCombatPieceDefinition* Definition = Catalog ? Catalog->Find(Id) : nullptr;
		if (Definition && Definition->Layer == ECombatPieceLayer::Wall)
		{
			DesignWallItemHeight = Definition->MountHeight;
		}
	}
	DesignPieceId = Id;
}

bool UCombatSubsystem::IsDesignPieceWallItem()
{
	const UCombatPieceCatalog* Catalog = GetPieceCatalog();
	const FCombatPieceDefinition* Definition = Catalog ? Catalog->Find(DesignPieceId) : nullptr;
	return Definition && Definition->Layer == ECombatPieceLayer::Wall;
}

void UCombatSubsystem::RaiseDesignWallItem(int32 Steps)
{
	DesignWallItemHeight = FMath::Max(DesignWallItemHeight + Steps * GetDefault<UCombatSettings>()->WallItemHeightStep, 0.f);
}

void UCombatSubsystem::RotateDesignPiece(int32 Steps)
{
	const UCombatPieceCatalog* Catalog = GetPieceCatalog();
	const FCombatPieceDefinition* Definition = Catalog ? Catalog->Find(DesignPieceId) : nullptr;
	if (Definition && Definition->Layer == ECombatPieceLayer::Wall)
	{
		const int32 TiltSteps = CombatPieces::GetRotationSteps(ECombatPieceLayer::Wall);
		DesignWallItemTilt = ((DesignWallItemTilt + Steps) % TiltSteps + TiltSteps) % TiltSteps;
		return;
	}
	const int32 EighthsPerStep = Definition && Definition->Layer == ECombatPieceLayer::Detail ? 1 : 2;
	// A quarter-turn piece starts from a whole quarter, so a 45 degree detail rotation never leaves it halfway.
	const int32 Start = EighthsPerStep == 2 ? DesignPieceRotation / 2 * 2 : DesignPieceRotation;
	DesignPieceRotation = ((Start + Steps * EighthsPerStep) % 8 + 8) % 8;
}

int32 UCombatSubsystem::GetDesignPieceSteps()
{
	const UCombatPieceCatalog* Catalog = GetPieceCatalog();
	const FCombatPieceDefinition* Definition = Catalog ? Catalog->Find(DesignPieceId) : nullptr;
	if (Definition && Definition->Layer == ECombatPieceLayer::Wall)
	{
		return DesignWallItemTilt;
	}
	return Definition && Definition->Layer == ECombatPieceLayer::Detail ? DesignPieceRotation : DesignPieceRotation / 2;
}

float UCombatSubsystem::GetDesignPieceDegrees()
{
	const UCombatPieceCatalog* Catalog = GetPieceCatalog();
	const FCombatPieceDefinition* Definition = Catalog ? Catalog->Find(DesignPieceId) : nullptr;
	return GetDesignPieceSteps() * 360.f / CombatPieces::GetRotationSteps(Definition ? Definition->Layer : ECombatPieceLayer::Cell);
}

bool UCombatSubsystem::GetDesignPiecePlacement(const FVector& WorldPoint, FCombatLevelPiece& OutPiece, const FCombatPieceDefinition*& OutDefinition)
{
	const UCombatPieceCatalog* Catalog = GetPieceCatalog();
	OutDefinition = Catalog ? Catalog->Find(DesignPieceId) : nullptr;
	if (!OutDefinition)
	{
		return false;
	}
	const ACombatGrid* Grid = ACombatGrid::Find(GetWorld());
	const FVector Origin = Grid ? Grid->GetActorLocation() : FVector::ZeroVector;
	OutPiece = CombatPieces::PlaceAt(*OutDefinition, FVector2D(WorldPoint - Origin), GetDesignPieceSteps(), DesignLevel.CellSize);
	if (OutDefinition->Layer == ECombatPieceLayer::Wall)
	{
		OutPiece.Height = DesignWallItemHeight;
	}
	// A moved piece keeps its own color.
	if (OutDefinition->bTintable)
	{
		OutPiece.Color = DesignMovingPiece.IsSet() ? DesignMovingPiece->Color : DesignPieceColor;
	}
	return true;
}

bool UCombatSubsystem::IsDesignPieceTintable()
{
	const UCombatPieceCatalog* Catalog = GetPieceCatalog();
	const FCombatPieceDefinition* Definition = Catalog ? Catalog->Find(DesignPieceId) : nullptr;
	return Definition && Definition->bTintable;
}

void UCombatSubsystem::UpdateDesignPiecePreview(const FVector& WorldPoint, bool bErase)
{
	ACombatGrid* Grid = ACombatGrid::Find(GetWorld());
	if (!Grid)
	{
		return;
	}
	FCombatLevelPiece Piece;
	const FCombatPieceDefinition* Definition = nullptr;
	if (!bDesignMode || !GetDesignPiecePlacement(WorldPoint, Piece, Definition) || !Definition->Mesh)
	{
		Grid->HidePiecePreview();
		return;
	}
	const double BaseHeight = Piece.Layer == ECombatPieceLayer::Detail ? CombatPieces::FindDetailBaseHeight(DesignLevel, *GetPieceCatalog(), Piece) : 0.0;
	const bool bWallItem = Piece.Layer == ECombatPieceLayer::Wall;
	const FTransform MeshTransform = bWallItem
		? CombatPieces::ComputeWallItemTransform(Piece, DesignLevel.CellSize, Definition->Mesh->GetBoundingBox(), Definition->MeshYaw,
			Definition->Offset, CombatPieces::FindWallSurfaceOffset(DesignLevel, *GetPieceCatalog(), Piece))
		: CombatPieces::ComputeMeshTransform(Piece, DesignLevel.CellSize, Definition->Mesh->GetBoundingBox(),
			Definition->MeshYaw, Definition->Offset, Definition->bScaleToFit, BaseHeight);
	// A wall item fits only on a wall without an opening.
	const bool bFits = DesignLevel.IsPieceInBounds(Piece) && (!bWallItem || DesignLevel.IsWallItemSupported(Piece));
	Grid->ShowPiecePreview(Piece, DesignLevel.CellSize, Definition->Mesh, MeshTransform, bFits, bErase, Definition->bTintable);
	// An opening being placed shows its cut in the walls it would stand in.
	const bool bOpening = !bErase && Piece.Layer == ECombatPieceLayer::Edge && Piece.Slot == FCombatLevelPiece::OpeningSlot;
	Grid->UpdatePreviewCuts(bOpening ? &Piece : nullptr, MeshTransform, Definition->GetCutBox(Definition->Mesh->GetBoundingBox()), DesignLevel.CellSize);
}

void UCombatSubsystem::HideDesignPiecePreview()
{
	if (ACombatGrid* Grid = ACombatGrid::Find(GetWorld()))
	{
		Grid->HidePiecePreview();
	}
}

void UCombatSubsystem::RotateDesignUnit(int32 Steps)
{
	DesignUnitRotation = ((DesignUnitRotation + Steps) % CombatLevels::UnitRotationSteps + CombatLevels::UnitRotationSteps) % CombatLevels::UnitRotationSteps;
}

void UCombatSubsystem::UpdateDesignUnitGhost(const FVector& WorldPoint)
{
	ACombatGrid* Grid = ACombatGrid::Find(GetWorld());
	const TSharedPtr<const FCombatUnitType> UnitType = CombatUnits::FindType(DesignUnitType);
	if (!bDesignMode || !Grid || !UnitType || !(bDesignUnitSelected || IsMovingDesignUnit()))
	{
		HideDesignUnitGhost();
		return;
	}

	// Rebuilt only when the type or team changes; otherwise it moves and turns.
	const UCombatSettings* Settings = GetDefault<UCombatSettings>();
	const int32 Team = DesignTool == ECombatDesignTool::Spawn ? FCombatSimConfig().WaveTeam : DesignUnitTeam;
	if (!DesignGhost || DesignGhostType != DesignUnitType || DesignGhostTeam != Team)
	{
		HideDesignUnitGhost();
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		UClass* ActorClass = UnitType->Unit.LoadActorClass();
		DesignGhost = GetWorld()->SpawnActor<ACombatUnitActor>(ActorClass, WorldPoint, FRotator::ZeroRotator, SpawnParams);
		if (!DesignGhost)
		{
			return;
		}
		const FCombatUnitStats Stats = UnitType->ToSimStats(Settings->TickRate);
		const bool bRanged = Stats.Attacks.ContainsByPredicate([](const FCombatAttackStats& Attack) { return Attack.IsRanged(); });
		DesignGhost->InitUnit(INDEX_NONE, Team, Stats.Radius, Settings->GetTeamColor(Team), bRanged);
		DesignGhost->InitLook(UnitType->Unit.Look, CombatSubsystemPrivate::GetLookSeed(CurrentSeed, INDEX_NONE));
		DesignGhost->MakeGhost(Settings->DesignGhostMaterial.LoadSynchronous(), Settings->DesignGhostOpacity);
		DesignGhostType = DesignUnitType;
		DesignGhostTeam = Team;
	}

	FIntPoint Cell;
	int32 Position = CombatLevels::MiddlePosition;
	GetDesignSpot(WorldPoint, Cell, Position);
	const bool bFits = IsDesignSpotFree(Cell, Position, DesignUnitType);
	DesignGhost->SetActorHiddenInGame(false);
	DesignGhost->SetActorRotation(FRotator(0.0, CombatLevels::GetUnitYaw(DesignUnitRotation), 0.0));
	DesignGhost->UpdatePresentation(SimToWorld(DesignLevel.CellSize * FVector2D(Cell.X + 0.5, Cell.Y + 0.5)
		+ CombatLevels::GetUnitPositionOffset(Position, DesignLevel.CellSize)), FVector::ZeroVector);

	// The plate on the position: a detail-sized piece preview without a mesh (and no opening cutting walls).
	Grid->UpdatePreviewCuts(nullptr, FTransform::Identity, FBox(ForceInit), DesignLevel.CellSize);
	FCombatLevelPiece Plate;
	Plate.Layer = ECombatPieceLayer::Detail;
	Plate.Cell = Cell;
	Plate.DetailGrid = CombatLevels::UnitPositionsPerSide;
	Plate.Detail = Position;
	Grid->ShowPiecePreview(Plate, DesignLevel.CellSize, nullptr, FTransform::Identity, bFits, false);
}

void UCombatSubsystem::HideDesignUnitGhost()
{
	if (DesignGhost)
	{
		DesignGhost->Destroy();
		DesignGhost = nullptr;
		DesignGhostType.Reset();
		DesignGhostTeam = INDEX_NONE;
		if (ACombatGrid* Grid = ACombatGrid::Find(GetWorld()))
		{
			Grid->HidePiecePreview();
		}
	}
}

bool UCombatSubsystem::IsDesignCellWalkable(const FIntPoint& Cell) const
{
	const int32 PieceIndex = DesignLevel.FindPieceAt(ECombatPieceLayer::Cell, Cell);
	return PieceIndex == INDEX_NONE || !DesignLevel.Pieces[PieceIndex].bBlocksWalking;
}

void UCombatSubsystem::GetDesignSpot(const FVector& WorldPoint, FIntPoint& OutCell, int32& OutPosition) const
{
	const ACombatGrid* Grid = ACombatGrid::Find(GetWorld());
	const FVector Origin = Grid ? Grid->GetActorLocation() : FVector::ZeroVector;
	const FVector2D InCells = FVector2D(WorldPoint.X - Origin.X, WorldPoint.Y - Origin.Y) / DesignLevel.CellSize;
	OutCell = FIntPoint(FMath::FloorToInt32(InCells.X), FMath::FloorToInt32(InCells.Y));
	const int32 Side = CombatLevels::UnitPositionsPerSide;
	const int32 X = FMath::Clamp(FMath::FloorToInt32((InCells.X - OutCell.X) * Side), 0, Side - 1);
	const int32 Y = FMath::Clamp(FMath::FloorToInt32((InCells.Y - OutCell.Y) * Side), 0, Side - 1);
	OutPosition = X + Y * Side;
}

bool UCombatSubsystem::IsDesignSpotFree(const FIntPoint& Cell, int32 Position, const FString& Type)
{
	if (!DesignLevel.IsInBounds(Cell) || !IsDesignCellWalkable(Cell) || Position < 0 || Position >= FMath::Square(CombatLevels::UnitPositionsPerSide))
	{
		return false;
	}
	// The nav grids of the level as it is now (the same as a fight from it would build).
	if (DesignNavRevision != DesignRevision || DesignNavGrids.IsEmpty())
	{
		FCombatGridData Grid;
		DesignLevel.ToGridData(Grid);
		DesignNavGrids.SetNum(CombatNavigation::MaxClass + 1);
		for (int32 Class = 0; Class <= CombatNavigation::MaxClass; ++Class)
		{
			CombatNavigation::BuildNavGrid(Grid, Class, DesignNavGrids[Class]);
		}
		DesignNavRevision = DesignRevision;
	}
	const TSharedPtr<const FCombatUnitType> UnitType = CombatUnits::FindType(Type);
	const int32 Class = CombatNavigation::GetClearanceClass(UnitType ? UnitType->Unit.Radius : 0.f, DesignLevel.CellSize);
	const int32 Side = CombatLevels::UnitPositionsPerSide;
	return DesignNavGrids[Class].IsWalkable(Cell * Side + FIntPoint(Position % Side, Position / Side));
}

void UCombatSubsystem::RefreshDesignView(bool bFitCamera)
{
	// Wall items whose wall went (removed, moved, or an opening put there) go with it, in the same undo step.
	DesignLevel.RemoveUnsupportedWallItems();
	RecordDesignChange();
	++DesignRevision;
	ACombatGrid* Grid = ACombatGrid::Find(GetWorld());
	if (!Grid)
	{
		return;
	}
	Grid->ApplyLevel(DesignLevel);
	GridOrigin = Grid->GetActorLocation();
	if (bFitCamera)
	{
		ShowOverviewIfChanged(true);
	}

	// Preview units: the unit actors, standing on their cells in their rotation. No simulation.
	DestroyDesignPreviews();
	const UCombatSettings* Settings = GetDefault<UCombatSettings>();
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (int32 Index = 0; Index < DesignLevel.Units.Num(); ++Index)
	{
		const FCombatLevelUnit& Entry = DesignLevel.Units[Index];
		const TSharedPtr<const FCombatUnitType> UnitType = CombatUnits::FindType(Entry.Type);
		if (!UnitType)
		{
			continue;
		}

		const FCombatUnitStats Stats = UnitType->ToSimStats(Settings->TickRate);
		const FVector Location = SimToWorld(DesignLevel.CellSize * FVector2D(Entry.Cell.X + 0.5, Entry.Cell.Y + 0.5)
			+ CombatLevels::GetUnitPositionOffset(Entry.Position, DesignLevel.CellSize));
		UClass* ActorClass = UnitType->Unit.LoadActorClass();
		ACombatUnitActor* Actor = GetWorld()->SpawnActor<ACombatUnitActor>(ActorClass, Location, FRotator(0.0, CombatLevels::GetUnitYaw(Entry.Rotation), 0.0), SpawnParams);
		if (Actor)
		{
			const bool bRanged = Stats.Attacks.ContainsByPredicate([](const FCombatAttackStats& Attack) { return Attack.IsRanged(); });
			Actor->InitUnit(Index, Entry.Team, Stats.Radius, Settings->GetTeamColor(Entry.Team), bRanged);
			Actor->InitLook(UnitType->Unit.Look, CombatSubsystemPrivate::GetLookSeed(CurrentSeed, Index));
			Actor->SetHealth(1.f);
			Actor->UpdatePresentation(Location, FVector::ZeroVector);
			DesignPreviews.Add(Actor);
		}
	}

	// The selected wave's spawns, labelled with their time.
	if (!DesignLevel.Waves.IsValidIndex(DesignWave))
	{
		return;
	}
	const int32 WaveTeam = FCombatSimConfig().WaveTeam;
	const TArray<FCombatLevelSpawn>& Spawns = DesignLevel.Waves[DesignWave].Spawns;
	for (int32 Index = 0; Index < Spawns.Num(); ++Index)
	{
		const FCombatLevelSpawn& Entry = Spawns[Index];
		const TSharedPtr<const FCombatUnitType> UnitType = CombatUnits::FindType(Entry.Type);
		if (!UnitType)
		{
			continue;
		}

		const FCombatUnitStats Stats = UnitType->ToSimStats(Settings->TickRate);
		const FVector Location = SimToWorld(DesignLevel.CellSize * FVector2D(Entry.Cell.X + 0.5, Entry.Cell.Y + 0.5)
			+ CombatLevels::GetUnitPositionOffset(Entry.Position, DesignLevel.CellSize));
		UClass* ActorClass = UnitType->Unit.LoadActorClass();
		ACombatUnitActor* Actor = GetWorld()->SpawnActor<ACombatUnitActor>(ActorClass, Location, FRotator(0.0, CombatLevels::GetUnitYaw(Entry.Rotation), 0.0), SpawnParams);
		if (Actor)
		{
			const bool bRanged = Stats.Attacks.ContainsByPredicate([](const FCombatAttackStats& Attack) { return Attack.IsRanged(); });
			Actor->InitUnit(DesignLevel.Units.Num() + Index, WaveTeam, Stats.Radius, Settings->GetTeamColor(WaveTeam), bRanged);
			Actor->InitLook(UnitType->Unit.Look, CombatSubsystemPrivate::GetLookSeed(CurrentSeed, DesignLevel.Units.Num() + Index));
			Actor->SetHealth(1.f);
			Actor->SetStatusEffects({ { FString::Printf(TEXT("%gs"), Entry.Time), FLinearColor::Yellow } });
			Actor->UpdatePresentation(Location, FVector::ZeroVector);
			DesignPreviews.Add(Actor);
		}
	}
}

void UCombatSubsystem::SetDesignWave(int32 WaveIndex)
{
	const int32 Clamped = DesignLevel.Waves.IsEmpty() ? INDEX_NONE : FMath::Clamp(WaveIndex, 0, DesignLevel.Waves.Num() - 1);
	if (Clamped == DesignWave)
	{
		return;
	}
	DesignWave = Clamped;
	if (bDesignMode)
	{
		RefreshDesignView(false);
	}
}

void UCombatSubsystem::AddDesignWave()
{
	const int32 InsertAt = DesignLevel.Waves.IsValidIndex(DesignWave) ? DesignWave + 1 : DesignLevel.Waves.Num();
	DesignLevel.Waves.Insert(FCombatLevelWave(), InsertAt);
	DesignSpawnMove.Reset();
	DesignWave = InsertAt;
	if (bDesignMode)
	{
		RefreshDesignView(false);
	}
}

bool UCombatSubsystem::SetDesignSpawn(int32 WaveIndex, int32 SpawnIndex, const FCombatLevelSpawn& Spawn)
{
	DesignSpawnMove.Reset();
	if (!DesignLevel.Waves.IsValidIndex(WaveIndex) || !DesignLevel.Waves[WaveIndex].Spawns.IsValidIndex(SpawnIndex))
	{
		return false;
	}

	const int32 Occupant = DesignLevel.FindSpawnAt(WaveIndex, Spawn.Cell, Spawn.Position);
	const bool bValidCell = IsDesignSpotFree(Spawn.Cell, Spawn.Position, Spawn.Type)
		&& (Occupant == INDEX_NONE || Occupant == SpawnIndex);
	if (!bValidCell || Spawn.Type.IsEmpty())
	{
		// Refused: the UI rebuilds and shows the old values again.
		++DesignRevision;
		return false;
	}

	FCombatLevelSpawn& Target = DesignLevel.Waves[WaveIndex].Spawns[SpawnIndex];
	Target = Spawn;
	Target.Time = FMath::Max(Target.Time, 0.f);
	Target.Rotation = ((Target.Rotation % CombatLevels::UnitRotationSteps) + CombatLevels::UnitRotationSteps) % CombatLevels::UnitRotationSteps;
	DesignWave = WaveIndex;
	if (bDesignMode)
	{
		RefreshDesignView(false);
	}
	return true;
}

bool UCombatSubsystem::MoveDesignSpawnToWave(int32 WaveIndex, int32 SpawnIndex, int32 NewWaveIndex)
{
	DesignSpawnMove.Reset();
	if (!DesignLevel.Waves.IsValidIndex(WaveIndex) || !DesignLevel.Waves[WaveIndex].Spawns.IsValidIndex(SpawnIndex)
		|| !DesignLevel.Waves.IsValidIndex(NewWaveIndex) || NewWaveIndex == WaveIndex
		|| DesignLevel.FindSpawnAt(NewWaveIndex, DesignLevel.Waves[WaveIndex].Spawns[SpawnIndex].Cell, DesignLevel.Waves[WaveIndex].Spawns[SpawnIndex].Position) != INDEX_NONE)
	{
		++DesignRevision;
		return false;
	}

	DesignLevel.Waves[NewWaveIndex].Spawns.Add(DesignLevel.Waves[WaveIndex].Spawns[SpawnIndex]);
	DesignLevel.Waves[WaveIndex].Spawns.RemoveAt(SpawnIndex);
	DesignWave = NewWaveIndex;
	if (bDesignMode)
	{
		RefreshDesignView(false);
	}
	return true;
}

void UCombatSubsystem::RemoveDesignSpawn(int32 WaveIndex, int32 SpawnIndex)
{
	DesignSpawnMove.Reset();
	if (!DesignLevel.Waves.IsValidIndex(WaveIndex) || !DesignLevel.Waves[WaveIndex].Spawns.IsValidIndex(SpawnIndex))
	{
		return;
	}
	DesignLevel.Waves[WaveIndex].Spawns.RemoveAt(SpawnIndex);
	DesignWave = WaveIndex;
	if (bDesignMode)
	{
		RefreshDesignView(false);
	}
}

void UCombatSubsystem::BeginDesignSpawnMove(int32 WaveIndex, int32 SpawnIndex)
{
	if (IsMovingDesignSpawn(WaveIndex, SpawnIndex))
	{
		DesignSpawnMove.Reset();
		return;
	}
	DesignSpawnMove = FIntPoint(WaveIndex, SpawnIndex);
	// Show the wave being edited.
	SetDesignWave(WaveIndex);
}

bool UCombatSubsystem::SetDesignUnit(int32 UnitIndex, const FCombatLevelUnit& Unit)
{
	DesignSpawnMove.Reset();
	if (!DesignLevel.Units.IsValidIndex(UnitIndex))
	{
		return false;
	}

	const int32 Occupant = DesignLevel.FindUnitAt(Unit.Cell, Unit.Position);
	const bool bValidCell = IsDesignSpotFree(Unit.Cell, Unit.Position, Unit.Type)
		&& (Occupant == INDEX_NONE || Occupant == UnitIndex);
	if (!bValidCell || Unit.Type.IsEmpty())
	{
		// Refused: the UI rebuilds and shows the old values again.
		++DesignRevision;
		return false;
	}

	DesignLevel.Units[UnitIndex] = Unit;
	DesignLevel.Units[UnitIndex].Rotation = ((Unit.Rotation % CombatLevels::UnitRotationSteps) + CombatLevels::UnitRotationSteps) % CombatLevels::UnitRotationSteps;
	if (bDesignMode)
	{
		RefreshDesignView(false);
	}
	return true;
}

void UCombatSubsystem::RemoveDesignUnit(int32 UnitIndex)
{
	DesignSpawnMove.Reset();
	if (!DesignLevel.Units.IsValidIndex(UnitIndex))
	{
		return;
	}
	DesignLevel.Units.RemoveAt(UnitIndex);
	if (bDesignMode)
	{
		RefreshDesignView(false);
	}
}

void UCombatSubsystem::BeginDesignUnitMove(int32 UnitIndex)
{
	DesignSpawnMove = IsMovingDesignUnit(UnitIndex) ? TOptional<FIntPoint>() : TOptional<FIntPoint>(FIntPoint(INDEX_NONE, UnitIndex));
}

void UCombatSubsystem::RemoveDesignWave()
{
	if (!DesignLevel.Waves.IsValidIndex(DesignWave))
	{
		return;
	}
	DesignLevel.Waves.RemoveAt(DesignWave);
	DesignSpawnMove.Reset();
	DesignWave = DesignLevel.Waves.IsEmpty() ? INDEX_NONE : FMath::Min(DesignWave, DesignLevel.Waves.Num() - 1);
	if (bDesignMode)
	{
		RefreshDesignView(false);
	}
}

void UCombatSubsystem::DestroyDesignPreviews()
{
	for (ACombatUnitActor* Actor : DesignPreviews)
	{
		if (IsValid(Actor))
		{
			Actor->Destroy();
		}
	}
	DesignPreviews.Reset();
}

UCombatCommandScript* UCombatSubsystem::FindCommandScript(const FString& NameOrPath)
{
	if (NameOrPath.Contains(TEXT("/")))
	{
		return LoadObject<UCombatCommandScript>(nullptr, *NameOrPath);
	}

	IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();
	AssetRegistry.WaitForCompletion();

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssetsByClass(UCombatCommandScript::StaticClass()->GetClassPathName(), Assets);
	for (const FAssetData& Asset : Assets)
	{
		if (Asset.AssetName.ToString().Equals(NameOrPath, ESearchCase::IgnoreCase))
		{
			return Cast<UCombatCommandScript>(Asset.GetAsset());
		}
	}
	return nullptr;
}

namespace CombatConsole
{
	/** Project settings, with the taunt range override of the world's subsystem if there is one. */
	static FCombatSimSettings GetSimSettings(UWorld* World)
	{
		const UCombatSubsystem* Subsystem = World ? World->GetSubsystem<UCombatSubsystem>() : nullptr;
		return Subsystem ? Subsystem->GetCurrentSimSettings() : FCombatSimSettings::FromProjectSettings();
	}

	/** Removes a "script=<name>" argument and returns that script's commands (empty without one). False if it is not found. */
	static bool ExtractScript(TArray<FString>& Args, TArray<FCombatCommand>& OutCommands)
	{
		for (int32 Index = 0; Index < Args.Num(); ++Index)
		{
			FString Name;
			if (Args[Index].Split(TEXT("="), nullptr, &Name) && Args[Index].StartsWith(TEXT("script="), ESearchCase::IgnoreCase))
			{
				Args.RemoveAt(Index);
				const UCombatCommandScript* Script = UCombatSubsystem::FindCommandScript(Name);
				if (!Script)
				{
					UE_LOG(LogCombat, Error, TEXT("Command script '%s' not found."), *Name);
					return false;
				}
				OutCommands = Script->Commands;
				return true;
			}
		}
		return true;
	}

	/** Removes a "level=<name>" argument and loads that level (without one: the default level) into OutSource. False if it is not found. */
	static bool ExtractLevel(TArray<FString>& Args, FCombatFightSource& OutSource)
	{
		FString Name;
		for (int32 Index = 0; Index < Args.Num(); ++Index)
		{
			if (Args[Index].StartsWith(TEXT("level="), ESearchCase::IgnoreCase))
			{
				Name = Args[Index].RightChop(6);
				Args.RemoveAt(Index);
				break;
			}
		}
		return UCombatSubsystem::ResolveSource(Name, OutSource);
	}

	/** Parses "[seed]"; anything after it (an old setup argument) is an error. */
	static bool ParseSeed(const TArray<FString>& Args, const TCHAR* Command, int32& OutSeed)
	{
		OutSeed = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : GetDefault<UCombatSettings>()->DefaultSeed;
		if (Args.Num() > 1)
		{
			UE_LOG(LogCombat, Error, TEXT("%s: unexpected argument '%s'. Fights come from levels: use level=<name>."), Command, *Args[1]);
			return false;
		}
		return true;
	}

	static void Start(const TArray<FString>& InArgs, UWorld* World)
	{
		UCombatSubsystem* Subsystem = World ? World->GetSubsystem<UCombatSubsystem>() : nullptr;
		if (!Subsystem)
		{
			UE_LOG(LogCombat, Error, TEXT("Combat.Start needs a running game world."));
			return;
		}

		TArray<FString> Args = InArgs;
		FCombatFightSource Source;
		if (!ExtractLevel(Args, Source))
		{
			return;
		}
		int32 Seed;
		if (!ParseSeed(Args, TEXT("Combat.Start"), Seed))
		{
			return;
		}
		Subsystem->StartFightFromSource(Seed, Source, Subsystem->GetCurrentSimSettings());
	}

	static void Simulate(const TArray<FString>& InArgs, UWorld* World)
	{
		TArray<FString> Args = InArgs;
		TArray<FCombatCommand> Commands;
		FCombatFightSource Source;
		if (!ExtractScript(Args, Commands) || !ExtractLevel(Args, Source))
		{
			return;
		}

		int32 Seed;
		if (!ParseSeed(Args, TEXT("Combat.Simulate"), Seed))
		{
			return;
		}

		FCombatSimConfig Config;
		FVector GridOrigin;
		if (!UCombatSubsystem::BuildSimConfigFromSource(World, Seed, Source, GetSimSettings(World), Config, GridOrigin))
		{
			return;
		}
		Config.Commands = Commands;

		const double StartTime = FPlatformTime::Seconds();
		FCombatSimulation Simulation(Config);
		Simulation.RunToEnd();
		const double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;

		FString Result = FCombatSimulation::OutcomeToString(Simulation.GetOutcome());
		if (Simulation.GetOutcome() == ECombatOutcome::TeamWon)
		{
			Result += FString::Printf(TEXT(" (team %d)"), Simulation.GetWinningTeam());
		}
		UE_LOG(LogCombat, Display, TEXT("Combat.Simulate seed %d, %s: %s after %d ticks (%.1f s), checksum 0x%08X, %.2f ms."),
			Seed, *Source.GetName(), *Result, Simulation.GetTick(), Simulation.GetTick() * Simulation.GetFixedDt(),
			Simulation.GetChecksum(), ElapsedMs);
	}

	static void Stop(const TArray<FString>& Args, UWorld* World)
	{
		if (UCombatSubsystem* Subsystem = World ? World->GetSubsystem<UCombatSubsystem>() : nullptr)
		{
			Subsystem->StopFight();
		}
	}

	/** Combat.Batch <count> [startseed] [csv] [level=<name>] [script=<name>] */
	static void Batch(const TArray<FString>& InArgs, UWorld* World)
	{
		TArray<FString> Args = InArgs;
		TArray<FCombatCommand> Commands;
		FCombatFightSource Source;
		if (!ExtractScript(Args, Commands) || !ExtractLevel(Args, Source))
		{
			return;
		}

		TArray<FString> Positional;
		bool bCsv = false;
		for (const FString& Arg : Args)
		{
			if (Arg.Equals(TEXT("csv"), ESearchCase::IgnoreCase))
			{
				bCsv = true;
			}
			else
			{
				Positional.Add(Arg);
			}
		}

		const int32 Count = Positional.Num() > 0 ? FCString::Atoi(*Positional[0]) : 100;
		const int32 StartSeed = Positional.Num() > 1 ? FCString::Atoi(*Positional[1]) : 1;
		if (Positional.Num() > 2)
		{
			UE_LOG(LogCombat, Error, TEXT("Combat.Batch: unexpected argument '%s'. Fights come from levels: use level=<name>."), *Positional[2]);
			return;
		}

		FString Summary;
		UCombatSubsystem::RunBatchInWorld(World, Source, Count, StartSeed, GetSimSettings(World), bCsv, Summary, Commands);
	}

	static void SaveReplay(const TArray<FString>& Args, UWorld* World)
	{
		const UCombatSubsystem* Subsystem = World ? World->GetSubsystem<UCombatSubsystem>() : nullptr;
		FString Message = TEXT("Combat.SaveReplay needs a running game world.");
		if (Subsystem && !Subsystem->SaveReplay(Message))
		{
			UE_LOG(LogCombat, Warning, TEXT("%s"), *Message);
		}
	}

	static void Replay(const TArray<FString>& Args, UWorld* World)
	{
		UCombatSubsystem* Subsystem = World ? World->GetSubsystem<UCombatSubsystem>() : nullptr;
		if (!Subsystem || Args.IsEmpty())
		{
			UE_LOG(LogCombat, Error, TEXT("Combat.Replay <file> needs a running game world and a file in Saved/Replays (or a path)."));
			return;
		}
		FString Message;
		Subsystem->PlayReplay(Args[0], Message);
	}

	/** Combat.Move <unit> <x> <y> and Combat.Ability <unit> <index>: player commands from the console (until the unit list exists). */
	static void IssueFromConsole(UWorld* World, const FCombatCommand& Command)
	{
		UCombatSubsystem* Subsystem = World ? World->GetSubsystem<UCombatSubsystem>() : nullptr;
		if (!Subsystem || !Subsystem->IssueCommand(Command))
		{
			UE_LOG(LogCombat, Warning, TEXT("Command not accepted (no running fight, a replay plays, or the unit is not on the player's team)."));
		}
	}

	static void Move(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 3)
		{
			UE_LOG(LogCombat, Error, TEXT("Combat.Move <unit> <x> <y>"));
			return;
		}
		FCombatCommand Command;
		Command.Type = ECombatCommandType::Move;
		Command.UnitId = FCString::Atoi(*Args[0]);
		Command.TargetCell = FIntPoint(FCString::Atoi(*Args[1]), FCString::Atoi(*Args[2]));
		IssueFromConsole(World, Command);
	}

	static void Ability(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.IsEmpty())
		{
			UE_LOG(LogCombat, Error, TEXT("Combat.Ability <unit> [index]"));
			return;
		}
		FCombatCommand Command;
		Command.Type = ECombatCommandType::Ability;
		Command.UnitId = FCString::Atoi(*Args[0]);
		Command.AbilityIndex = Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 0;
		IssueFromConsole(World, Command);
	}

	static void CallWave(const TArray<FString>& Args, UWorld* World)
	{
		UCombatSubsystem* Subsystem = World ? World->GetSubsystem<UCombatSubsystem>() : nullptr;
		if (!Subsystem || !Subsystem->CallWave())
		{
			UE_LOG(LogCombat, Warning, TEXT("Wave call not accepted (no running fight, a replay plays, no wave left, or a call is already queued)."));
		}
	}

	/** Debug: puts a mesh (asset name or object path; none = empty) in a swappable slot of a unit's look. */
	static void SetSlot(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 2)
		{
			UE_LOG(LogCombat, Error, TEXT("Combat.SetSlot <unit> <slot> [mesh]"));
			return;
		}
		const UCombatSubsystem* Subsystem = World ? World->GetSubsystem<UCombatSubsystem>() : nullptr;
		ACombatUnitActor* Actor = Subsystem ? Subsystem->GetUnitActor(FCString::Atoi(*Args[0])) : nullptr;
		if (!Actor)
		{
			UE_LOG(LogCombat, Error, TEXT("No unit %s in a running fight."), *Args[0]);
			return;
		}

		const FString SlotName = Args[1].StartsWith(TEXT("Slot."), ESearchCase::IgnoreCase) ? Args[1].RightChop(5) : Args[1];
		const int64 SlotValue = StaticEnum<ECombatLookSlot>()->GetValueByNameString(SlotName);
		if (SlotValue == INDEX_NONE)
		{
			UE_LOG(LogCombat, Error, TEXT("Unknown slot %s (Body, Face, Hair, Hat, Glasses, Shirt, Outwear, Pants, Shoes, Gloves, Backpack)."), *SlotName);
			return;
		}
		const ECombatLookSlot Slot = static_cast<ECombatLookSlot>(SlotValue);

		USkeletalMesh* Mesh = nullptr;
		if (Args.Num() > 2)
		{
			if (Args[2].Contains(TEXT("/")))
			{
				Mesh = LoadObject<USkeletalMesh>(nullptr, *Args[2]);
			}
			else
			{
				TArray<FAssetData> Assets;
				IAssetRegistry::GetChecked().GetAssetsByClass(USkeletalMesh::StaticClass()->GetClassPathName(), Assets);
				const FAssetData* Found = Assets.FindByPredicate([&Args](const FAssetData& Asset) { return Asset.AssetName.ToString() == Args[2]; });
				Mesh = Found ? Cast<USkeletalMesh>(Found->GetAsset()) : nullptr;
			}
			if (!Mesh)
			{
				UE_LOG(LogCombat, Error, TEXT("Skeletal mesh %s not found."), *Args[2]);
				return;
			}
		}

		if (!Actor->SetSlotMesh(Slot, Mesh))
		{
			UE_LOG(LogCombat, Warning, TEXT("Slot %s of unit %s cannot change (no look, or a merged slot)."), *SlotName, *Args[0]);
		}
	}

	static FAutoConsoleCommandWithWorldAndArgs SetSlotCommand(
		TEXT("Combat.SetSlot"),
		TEXT("Combat.SetSlot <unit> <slot> [mesh]: presentation debug, puts a skeletal mesh (asset name or path; none = empty) in a swappable slot (Hat) of the unit's look."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetSlot));

	static FAutoConsoleCommandWithWorldAndArgs CallWaveCommand(
		TEXT("Combat.CallWave"),
		TEXT("Combat.CallWave: player command, the next wave starts now (runs CommandDelayTicks later)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&CallWave));

	static FAutoConsoleCommandWithWorldAndArgs MoveCommand(
		TEXT("Combat.Move"),
		TEXT("Combat.Move <unit> <x> <y>: player command, the unit walks to that cell (runs CommandDelayTicks later)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Move));

	static FAutoConsoleCommandWithWorldAndArgs AbilityCommand(
		TEXT("Combat.Ability"),
		TEXT("Combat.Ability <unit> [index]: player command, the unit uses its player ability (runs CommandDelayTicks later)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Ability));

	static FAutoConsoleCommandWithWorldAndArgs BatchCommand(
		TEXT("Combat.Batch"),
		TEXT("Combat.Batch <count> [startseed] [csv] [level=<name>] [script=<name>]: runs fights headless from a LevelDesigner level (default: DefaultLevel from the Combat settings), optionally all with the same command script, and reports win rates, durations and per unit type statistics."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Batch));

	static FAutoConsoleCommandWithWorldAndArgs SaveReplayCommand(
		TEXT("Combat.SaveReplay"),
		TEXT("Combat.SaveReplay: saves the finished fight as a replay in Saved/Replays."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SaveReplay));

	static FAutoConsoleCommandWithWorldAndArgs ReplayCommand(
		TEXT("Combat.Replay"),
		TEXT("Combat.Replay <file>: plays a replay from Saved/Replays (or a path) and checks that it ends the same."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Replay));

	static FAutoConsoleCommandWithWorldAndArgs StartCommand(
		TEXT("Combat.Start"),
		TEXT("Combat.Start [seed] [level=<name>]: starts a fight with presentation from a LevelDesigner level (default: DefaultLevel from the Combat settings)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Start));

	static FAutoConsoleCommandWithWorldAndArgs SimulateCommand(
		TEXT("Combat.Simulate"),
		TEXT("Combat.Simulate [seed] [level=<name>] [script=<name>]: runs a fight headless from a LevelDesigner level (default: DefaultLevel from the Combat settings), optionally with a command script, and prints the outcome, duration and final checksum."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Simulate));

	static FAutoConsoleCommandWithWorldAndArgs StopCommand(
		TEXT("Combat.Stop"),
		TEXT("Combat.Stop: stops the running fight and removes its units."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Stop));
}
