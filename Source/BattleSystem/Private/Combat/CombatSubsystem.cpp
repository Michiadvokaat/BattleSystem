// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatSubsystem.h"
#include "AssetRegistry/IAssetRegistry.h"
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
	StopFight();

	if (!Setup)
	{
		UE_LOG(LogCombat, Error, TEXT("StartFight: no setup."));
		return false;
	}

	FCombatSimConfig Config;
	TArray<const UCombatUnitDefinition*> Definitions;
	if (!BuildSimConfig(GetWorld(), Seed, *Setup, Config, GridOrigin, &Definitions))
	{
		return false;
	}

	if (TauntRangeOverride > 0.f)
	{
		for (FCombatUnitSpawn& Spawn : Config.Units)
		{
			for (FCombatAttackStats& Attack : Spawn.Stats.Attacks)
			{
				if (Attack.Type.MatchesTagExact(CombatTags::Attack_Taunt))
				{
					Attack.Range = TauntRangeOverride;
				}
			}
		}
	}

	Simulation = MakeUnique<FCombatSimulation>(Config);
	MaxStepsPerFrame = FMath::Max(GetDefault<UCombatSettings>()->MaxStepsPerFrame, 1);
	Accumulator = 0.0;
	bPaused = false;
	CurrentSeed = Seed;
	CurrentSetupName = Setup->GetName();

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
			}
			break;
		case ECombatEventType::Death:
			if (ACombatUnitActor* Actor = UnitActors[Event.TargetId])
			{
				Actor->OnDeath();
			}
			break;
		case ECombatEventType::AreaAttackFired:
			if (ACombatUnitActor* Actor = UnitActors[Event.SourceId])
			{
				Actor->OnAreaAttack(Event.Amount);
			}
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

		Actor->SetTaunted(Unit.Effects.HasGrantedTag(CombatTags::Status_Taunted));
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
			if (Attack.bAreaAroundSelf)
			{
				// Reach from the center: an enemy is hit when its edge is inside this circle.
				DrawDebugCircle(World, Center, Attack.Range + Unit.Stats.Radius, 64, RangeColor, false, -1.f, 0, 2.f,
					FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), false);
			}
		}
	}
}

void UCombatSubsystem::ReportResult() const
{
	FString Result = FCombatSimulation::OutcomeToString(Simulation->GetOutcome());
	if (Simulation->GetOutcome() == ECombatOutcome::TeamWon)
	{
		Result += FString::Printf(TEXT(" (team %d)"), Simulation->GetWinningTeam());
	}

	const FString Message = FString::Printf(TEXT("Fight over: %s after %d ticks, checksum 0x%08X"),
		*Result, Simulation->GetTick(), Simulation->GetChecksum());
	UE_LOG(LogCombat, Display, TEXT("%s"), *Message);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(INDEX_NONE, 10.f, FColor::Green, Message);
	}
}

bool UCombatSubsystem::BuildSimConfig(UWorld* World, int32 Seed, const UCombatSetup& Setup, FCombatSimConfig& OutConfig,
	FVector& OutGridOrigin, TArray<const UCombatUnitDefinition*>* OutDefinitions)
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
	OutConfig.TickRate = Settings->TickRate;
	OutConfig.MaxTicks = Settings->SecondsToTicks(Settings->FightTimeLimit);
	OutConfig.MaxFirstAttackDelayTicks = Settings->SecondsToTicks(Settings->MaxFirstAttackDelay);
	OutConfig.RetargetIntervalTicks = FMath::Max(Settings->SecondsToTicks(Settings->RetargetInterval), 1);
	OutConfig.PathLookaheadCells = Settings->PathLookaheadCells;
	OutConfig.SeparationStrength = Settings->SeparationStrength;

	if (Settings->ThreatDecayMode == ECombatThreatDecayMode::HalfLife)
	{
		OutConfig.ThreatDecayFactorPerTick = FMath::Pow(0.5f, 1.f / (FMath::Max(Settings->ThreatHalfLife, 0.1f) * OutConfig.TickRate));
		OutConfig.ThreatDecayAmountPerTick = 0.f;
	}
	else
	{
		OutConfig.ThreatDecayFactorPerTick = 1.f;
		OutConfig.ThreatDecayAmountPerTick = Settings->ThreatDecayPerSecond / OutConfig.TickRate;
	}
	OutConfig.ThreatThreshold = Settings->ThreatThreshold;
	OutConfig.ThreatSwitchRatio = Settings->ThreatSwitchRatio;
	OutConfig.RetargetDistanceMargin = Settings->RetargetDistanceMargin;

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
		if (!UCombatSubsystem::BuildSimConfig(World, Seed, *Setup, Config, GridOrigin))
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
