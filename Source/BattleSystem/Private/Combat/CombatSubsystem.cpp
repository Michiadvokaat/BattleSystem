// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatSubsystem.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Combat/CombatBatch.h"
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
#include "Engine/World.h"
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

bool UCombatSubsystem::StartFightWithSettings(int32 Seed, const UCombatSetup* Setup, const FCombatSimSettings& SimSettings)
{
	StopFight();
	bIsReplay = false;
	ReplayVerdict.Reset();

	if (!Setup)
	{
		UE_LOG(LogCombat, Error, TEXT("StartFight: no setup."));
		return false;
	}

	FCombatSimConfig Config;
	TArray<const UCombatUnitDefinition*> Definitions;
	if (!BuildSimConfig(GetWorld(), Seed, *Setup, SimSettings, Config, GridOrigin, &Definitions))
	{
		return false;
	}

	Simulation = MakeUnique<FCombatSimulation>(Config);
	CueTable = GetDefault<UCombatSettings>()->CueTable.LoadSynchronous();
	MaxStepsPerFrame = FMath::Max(GetDefault<UCombatSettings>()->MaxStepsPerFrame, 1);
	Accumulator = 0.0;
	bPaused = false;
	CurrentSeed = Seed;
	CurrentSetupName = Setup->GetName();
	CurrentSetupPath = Setup->GetPathName();
	CurrentSettings = SimSettings;

	const UCombatSettings* Settings = GetDefault<UCombatSettings>();
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (const FCombatUnit& Unit : Simulation->GetUnits())
	{
		const UCombatUnitDefinition* Definition = Definitions[Unit.Id];
		UClass* ActorClass = Definition->ActorClass ? Definition->ActorClass.Get() : ACombatUnitActor::StaticClass();

		ACombatUnitActor* Actor = GetWorld()->SpawnActor<ACombatUnitActor>(ActorClass, SimToWorld(Unit.Position), FRotator::ZeroRotator, SpawnParams);
		if (Actor)
		{
			const bool bRanged = Unit.Stats.Attacks.ContainsByPredicate([](const FCombatAttackStats& Attack) { return Attack.IsRanged(); });
			Actor->InitUnit(Unit.Id, Unit.Team, Unit.Stats.Radius, Settings->GetTeamColor(Unit.Team), bRanged);
		}
		UnitActors.Add(Actor);
		UnitDefinitions.Add(const_cast<UCombatUnitDefinition*>(Definition));
	}

	UE_LOG(LogCombat, Display, TEXT("Fight started: seed %d, setup %s, %d units."), Seed, *Setup->GetName(), UnitActors.Num());
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
	Replay.Seed = CurrentSeed;
	Replay.Settings = CurrentSettings;
	Replay.Ticks = Simulation->GetTick();
	Replay.Outcome = FCombatSimulation::OutcomeToString(Simulation->GetOutcome());
	Replay.WinningTeam = Simulation->GetWinningTeam();
	Replay.FinalChecksum = CombatReplay::ChecksumToString(Simulation->GetChecksum());

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

	UCombatSetup* Setup = LoadObject<UCombatSetup>(nullptr, *Replay.SetupPath);
	if (!Setup || !StartFightWithSettings(Replay.Seed, Setup, Replay.Settings))
	{
		OutMessage = FString::Printf(TEXT("Could not start the replay (setup %s)."), *Replay.SetupPath);
		UE_LOG(LogCombat, Error, TEXT("%s"), *OutMessage);
		return false;
	}

	bIsReplay = true;
	PlayingReplay = Replay;

	TArray<FString> Warnings;
	if (Replay.BuildVersion != FApp::GetBuildVersion())
	{
		Warnings.Add(FString::Printf(TEXT("other build (%s)"), *Replay.BuildVersion));
	}
	if (Replay.MapName != UWorld::RemovePIEPrefix(GetWorld()->GetMapName()))
	{
		Warnings.Add(FString::Printf(TEXT("other map (%s)"), *Replay.MapName));
	}
	if (Replay.GridChecksum != CombatReplay::ChecksumToString(Simulation->GetGrid().ComputeChecksum()))
	{
		Warnings.Add(TEXT("the arena grid changed"));
	}

	OutMessage = FString::Printf(TEXT("Playing replay %s, seed %d"), *Setup->GetName(), Replay.Seed);
	if (!Warnings.IsEmpty())
	{
		OutMessage += TEXT(" - warning: ") + FString::Join(Warnings, TEXT(", "));
	}
	UE_LOG(LogCombat, Display, TEXT("%s"), *OutMessage);
	return true;
}

