// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatSubsystem.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Combat/CombatAppearance.h"
#include "Combat/CombatBatch.h"
#include "Combat/CombatCommandScript.h"
#include "Combat/CombatCueTable.h"
#include "Combat/CombatGrid.h"
#include "Combat/CombatProjectileActor.h"
#include "Combat/CombatSettings.h"
#include "Combat/CombatSetup.h"
#include "Combat/CombatTags.h"
#include "Combat/CombatUnitActor.h"
#include "Combat/CombatUnitDefinition.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
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

bool UCombatSubsystem::StartFight(int32 Seed, const UCombatSetup* Setup)
{
	return StartFightWithSettings(Seed, Setup, GetCurrentSimSettings());
}

FString FCombatFightSource::GetName() const
{
	if (Level.IsSet())
	{
		return TEXT("Level: ") + Level->Name;
	}
	return Setup ? Setup->GetName() : FString();
}

bool UCombatSubsystem::StartFightWithSettings(int32 Seed, const UCombatSetup* Setup, const FCombatSimSettings& SimSettings,
	TConstArrayView<FCombatCommand> Commands)
{
	FCombatFightSource Source;
	Source.Setup = Setup;
	return StartFightFromSource(Seed, Source, SimSettings, Commands);
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
		UE_LOG(LogCombat, Error, TEXT("StartFight: no setup or level."));
		return false;
	}
	bDesignMode = false;
	DestroyDesignPreviews();

	FCombatSimConfig Config;
	TArray<const UCombatUnitDefinition*> Definitions;
	if (!BuildSimConfigFromSource(GetWorld(), Seed, Source, SimSettings, Config, GridOrigin, &Definitions))
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
	CurrentSetupName = Source.GetName();
	CurrentSetupPath = Source.Setup ? Source.Setup->GetPathName() : FString();
	CurrentLevel = Source.Level;
	CurrentSettings = SimSettings;

	for (const UCombatUnitDefinition* Definition : Definitions)
	{
		SourceDefinitions.Add(const_cast<UCombatUnitDefinition*>(Definition));
	}
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
	Replay.MapName = UWorld::RemovePIEPrefix(GetWorld()->GetMapName());
	Replay.GridChecksum = CombatReplay::ChecksumToString(Simulation->GetGrid().ComputeChecksum());
	Replay.SetupPath = CurrentSetupPath;
	Replay.bHasLevel = CurrentLevel.IsSet();
	if (CurrentLevel.IsSet())
	{
		Replay.Level = CurrentLevel.GetValue();
	}
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

	const FString FileName = FString::Printf(TEXT("%s_%s_%d.json"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")), *CurrentSetupName, CurrentSeed);
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

	FCombatFightSource Source;
	if (Replay.bHasLevel)
	{
		Source.Level = Replay.Level;
		Source.Level->Normalize();
	}
	else
	{
		Source.Setup = LoadObject<UCombatSetup>(nullptr, *Replay.SetupPath);
	}
	if (!Source.IsValid() || !StartFightFromSource(Replay.Seed, Source, Replay.Settings, Replay.Commands))
	{
		OutMessage = FString::Printf(TEXT("Could not start the replay (%s)."), Replay.bHasLevel ? *(TEXT("level ") + Replay.Level.Name) : *Replay.SetupPath);
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
	if (Replay.MapName != UWorld::RemovePIEPrefix(GetWorld()->GetMapName()))
	{
		Warnings.Add(FString::Printf(TEXT("other map (%s)"), *Replay.MapName));
	}
	if (!Replay.bHasLevel && Replay.GridChecksum != CombatReplay::ChecksumToString(Simulation->GetGrid().ComputeChecksum()))
	{
		Warnings.Add(TEXT("the arena grid changed"));
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
		OutSummary = TEXT("No setup or level for the batch.");
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
	TArray<const UCombatUnitDefinition*> Definitions;
	if (!BuildSimConfigFromSource(World, StartSeed, Source, Settings, Config, GridOrigin, &Definitions))
	{
		OutSummary = FString::Printf(TEXT("Could not build %s."), *Source.GetName());
		return false;
	}

	// The same commands on every seed: what does this player input do on average?
	Config.Commands = Commands;

	TArray<FString> UnitTypeNames;
	for (const UCombatUnitDefinition* Definition : Definitions)
	{
		UnitTypeNames.Add(Definition->GetName());
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
	UnitDefinitions.Reset();
	SourceDefinitions.Reset();

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
				Actor->OnAttack(Event.TargetId != INDEX_NONE ? SimToWorld(Units[Event.TargetId].Position) : Actor->GetActorLocation());
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

void UCombatSubsystem::SpawnUnitActor(const FCombatUnit& Unit)
{
	// Unit IDs grow by one with every spawn, so the actor arrays stay indexed by unit ID.
	check(UnitActors.Num() == Unit.Id);
	UCombatUnitDefinition* Definition = SourceDefinitions.IsValidIndex(Unit.SourceIndex) ? SourceDefinitions[Unit.SourceIndex].Get() : nullptr;
	UClass* ActorClass = Definition && Definition->ActorClass ? Definition->ActorClass.Get() : ACombatUnitActor::StaticClass();

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACombatUnitActor* Actor = GetWorld()->SpawnActor<ACombatUnitActor>(ActorClass, SimToWorld(Unit.Position), FRotator::ZeroRotator, SpawnParams);
	if (Actor)
	{
		const bool bRanged = Unit.Stats.Attacks.ContainsByPredicate([](const FCombatAttackStats& Attack) { return Attack.IsRanged(); });
		Actor->InitUnit(Unit.Id, Unit.Team, Unit.Stats.Radius, GetDefault<UCombatSettings>()->GetTeamColor(Unit.Team), bRanged);
		if (Definition && Definition->Appearance)
		{
			// Same seed and unit ID, same look: a replay shows the same figures.
			Actor->InitAppearance(Definition->Appearance, static_cast<int32>(HashCombine(GetTypeHash(CurrentSeed), GetTypeHash(Unit.Id))));
		}
	}
	UnitActors.Add(Actor);
	UnitDefinitions.Add(Definition);
}

void UCombatSubsystem::SpawnProjectileActor(const FCombatEvent& Event)
{
	// The projectile actor class comes from the attack definition the simulation attack was made from.
	UClass* ActorClass = ACombatProjectileActor::StaticClass();
	const FCombatUnit& Source = Simulation->GetUnits()[Event.SourceId];
	const int32 SourceIndex = Source.Stats.GetAttack(Event.AttackIndex).SourceIndex;
	const UCombatUnitDefinition* Definition = UnitDefinitions[Event.SourceId];
	if (Definition && Definition->Attacks.IsValidIndex(SourceIndex) && Definition->Attacks[SourceIndex].ProjectileActorClass)
	{
		ActorClass = Definition->Attacks[SourceIndex].ProjectileActorClass.Get();
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

		FVector2D Facing = Unit.Velocity;
		if (Unit.TargetId != INDEX_NONE && Units[Unit.TargetId].bAlive)
		{
			const FCombatUnit& Target = Units[Unit.TargetId];
			Facing = FMath::Lerp(Target.PreviousPosition, Target.Position, Alpha) - Position;
		}

		Actor->SetHealth(Unit.Stats.MaxHP > 0.f ? Unit.HP / Unit.Stats.MaxHP : 0.f);

		Actor->SetStatusEffects(GetStatusDisplays(Unit));
		if (Actor->HasAppearanceOverrides())
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

	const FCombatDistanceMap* Map = Level >= 2 ? Simulation->GetDistanceMap(0) : nullptr;
	if (Map)
	{
		const FCombatGridData& Grid = Simulation->GetGrid();
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
				DrawDebugCircle(World, Center, Attack.AreaRadius + Unit.Stats.Radius, 64, RangeColor, false, -1.f, 0, 2.f,
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
	else if (Attack.Type.MatchesTagExact(CombatTags::Attack_Taunt))
	{
		Color = Settings->TauntColor;
	}

	DrawArea(Event.Area, Color.ToFColor(true), Settings->AreaPulseDuration, 6.f);
	PlayCue(Event.Cue, SimToWorld(Event.Area.Center));

	if (ACombatUnitActor* Actor = UnitActors[Event.SourceId])
	{
		Actor->OnAreaAttack(Event.Area.Radius + (Event.Area.Shape == ECombatAreaShape::CircleAtTarget ? 0.f : Event.Area.SourceRadius));
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

	// Edge-to-edge shapes reach from the attacker's edge, so draw them from its center with its radius added.
	const float Reach = (Area.Radius + (Area.Shape == ECombatAreaShape::CircleAtTarget ? 0.f : Area.SourceRadius)) * Scale;
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

bool UCombatSubsystem::BuildSimConfig(UWorld* World, int32 Seed, const UCombatSetup& Setup, const FCombatSimSettings& SimSettings,
	FCombatSimConfig& OutConfig, FVector& OutGridOrigin, TArray<const UCombatUnitDefinition*>* OutDefinitions)
{
	const UCombatSettings* Settings = GetDefault<UCombatSettings>();

	if (ACombatGrid* Grid = ACombatGrid::Find(World))
	{
		OutConfig.Grid = Grid->GetGridData();
		OutGridOrigin = Grid->GetActorLocation();
	}
	else
	{
		OutConfig.Grid.Init(Settings->FallbackGridSize.X, Settings->FallbackGridSize.Y, Settings->FallbackCellSize);
		OutGridOrigin = FVector::ZeroVector;
	}

	OutConfig.Seed = Seed;
	OutConfig.TickRate = SimSettings.TickRate;

	OutConfig.Units.Reset();
	for (int32 Index = 0; Index < Setup.Units.Num(); ++Index)
	{
		const FCombatSetupEntry& Entry = Setup.Units[Index];
		if (!Entry.Definition)
		{
			UE_LOG(LogCombat, Warning, TEXT("%s: entry %d has no definition, skipped."), *Setup.GetName(), Index);
			continue;
		}
		if (!OutConfig.Grid.IsWalkable(Entry.StartCell))
		{
			UE_LOG(LogCombat, Warning, TEXT("%s: entry %d starts in cell (%d,%d), which is outside the grid or blocked; skipped."),
				*Setup.GetName(), Index, Entry.StartCell.X, Entry.StartCell.Y);
			continue;
		}

		FCombatUnitSpawn& Spawn = OutConfig.Units.AddDefaulted_GetRef();
		Spawn.Stats = Entry.Definition->ToSimStats(OutConfig.TickRate);
		Spawn.Team = Entry.Team;
		Spawn.StartCell = Entry.StartCell;
		if (OutDefinitions)
		{
			OutDefinitions->Add(Entry.Definition);
		}
	}

	if (OutConfig.Units.IsEmpty())
	{
		UE_LOG(LogCombat, Error, TEXT("%s: no valid units."), *Setup.GetName());
		return false;
	}

	SimSettings.ApplyTo(OutConfig);
	return true;
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

const UCombatUnitDefinition* UCombatSubsystem::GetUnitDefinition(int32 UnitId) const
{
	return UnitDefinitions.IsValidIndex(UnitId) ? UnitDefinitions[UnitId].Get() : nullptr;
}

ACombatUnitActor* UCombatSubsystem::GetUnitActor(int32 UnitId) const
{
	return UnitActors.IsValidIndex(UnitId) ? UnitActors[UnitId].Get() : nullptr;
}

FText UCombatSubsystem::GetAbilityName(int32 UnitId, int32 AbilityIndex) const
{
	const UCombatUnitDefinition* Definition = GetUnitDefinition(UnitId);
	if (!Definition || !Definition->PlayerAbilities.IsValidIndex(AbilityIndex))
	{
		return FText::FromString(FString::Printf(TEXT("Ability %d"), AbilityIndex));
	}

	const FCombatAttackDefinition& Ability = Definition->PlayerAbilities[AbilityIndex];
	if (!Ability.DisplayName.IsEmpty())
	{
		return Ability.DisplayName;
	}
	FString Name = Ability.Type.GetTagName().ToString();
	Name.Split(TEXT("."), nullptr, &Name, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	return FText::FromString(Name);
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
	FCombatSimConfig& OutConfig, FVector& OutGridOrigin, TArray<const UCombatUnitDefinition*>* OutDefinitions)
{
	if (!Source.Level.IsSet())
	{
		return Source.Setup && BuildSimConfig(World, Seed, *Source.Setup, Settings, OutConfig, OutGridOrigin, OutDefinitions);
	}

	const ACombatGrid* Grid = ACombatGrid::Find(World);
	OutGridOrigin = Grid ? Grid->GetActorLocation() : FVector::ZeroVector;
	OutConfig.Seed = Seed;
	OutConfig.TickRate = Settings.TickRate;
	if (!CombatLevels::BuildConfig(Source.Level.GetValue(), Settings.TickRate, &UCombatSubsystem::FindUnitDefinition, OutConfig, OutDefinitions))
	{
		return false;
	}
	Settings.ApplyTo(OutConfig);
	return true;
}

TArray<FString> UCombatSubsystem::GetAllSourceNames()
{
	TArray<FString> Names = GetAllSetupNames();
	for (const FString& Level : CombatLevels::FindLevelNames())
	{
		Names.Add(TEXT("Level: ") + Level);
	}
	return Names;
}

bool UCombatSubsystem::ResolveSource(const FString& Name, FCombatFightSource& OutSource)
{
	OutSource = FCombatFightSource();
	FString LevelName;
	if (Name.Split(TEXT("Level: "), nullptr, &LevelName) && Name.StartsWith(TEXT("Level: ")))
	{
		FCombatLevel Level;
		if (!CombatLevels::Load(LevelName, Level))
		{
			UE_LOG(LogCombat, Error, TEXT("Level '%s' could not be loaded from %s."), *LevelName, *CombatLevels::GetDirectory());
			return false;
		}
		OutSource.Level = Level;
		return true;
	}
	OutSource.Setup = FindSetup(Name);
	return OutSource.Setup != nullptr;
}

const UCombatUnitDefinition* UCombatSubsystem::FindUnitDefinition(const FString& NameOrPath)
{
	if (NameOrPath.Contains(TEXT("/")))
	{
		return LoadObject<UCombatUnitDefinition>(nullptr, *NameOrPath);
	}

	IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();
	AssetRegistry.WaitForCompletion();

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssetsByClass(UCombatUnitDefinition::StaticClass()->GetClassPathName(), Assets);
	for (const FAssetData& Asset : Assets)
	{
		if (Asset.AssetName.ToString().Equals(NameOrPath, ESearchCase::IgnoreCase))
		{
			return Cast<UCombatUnitDefinition>(Asset.GetAsset());
		}
	}
	return nullptr;
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
		FitCameraToShownGrid();
	}
	else
	{
		Grid->ClearLevel();
		RestoreCamera();
	}
}

void UCombatSubsystem::FitCameraToShownGrid()
{
	APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
	ACameraActor* Camera = PlayerController ? Cast<ACameraActor>(PlayerController->GetViewTarget()) : nullptr;
	ACombatGrid* Grid = ACombatGrid::Find(GetWorld());
	if (!Camera || !Grid)
	{
		return;
	}
	if (!OriginalCameraTransform.IsSet() || FittedCamera.Get() != Camera)
	{
		FittedCamera = Camera;
		OriginalCameraTransform = Camera->GetActorTransform();
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
	if (AActor* Camera = FittedCamera.Get(); Camera && OriginalCameraTransform.IsSet())
	{
		Camera->SetActorTransform(OriginalCameraTransform.GetValue());
	}
	OriginalCameraTransform.Reset();
	FittedCamera.Reset();
}

void UCombatSubsystem::EnterDesignMode()
{
	if (bDesignMode)
	{
		return;
	}

	// Start from the level of the fight on screen, if it had one; else from what was edited before, else empty.
	const TOptional<FCombatLevel> LastLevel = Simulation ? CurrentLevel : TOptional<FCombatLevel>();
	StopFight();
	bDesignMode = true;
	DesignSpawnMove.Reset();
	if (LastLevel.IsSet())
	{
		DesignLevel = LastLevel.GetValue();
	}
	else if (DesignLevel.Rows.IsEmpty())
	{
		NewDesignLevel();
		return;
	}
	if (DesignUnitType.IsEmpty())
	{
		const TArray<FString> Types = GetAllUnitDefinitionNames();
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
	if (ACombatGrid* Grid = ACombatGrid::Find(GetWorld()))
	{
		Grid->ClearLevel();
	}
	RestoreCamera();
	++FightSerial;
}

void UCombatSubsystem::NewDesignLevel()
{
	DesignLevel = FCombatLevel::MakeEmpty(TEXT("NewLevel"), 20, 12);
	DesignWave = INDEX_NONE;
	DesignSpawnMove.Reset();
	if (DesignUnitType.IsEmpty())
	{
		const TArray<FString> Types = GetAllUnitDefinitionNames();
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

	const ACombatGrid* Grid = ACombatGrid::Find(GetWorld());
	const FVector Origin = Grid ? Grid->GetActorLocation() : FVector::ZeroVector;
	const FIntPoint Cell(FMath::FloorToInt32((WorldPoint.X - Origin.X) / DesignLevel.CellSize),
		FMath::FloorToInt32((WorldPoint.Y - Origin.Y) / DesignLevel.CellSize));
	if (!DesignLevel.IsInBounds(Cell))
	{
		return;
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
			SetDesignUnit(Move.Y, Unit);
		}
		else if (!bErase && DesignLevel.Waves.IsValidIndex(Move.X) && DesignLevel.Waves[Move.X].Spawns.IsValidIndex(Move.Y))
		{
			FCombatLevelSpawn Spawn = DesignLevel.Waves[Move.X].Spawns[Move.Y];
			Spawn.Cell = Cell;
			SetDesignSpawn(Move.X, Move.Y, Spawn);
		}
		return;
	}

	const int32 UnitIndex = DesignLevel.FindUnitAt(Cell);
	const int32 SpawnIndex = DesignLevel.FindSpawnAt(DesignWave, Cell);
	const TCHAR OldKind = DesignLevel.GetCell(Cell);
	bool bChanged = false;

	if (bErase)
	{
		if (UnitIndex != INDEX_NONE)
		{
			DesignLevel.Units.RemoveAt(UnitIndex);
			bChanged = true;
		}
		if (SpawnIndex != INDEX_NONE)
		{
			DesignLevel.Waves[DesignWave].Spawns.RemoveAt(SpawnIndex);
			bChanged = true;
		}
		if (OldKind != FCombatLevel::Open)
		{
			DesignLevel.SetCell(Cell, FCombatLevel::Open);
			bChanged = true;
		}
	}
	else if (DesignTool == ECombatDesignTool::Unit)
	{
		// One unit per cell, not on walls or water; placing on a unit replaces it.
		const bool bWalkable = !EnumHasAnyFlags(FCombatLevel::FlagsFor(OldKind), ECombatCellFlags::Blocked);
		if (bStroke || !bWalkable || DesignUnitType.IsEmpty())
		{
			return;
		}
		FCombatLevelUnit Unit;
		Unit.Type = DesignUnitType;
		Unit.Team = DesignUnitTeam;
		Unit.Cell = Cell;
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
		// One spawn per cell and wave, not on walls or water; placing on a spawn replaces it. Without waves, wave 1 is made.
		const bool bWalkable = !EnumHasAnyFlags(FCombatLevel::FlagsFor(OldKind), ECombatCellFlags::Blocked);
		if (bStroke || !bWalkable || DesignUnitType.IsEmpty())
		{
			return;
		}
		if (!DesignLevel.Waves.IsValidIndex(DesignWave))
		{
			AddDesignWave();
		}
		FCombatLevelSpawn Spawn;
		Spawn.Type = DesignUnitType;
		Spawn.Cell = Cell;
		Spawn.Time = DesignSpawnTime;
		TArray<FCombatLevelSpawn>& Spawns = DesignLevel.Waves[DesignWave].Spawns;
		if (SpawnIndex != INDEX_NONE)
		{
			Spawns[SpawnIndex] = Spawn;
		}
		else
		{
			Spawns.Add(Spawn);
		}
		bChanged = true;
	}
	else
	{
		const TCHAR Kind = DesignTool == ECombatDesignTool::Wall ? FCombatLevel::Wall
			: DesignTool == ECombatDesignTool::Hedge ? FCombatLevel::Hedge : FCombatLevel::Water;
		if (OldKind == Kind)
		{
			return;
		}
		DesignLevel.SetCell(Cell, Kind);
		// Walls and water cannot hold a unit or a spawn (of any wave).
		if (EnumHasAnyFlags(FCombatLevel::FlagsFor(Kind), ECombatCellFlags::Blocked))
		{
			if (UnitIndex != INDEX_NONE)
			{
				DesignLevel.Units.RemoveAt(UnitIndex);
			}
			DesignLevel.RemoveSpawnsAt(Cell);
		}
		bChanged = true;
	}

	if (bChanged)
	{
		RefreshDesignView(false);
	}
}

void UCombatSubsystem::RefreshDesignView(bool bFitCamera)
{
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
		FitCameraToShownGrid();
	}

	// Preview units: the unit actors, standing on their cells, facing the other side. No simulation.
	DestroyDesignPreviews();
	const UCombatSettings* Settings = GetDefault<UCombatSettings>();
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (int32 Index = 0; Index < DesignLevel.Units.Num(); ++Index)
	{
		const FCombatLevelUnit& Entry = DesignLevel.Units[Index];
		const UCombatUnitDefinition* Definition = FindUnitDefinition(Entry.Type);
		if (!Definition)
		{
			continue;
		}

		const FCombatUnitStats Stats = Definition->ToSimStats(Settings->TickRate);
		const FVector Location = SimToWorld(DesignLevel.CellSize * FVector2D(Entry.Cell.X + 0.5, Entry.Cell.Y + 0.5));
		UClass* ActorClass = Definition->ActorClass ? Definition->ActorClass.Get() : ACombatUnitActor::StaticClass();
		ACombatUnitActor* Actor = GetWorld()->SpawnActor<ACombatUnitActor>(ActorClass, Location, FRotator::ZeroRotator, SpawnParams);
		if (Actor)
		{
			const bool bRanged = Stats.Attacks.ContainsByPredicate([](const FCombatAttackStats& Attack) { return Attack.IsRanged(); });
			Actor->InitUnit(Index, Entry.Team, Stats.Radius, Settings->GetTeamColor(Entry.Team), bRanged);
			Actor->SetHealth(1.f);
			Actor->UpdatePresentation(Location, FVector(Entry.Team == 0 ? 1.0 : -1.0, 0.0, 0.0));
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
		const UCombatUnitDefinition* Definition = FindUnitDefinition(Entry.Type);
		if (!Definition)
		{
			continue;
		}

		const FCombatUnitStats Stats = Definition->ToSimStats(Settings->TickRate);
		const FVector Location = SimToWorld(DesignLevel.CellSize * FVector2D(Entry.Cell.X + 0.5, Entry.Cell.Y + 0.5));
		UClass* ActorClass = Definition->ActorClass ? Definition->ActorClass.Get() : ACombatUnitActor::StaticClass();
		ACombatUnitActor* Actor = GetWorld()->SpawnActor<ACombatUnitActor>(ActorClass, Location, FRotator::ZeroRotator, SpawnParams);
		if (Actor)
		{
			const bool bRanged = Stats.Attacks.ContainsByPredicate([](const FCombatAttackStats& Attack) { return Attack.IsRanged(); });
			Actor->InitUnit(DesignLevel.Units.Num() + Index, WaveTeam, Stats.Radius, Settings->GetTeamColor(WaveTeam), bRanged);
			Actor->SetHealth(1.f);
			Actor->SetStatusEffects({ { FString::Printf(TEXT("%gs"), Entry.Time), FLinearColor::Yellow } });
			Actor->UpdatePresentation(Location, FVector(-1.0, 0.0, 0.0));
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

	const int32 Occupant = DesignLevel.FindSpawnAt(WaveIndex, Spawn.Cell);
	const bool bValidCell = DesignLevel.IsInBounds(Spawn.Cell)
		&& !EnumHasAnyFlags(FCombatLevel::FlagsFor(DesignLevel.GetCell(Spawn.Cell)), ECombatCellFlags::Blocked)
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
		|| DesignLevel.FindSpawnAt(NewWaveIndex, DesignLevel.Waves[WaveIndex].Spawns[SpawnIndex].Cell) != INDEX_NONE)
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

	const int32 Occupant = DesignLevel.FindUnitAt(Unit.Cell);
	const bool bValidCell = DesignLevel.IsInBounds(Unit.Cell)
		&& !EnumHasAnyFlags(FCombatLevel::FlagsFor(DesignLevel.GetCell(Unit.Cell)), ECombatCellFlags::Blocked)
		&& (Occupant == INDEX_NONE || Occupant == UnitIndex);
	if (!bValidCell || Unit.Type.IsEmpty())
	{
		// Refused: the UI rebuilds and shows the old values again.
		++DesignRevision;
		return false;
	}

	DesignLevel.Units[UnitIndex] = Unit;
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

TArray<FString> UCombatSubsystem::GetAllUnitDefinitionNames()
{
	IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();
	AssetRegistry.WaitForCompletion();

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssetsByClass(UCombatUnitDefinition::StaticClass()->GetClassPathName(), Assets);
	TArray<FString> Names;
	for (const FAssetData& Asset : Assets)
	{
		Names.Add(Asset.AssetName.ToString());
	}
	Names.Sort();
	return Names;
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

UCombatSetup* UCombatSubsystem::FindSetup(const FString& NameOrPath)
{
	if (NameOrPath.IsEmpty())
	{
		return GetDefault<UCombatSettings>()->DefaultSetup.LoadSynchronous();
	}

	if (NameOrPath.Contains(TEXT("/")))
	{
		return LoadObject<UCombatSetup>(nullptr, *NameOrPath);
	}

	IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();
	AssetRegistry.WaitForCompletion();

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssetsByClass(UCombatSetup::StaticClass()->GetClassPathName(), Assets);
	for (const FAssetData& Asset : Assets)
	{
		if (Asset.AssetName.ToString().Equals(NameOrPath, ESearchCase::IgnoreCase))
		{
			return Cast<UCombatSetup>(Asset.GetAsset());
		}
	}
	return nullptr;
}

TArray<FString> UCombatSubsystem::GetAllSetupNames()
{
	IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();
	AssetRegistry.WaitForCompletion();

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssetsByClass(UCombatSetup::StaticClass()->GetClassPathName(), Assets);

	TArray<FString> Names;
	for (const FAssetData& Asset : Assets)
	{
		Names.Add(Asset.AssetName.ToString());
	}
	Names.Sort();
	return Names;
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

	/** Removes a "level=<name>" argument and loads that level into OutSource. False if it is not found. */
	static bool ExtractLevel(TArray<FString>& Args, FCombatFightSource& OutSource)
	{
		for (int32 Index = 0; Index < Args.Num(); ++Index)
		{
			if (Args[Index].StartsWith(TEXT("level="), ESearchCase::IgnoreCase))
			{
				const FString Name = Args[Index].RightChop(6);
				Args.RemoveAt(Index);
				return UCombatSubsystem::ResolveSource(TEXT("Level: ") + Name, OutSource);
			}
		}
		return true;
	}

	/** Parses "<seed> [setup]". */
	static bool ParseArgs(const TArray<FString>& Args, int32& OutSeed, UCombatSetup*& OutSetup)
	{
		OutSeed = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : GetDefault<UCombatSettings>()->DefaultSeed;
		const FString SetupArg = Args.Num() > 1 ? Args[1] : FString();

		OutSetup = UCombatSubsystem::FindSetup(SetupArg);
		if (!OutSetup)
		{
			UE_LOG(LogCombat, Error, TEXT("Setup '%s' not found."), SetupArg.IsEmpty() ? TEXT("(default from Combat settings)") : *SetupArg);
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
		int32 Seed = GetDefault<UCombatSettings>()->DefaultSeed;
		if (Source.Level.IsSet())
		{
			Seed = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : Seed;
		}
		else
		{
			UCombatSetup* Setup;
			if (!ParseArgs(Args, Seed, Setup))
			{
				return;
			}
			Source.Setup = Setup;
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

		int32 Seed = GetDefault<UCombatSettings>()->DefaultSeed;
		if (Source.Level.IsSet())
		{
			Seed = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : Seed;
		}
		else
		{
			UCombatSetup* Setup;
			if (!ParseArgs(Args, Seed, Setup))
			{
				return;
			}
			Source.Setup = Setup;
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

	/** Combat.Batch <count> [setup] [startseed] [csv] [script=<name>] */
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

		// With level=<name> there is no setup argument: <count> [startseed].
		const int32 Count = Positional.Num() > 0 ? FCString::Atoi(*Positional[0]) : 100;
		const int32 SeedArg = Source.Level.IsSet() ? 1 : 2;
		const int32 StartSeed = Positional.Num() > SeedArg ? FCString::Atoi(*Positional[SeedArg]) : 1;
		if (!Source.Level.IsSet())
		{
			Source.Setup = UCombatSubsystem::FindSetup(Positional.Num() > 1 ? Positional[1] : FString());
		}
		if (!Source.IsValid())
		{
			UE_LOG(LogCombat, Error, TEXT("Combat.Batch: setup not found."));
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

		const FString SlotName = Args[1].StartsWith(TEXT("Slot.")) ? Args[1] : TEXT("Slot.") + Args[1];
		const FGameplayTag SlotTag = FGameplayTag::RequestGameplayTag(FName(*SlotName), false);
		if (!SlotTag.IsValid())
		{
			UE_LOG(LogCombat, Error, TEXT("Unknown slot tag %s."), *SlotName);
			return;
		}

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

		if (!Actor->SetSlotMesh(SlotTag, Mesh))
		{
			UE_LOG(LogCombat, Warning, TEXT("Slot %s of unit %s cannot change (no look, or a merged slot)."), *SlotName, *Args[0]);
		}
	}

	static FAutoConsoleCommandWithWorldAndArgs SetSlotCommand(
		TEXT("Combat.SetSlot"),
		TEXT("Combat.SetSlot <unit> <slot> [mesh]: presentation debug, puts a skeletal mesh (asset name or path; none = empty) in a swappable slot (Hat or Slot.Hat) of the unit's look."),
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
		TEXT("Combat.Batch <count> [setup] [startseed] [csv] [level=<name>] [script=<name>]: runs fights headless (from a setup or a level, optionally all with the same command script) and reports win rates, durations and per unit type statistics. With level=, the start seed follows the count."),
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
		TEXT("Combat.Start <seed> [setup] [level=<name>]: starts a fight with presentation in the current level, from a setup or a LevelDesigner level."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Start));

	static FAutoConsoleCommandWithWorldAndArgs SimulateCommand(
		TEXT("Combat.Simulate"),
		TEXT("Combat.Simulate <seed> [setup] [level=<name>] [script=<name>]: runs a fight headless (from a setup or a level, optionally with a command script) and prints the outcome, duration and final checksum."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Simulate));

	static FAutoConsoleCommandWithWorldAndArgs StopCommand(
		TEXT("Combat.Stop"),
		TEXT("Combat.Stop: stops the running fight and removes its units."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Stop));
}