bool UCombatSubsystem::RunBatch(const UCombatSetup* Setup, int32 Count, int32 StartSeed, bool bWriteCsv, FString& OutSummary)
{
	if (!Setup)
	{
		OutSummary = TEXT("No setup for the batch.");
		return false;
	}
	const bool bOk = RunBatchInWorld(GetWorld(), *Setup, Count, StartSeed, GetCurrentSimSettings(), bWriteCsv, OutSummary);
	LastBatchSummary = OutSummary;
	return bOk;
}

bool UCombatSubsystem::RunBatchInWorld(UWorld* World, const UCombatSetup& Setup, int32 Count, int32 StartSeed,
	const FCombatSimSettings& Settings, bool bWriteCsv, FString& OutSummary)
{
	FCombatSimConfig Config;
	FVector GridOrigin;
	TArray<const UCombatUnitDefinition*> Definitions;
	if (!BuildSimConfig(World, StartSeed, Setup, Settings, Config, GridOrigin, &Definitions))
	{
		OutSummary = FString::Printf(TEXT("Could not build setup %s."), *Setup.GetName());
		return false;
	}

	TArray<FString> UnitTypeNames;
	for (const UCombatUnitDefinition* Definition : Definitions)
	{
		UnitTypeNames.Add(Definition->GetName());
	}

	const FCombatBatchResult Result = CombatBatch::Run(Config, UnitTypeNames, FMath::Max(Count, 1), StartSeed);
	OutSummary = Result.ToSummary(Setup.GetName(), Settings.TickRate);

	if (bWriteCsv)
	{
		const FString BasePath = FPaths::ProjectSavedDir() / TEXT("CombatBatch")
			/ FString::Printf(TEXT("%s_%s"), *Setup.GetName(), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
		OutSummary += Result.WriteCsv(BasePath, Settings.TickRate)
			? FString::Printf(TEXT("  CSV: %s_fights.csv / _units.csv\n"), *FPaths::GetCleanFilename(BasePath))
			: TEXT("  CSV could not be written.\n");
	}

	UE_LOG(LogCombat, Display, TEXT("%s"), *OutSummary);
	return true;
}

void UCombatSubsystem::StopFight()
{
	for (ACombatUnitActor* Actor : UnitActors)
	{
		if (IsValid(Actor))
		{
			Actor->Destroy();
		}
	}
	UnitActors.Reset();
	UnitDefinitions.Reset();

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
				if (!Units[Event.SourceId].Stats.Attacks[Event.AttackIndex].IsArea())
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

void UCombatSubsystem::SpawnProjectileActor(const FCombatEvent& Event)
{
	// The projectile actor class comes from the attack definition the simulation attack was made from.
	UClass* ActorClass = ACombatProjectileActor::StaticClass();
	const FCombatUnit& Source = Simulation->GetUnits()[Event.SourceId];
	const int32 SourceIndex = Source.Stats.Attacks[Event.AttackIndex].SourceIndex;
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
		Actor->SetStatusEffects(StatusIcons);
		Actor->UpdatePresentation(SimToWorld(Position), FVector(Facing.X, Facing.Y, 0.0));
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
	const FCombatAttackStats& Attack = Simulation->GetUnits()[Event.SourceId].Stats.Attacks[Event.AttackIndex];

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
		const bool bIdentical = Checksum == PlayingReplay.FinalChecksum && Simulation->GetTick() == PlayingReplay.Ticks;
		ReplayVerdict = bIdentical
			? FString::Printf(TEXT("Replay identical (checksum %s)"), *Checksum)
			: FString::Printf(TEXT("Replay DIFFERENT: recorded %s after %d ticks, now %s after %d ticks"),
				*PlayingReplay.FinalChecksum, PlayingReplay.Ticks, *Checksum, Simulation->GetTick());
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

	static void Start(const TArray<FString>& Args, UWorld* World)
	{
		UCombatSubsystem* Subsystem = World ? World->GetSubsystem<UCombatSubsystem>() : nullptr;
		if (!Subsystem)
		{
			UE_LOG(LogCombat, Error, TEXT("Combat.Start needs a running game world."));
			return;
		}

		int32 Seed;
		UCombatSetup* Setup;
		if (ParseArgs(Args, Seed, Setup))
		{
			Subsystem->StartFight(Seed, Setup);
		}
	}

	static void Simulate(const TArray<FString>& Args, UWorld* World)
	{
		int32 Seed;
		UCombatSetup* Setup;
		if (!ParseArgs(Args, Seed, Setup))
		{
			return;
		}

		FCombatSimConfig Config;
		FVector GridOrigin;
		if (!UCombatSubsystem::BuildSimConfig(World, Seed, *Setup, GetSimSettings(World), Config, GridOrigin))
		{
			return;
		}

		const double StartTime = FPlatformTime::Seconds();
		FCombatSimulation Simulation(Config);
		Simulation.RunToEnd();
		const double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;

		FString Result = FCombatSimulation::OutcomeToString(Simulation.GetOutcome());
		if (Simulation.GetOutcome() == ECombatOutcome::TeamWon)
		{
			Result += FString::Printf(TEXT(" (team %d)"), Simulation.GetWinningTeam());
		}
		UE_LOG(LogCombat, Display, TEXT("Combat.Simulate seed %d, setup %s: %s after %d ticks (%.1f s), checksum 0x%08X, %.2f ms."),
			Seed, *Setup->GetName(), *Result, Simulation.GetTick(), Simulation.GetTick() * Simulation.GetFixedDt(),
			Simulation.GetChecksum(), ElapsedMs);
	}

	static void Stop(const TArray<FString>& Args, UWorld* World)
	{
		if (UCombatSubsystem* Subsystem = World ? World->GetSubsystem<UCombatSubsystem>() : nullptr)
		{
			Subsystem->StopFight();
		}
	}

	/** Combat.Batch <count> [setup] [startseed] [csv] */
	static void Batch(const TArray<FString>& Args, UWorld* World)
	{
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
		UCombatSetup* Setup = UCombatSubsystem::FindSetup(Positional.Num() > 1 ? Positional[1] : FString());
		const int32 StartSeed = Positional.Num() > 2 ? FCString::Atoi(*Positional[2]) : 1;
		if (!Setup)
		{
			UE_LOG(LogCombat, Error, TEXT("Combat.Batch: setup not found."));
			return;
		}

		FString Summary;
		UCombatSubsystem::RunBatchInWorld(World, *Setup, Count, StartSeed, GetSimSettings(World), bCsv, Summary);
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

	static FAutoConsoleCommandWithWorldAndArgs BatchCommand(
		TEXT("Combat.Batch"),
		TEXT("Combat.Batch <count> [setup] [startseed] [csv]: runs fights headless and reports win rates, durations and per unit type statistics."),
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
		TEXT("Combat.Start <seed> [setup]: starts a fight with presentation in the current level."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Start));

	static FAutoConsoleCommandWithWorldAndArgs SimulateCommand(
		TEXT("Combat.Simulate"),
		TEXT("Combat.Simulate <seed> [setup]: runs a fight headless and prints the outcome, duration and final checksum."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Simulate));

	static FAutoConsoleCommandWithWorldAndArgs StopCommand(
		TEXT("Combat.Stop"),
		TEXT("Combat.Stop: stops the running fight and removes its units."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Stop));
}
