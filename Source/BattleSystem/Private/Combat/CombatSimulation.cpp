// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatSimulation.h"
#include "Combat/CombatPathfinding.h"
#include "Combat/CombatTags.h"
#include "Misc/Crc.h"
#include "Algo/StableSort.h"

bool FCombatArea::Contains(const FVector2D& Position, float UnitRadius) const
{
	const double Distance = FVector2D::Distance(Center, Position);
	switch (Shape)
	{
	case ECombatAreaShape::CircleAroundSelf:
		return Distance - SourceRadius - UnitRadius <= Radius;
	case ECombatAreaShape::CircleAtTarget:
		return Distance - UnitRadius <= Radius;
	case ECombatAreaShape::Cone:
		if (Distance - SourceRadius - UnitRadius > Radius)
		{
			return false;
		}
		// Angle to the unit's center; a unit on top of the attacker counts as inside.
		return Distance <= UE_KINDA_SMALL_NUMBER || FVector2D::DotProduct(Direction, (Position - Center) / Distance) >= ConeCosHalfAngle;
	case ECombatAreaShape::None:
		break;
	}
	return false;
}

float FCombatUnit::GetThreatOn(int32 EnemyId) const
{
	const FCombatThreatEntry* Entry = Threat.FindByPredicate([EnemyId](const FCombatThreatEntry& Candidate) { return Candidate.EnemyId == EnemyId; });
	return Entry ? Entry->Threat : 0.f;
}

FCombatSimulation::FCombatSimulation(const FCombatSimConfig& InConfig)
	: Config(InConfig)
	, FixedDt(1.f / FMath::Max(InConfig.TickRate, 1))
	, Random(InConfig.Seed)
{
	Config.MaxTicks = FMath::Max(Config.MaxTicks, 1);

	Units.Reserve(Config.Units.Num());
	for (int32 Index = 0; Index < Config.Units.Num(); ++Index)
	{
		const FCombatUnitSpawn& Spawn = Config.Units[Index];
		AddUnit(Spawn.Stats, Spawn.Team, Spawn.StartCell, Index);
	}

	// Waves: the wave team needs a distance map even when it has no units at the start.
	if (!Config.Waves.IsEmpty())
	{
		int32 SourceIndex = Config.Units.Num();
		for (const FCombatWave& Wave : Config.Waves)
		{
			WaveFirstSourceIndex.Add(SourceIndex);
			SourceIndex += Wave.Spawns.Num();
		}
		TeamIds.AddUnique(Config.WaveTeam);
		NextWaveTick = FMath::Max(Config.WavePauseTicks, 1);
	}
	DistanceMaps.SetNum(TeamIds.Num());

	// Commands known in advance (script, replay): by tick, keeping the given order within a tick.
	TArray<FCombatCommand> Known = Config.Commands;
	Algo::StableSortBy(Known, [](const FCombatCommand& Command) { return FMath::Max(Command.Tick, 1); });
	for (FCombatCommand Command : Known)
	{
		Command.Tick = FMath::Max(Command.Tick, 1);
		QueueCommand(Command);
	}

	Checksum = ComputeChecksum();
}

FCombatUnit& FCombatSimulation::AddUnit(const FCombatUnitStats& Stats, int32 Team, const FIntPoint& Cell, int32 SourceIndex)
{
	FCombatUnit& Unit = Units.AddDefaulted_GetRef();
	Unit.Id = Units.Num() - 1;
	Unit.Team = Team;
	Unit.SourceIndex = SourceIndex;
	Unit.Stats = Stats;
	for (FCombatAttackStats& Attack : Unit.Stats.Attacks)
	{
		Attack.CooldownTicks = FMath::Max(Attack.CooldownTicks, 1);
		Attack.WindupTicks = FMath::Max(Attack.WindupTicks, 0);
	}
	Unit.AttackCooldowns.Init(0, Unit.Stats.Attacks.Num());
	Unit.Position = Config.Grid.CellToLocal(Cell);
	Unit.PreviousPosition = Unit.Position;
	Unit.HP = Unit.Stats.MaxHP;
	Unit.bAlive = Unit.HP > 0.f;
	Unit.FirstAttackDelayTicks = Random.RandRange(0, FMath::Max(Config.MaxFirstAttackDelayTicks, 0));
	Unit.SteerPoint = Unit.Position;

	TeamIds.AddUnique(Unit.Team);
	return Unit;
}

void FCombatSimulation::StartWave()
{
	const int32 WaveIndex = WavesStarted++;
	NextWaveTick = INDEX_NONE;

	const TArray<FCombatWaveSpawn>& Spawns = Config.Waves[WaveIndex].Spawns;
	for (int32 SpawnIndex = 0; SpawnIndex < Spawns.Num(); ++SpawnIndex)
	{
		PendingSpawns.Add({ Tick + FMath::Max(Spawns[SpawnIndex].DelayTicks, 0), WaveIndex, SpawnIndex });
	}

	FCombatEvent& Event = Events.Add_GetRef({ ECombatEventType::WaveStarted, INDEX_NONE, INDEX_NONE, 0.f });
	Event.WaveIndex = WaveIndex;
}

void FCombatSimulation::UpdateWaves()
{
	if (NextWaveTick != INDEX_NONE && Tick >= NextWaveTick && WavesStarted < Config.Waves.Num())
	{
		StartWave();
	}

	// Due spawns in the order the waves started, each wave's in its own order: that order gives the unit IDs.
	bool bSpawned = false;
	for (const FPendingSpawn& Pending : PendingSpawns)
	{
		if (Pending.Tick > Tick)
		{
			continue;
		}
		const FCombatWaveSpawn& Spawn = Config.Waves[Pending.WaveIndex].Spawns[Pending.SpawnIndex];
		const FCombatUnit& Unit = AddUnit(Spawn.Stats, Config.WaveTeam, Spawn.Cell, WaveFirstSourceIndex[Pending.WaveIndex] + Pending.SpawnIndex);
		FCombatEvent& Event = Events.Add_GetRef({ ECombatEventType::UnitSpawned, Unit.Id, INDEX_NONE, 0.f });
		Event.WaveIndex = Pending.WaveIndex;
		bSpawned = true;
	}
	if (bSpawned)
	{
		PendingSpawns.RemoveAll([this](const FPendingSpawn& Pending) { return Pending.Tick <= Tick; });
		// New units are targets (and targeters) in this very step.
		DistanceMaps.SetNum(TeamIds.Num());
		RebuildDistanceMaps();
	}
}

void FCombatSimulation::UpdateWaveClear()
{
	if (NextWaveTick != INDEX_NONE || WavesStarted >= Config.Waves.Num() || !PendingSpawns.IsEmpty())
	{
		return;
	}
	const bool bEnemyAlive = Units.ContainsByPredicate([this](const FCombatUnit& Unit) { return Unit.bAlive && Unit.Team == Config.WaveTeam; });
	if (!bEnemyAlive)
	{
		NextWaveTick = Tick + FMath::Max(Config.WavePauseTicks, 1);
	}
}

void FCombatSimulation::Step()
{
	if (IsFinished())
	{
		return;
	}

	++Tick;
	Events.Reset();
	PendingHits.Reset();

	// Decisions use the positions at the start of the step, so the processing order cannot matter.
	for (FCombatUnit& Unit : Units)
	{
		Unit.PreviousPosition = Unit.Position;
	}
	for (FCombatProjectile& Projectile : Projectiles)
	{
		Projectile.PreviousPosition = Projectile.Position;
	}

	for (FCombatUnit& Unit : Units)
	{
		if (Unit.bAlive)
		{
			Unit.Effects.Tick();
			DecayThreat(Unit);
		}
	}

	if (bDistanceMapsDirty || (Tick - 1) % FMath::Max(Config.RetargetIntervalTicks, 1) == 0)
	{
		RebuildDistanceMaps();
	}

	// Before the units: areas placed this step only start counting down next step.
	UpdatePendingAreas();

	// Player commands run at the start of their tick, after the areas so a telegraphed ability counts down from next step.
	ExecuteDueCommands();

	// After the commands, so units of a called wave with no delay appear in the same step.
	if (!Config.Waves.IsEmpty())
	{
		UpdateWaves();
	}

	for (FCombatUnit& Unit : Units)
	{
		if (Unit.bAlive)
		{
			UpdateUnit(Unit);
		}
	}

	// After the units, so projectiles fired this step move right away and home in on the new positions.
	UpdateProjectiles();
	ApplyPendingHits();
	if (!Config.Waves.IsEmpty())
	{
		UpdateWaveClear();
	}
	UpdateOutcome();
	Checksum = ComputeChecksum();
}

void FCombatSimulation::RunToEnd()
{
	while (!IsFinished())
	{
		Step();
	}
}

int32 FCombatSimulation::FindNearestEnemy(const FCombatUnit& Unit) const
{
	int32 BestId = INDEX_NONE;
	double BestDistSq = TNumericLimits<double>::Max();

	// ID order with a strict '<' makes the lowest ID win ties.
	for (const FCombatUnit& Other : Units)
	{
		if (!Other.bAlive || Other.Team == Unit.Team)
		{
			continue;
		}

		const double DistSq = FVector2D::DistSquared(Unit.PreviousPosition, Other.PreviousPosition);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestId = Other.Id;
		}
	}
	return BestId;
}

const FCombatDistanceMap* FCombatSimulation::GetDistanceMap(int32 Team) const
{
	const int32 Index = TeamIds.IndexOfByKey(Team);
	return Index != INDEX_NONE ? &DistanceMaps[Index] : nullptr;
}

void FCombatSimulation::RebuildDistanceMaps()
{
	TArray<FCombatDistanceMap::FSource> Sources;
	for (int32 TeamIndex = 0; TeamIndex < TeamIds.Num(); ++TeamIndex)
	{
		Sources.Reset();
		for (const FCombatUnit& Other : Units)
		{
			if (Other.bAlive && Other.Team != TeamIds[TeamIndex])
			{
				Sources.Add({ Config.Grid.LocalToCell(Other.PreviousPosition), Other.Id });
			}
		}
		DistanceMaps[TeamIndex].Build(Config.Grid, Sources);
	}
	bDistanceMapsDirty = false;
}

void FCombatSimulation::UpdateTarget(FCombatUnit& Unit)
{
	// A taunt overrides everything, right away.
	const int32 TaunterId = Unit.Effects.FindSourceOfTag(CombatTags::Status_Taunted);
	if (TaunterId != INDEX_NONE && Units[TaunterId].bAlive && Units[TaunterId].Team != Unit.Team)
	{
		Unit.TargetId = TaunterId;
		Unit.TargetReason = ECombatTargetReason::Taunt;
		return;
	}

	const bool bHasTarget = Unit.TargetId != INDEX_NONE && Units[Unit.TargetId].bAlive;
	const bool bRetargetTick = (Tick - 1) % FMath::Max(Config.RetargetIntervalTicks, 1) == 0;
	if (bRetargetTick || !bHasTarget || Unit.TargetReason == ECombatTargetReason::Taunt)
	{
		ChooseTarget(Unit);
	}
}

void FCombatSimulation::ChooseTarget(FCombatUnit& Unit) const
{
	const int32 CurrentId = Unit.TargetId != INDEX_NONE && Units[Unit.TargetId].bAlive ? Unit.TargetId : INDEX_NONE;

	// 1. The most threat above the threshold. Stay on the current target unless another has clearly more.
	int32 BestThreatId = INDEX_NONE;
	float BestThreat = 0.f;
	for (const FCombatThreatEntry& Entry : Unit.Threat)
	{
		if (Units[Entry.EnemyId].bAlive
			&& (BestThreatId == INDEX_NONE || Entry.Threat > BestThreat || (Entry.Threat == BestThreat && Entry.EnemyId < BestThreatId)))
		{
			BestThreatId = Entry.EnemyId;
			BestThreat = Entry.Threat;
		}
	}
	if (BestThreatId != INDEX_NONE && BestThreat >= Config.ThreatThreshold)
	{
		const float CurrentThreat = CurrentId != INDEX_NONE ? Unit.GetThreatOn(CurrentId) : 0.f;
		const bool bKeep = CurrentThreat >= Config.ThreatThreshold && BestThreat < CurrentThreat * Config.ThreatSwitchRatio;
		Unit.TargetId = bKeep ? CurrentId : BestThreatId;
		Unit.TargetReason = ECombatTargetReason::Threat;
		return;
	}

	// 2. Ranged: an enemy it can shoot right now. Stay on the current target while it can still be shot.
	const int32 VisibleId = FindVisibleEnemyInRange(Unit);
	if (VisibleId != INDEX_NONE)
	{
		const bool bKeep = CurrentId != INDEX_NONE && CanShootNow(Unit, Units[CurrentId]);
		Unit.TargetId = bKeep ? CurrentId : VisibleId;
		Unit.TargetReason = ECombatTargetReason::Visible;
		return;
	}

	// 3. Nearest by walking. Stay on the current target unless the new one is clearly closer.
	const int32 NearestId = FindNearestByWalking(Unit);
	bool bKeep = false;
	if (NearestId != INDEX_NONE && CurrentId != INDEX_NONE && CurrentId != NearestId)
	{
		const double CurrentDistance = FVector2D::Distance(Unit.PreviousPosition, Units[CurrentId].PreviousPosition);
		const double NearestDistance = FVector2D::Distance(Unit.PreviousPosition, Units[NearestId].PreviousPosition);
		bKeep = CurrentDistance <= NearestDistance + Config.RetargetDistanceMargin;
	}
	Unit.TargetId = bKeep ? CurrentId : NearestId;
	Unit.TargetReason = Unit.TargetId != INDEX_NONE ? ECombatTargetReason::Nearest : ECombatTargetReason::None;
}

int32 FCombatSimulation::FindNearestByWalking(const FCombatUnit& Unit) const
{
	// Nearest enemy by walking distance; as the crow flies if no enemy is reachable.
	if (const FCombatDistanceMap* Map = GetDistanceMap(Unit.Team))
	{
		const int32 Id = Map->GetNearestId(Config.Grid.LocalToCell(Unit.PreviousPosition));
		if (Id != INDEX_NONE && Units[Id].bAlive)
		{
			return Id;
		}
	}
	return FindNearestEnemy(Unit);
}

bool FCombatSimulation::CanShootNow(const FCombatUnit& Unit, const FCombatUnit& Other) const
{
	if (!Other.bAlive || Other.Team == Unit.Team)
	{
		return false;
	}

	const FCombatAttackStats* Ranged = nullptr;
	for (const FCombatAttackStats& Attack : Unit.Stats.Attacks)
	{
		if (Attack.IsRanged() && (!Ranged || Attack.Range > Ranged->Range))
		{
			Ranged = &Attack;
		}
	}
	if (!Ranged)
	{
		return false;
	}

	const double Gap = FVector2D::Distance(Unit.PreviousPosition, Other.PreviousPosition) - Unit.Stats.Radius - Other.Stats.Radius;
	return Gap <= Ranged->Range
		&& (!Ranged->bNeedsLineOfSight || Config.Grid.HasLineOfSight(Unit.PreviousPosition, Other.PreviousPosition));
}

int32 FCombatSimulation::FindVisibleEnemyInRange(const FCombatUnit& Unit) const
{
	int32 BestId = INDEX_NONE;
	double BestDistSq = TNumericLimits<double>::Max();
	for (const FCombatUnit& Other : Units)
	{
		const double DistSq = FVector2D::DistSquared(Unit.PreviousPosition, Other.PreviousPosition);
		if (DistSq < BestDistSq && CanShootNow(Unit, Other))
		{
			BestDistSq = DistSq;
			BestId = Other.Id;
		}
	}
	return BestId;
}

int32 FCombatSimulation::FindReadyAreaAttack(const FCombatUnit& Unit) const
{
	for (int32 Index = 0; Index < Unit.Stats.Attacks.Num(); ++Index)
	{
		const FCombatAttackStats& Attack = Unit.Stats.Attacks[Index];
		if (Attack.AreaShape != ECombatAreaShape::CircleAroundSelf || Unit.AttackCooldowns[Index] > 0)
		{
			continue;
		}

		FCombatArea Area;
		Area.Shape = Attack.AreaShape;
		Area.Center = Unit.PreviousPosition;
		Area.Radius = Attack.AreaRadius;
		Area.SourceRadius = Unit.Stats.Radius;

		for (const FCombatUnit& Other : Units)
		{
			if (!AffectsUnit(Attack, Unit, Other) || !Area.Contains(Other.PreviousPosition, Other.Stats.Radius)
				|| (Attack.bNeedsLineOfSight && !Config.Grid.HasLineOfSight(Unit.PreviousPosition, Other.PreviousPosition)))
			{
				continue;
			}

			// Worth using if it damages an enemy here, or if a unit here lacks one of its effects from this unit.
			const bool bDamagesEnemy = Attack.Damage > 0.f && Other.Team != Unit.Team;
			const bool bMissingEffect = Attack.Effects.ContainsByPredicate([&Other, &Unit](const FCombatEffectStats& Effect)
			{
				return !Other.Effects.HasEffectFromSource(Effect.EffectTag, Unit.Id);
			});
			if (bDamagesEnemy || bMissingEffect)
			{
				return Index;
			}
		}
	}
	return INDEX_NONE;
}

bool FCombatSimulation::AffectsUnit(const FCombatAttackStats& Attack, const FCombatUnit& Source, const FCombatUnit& Other)
{
	if (!Other.bAlive)
	{
		return false;
	}
	return Other.Team == Source.Team ? Attack.bAffectsAllies : Attack.bAffectsEnemies;
}

int32 FCombatSimulation::FindUsableAttack(const FCombatUnit& Unit, double Gap, bool bClearWalkingLine, bool bLineOfSight) const
{
	int32 BestIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Unit.Stats.Attacks.Num(); ++Index)
	{
		const FCombatAttackStats& Attack = Unit.Stats.Attacks[Index];
		const bool bCanReach = Attack.IsTargeted() && Gap <= Attack.Range
			&& (!Attack.bNeedsWalkableLine || bClearWalkingLine)
			&& (!Attack.bNeedsLineOfSight || bLineOfSight);
		if (bCanReach && (BestIndex == INDEX_NONE || Attack.Range < Unit.Stats.Attacks[BestIndex].Range))
		{
			BestIndex = Index;
		}
	}
	return BestIndex;
}

void FCombatSimulation::UpdateUnit(FCombatUnit& Unit)
{
	for (int32& Cooldown : Unit.AttackCooldowns)
	{
		if (Cooldown > 0)
		{
			--Cooldown;
		}
	}

	const FVector2D Move = UpdateCombat(Unit);
	const FVector2D Desired = Unit.PreviousPosition + Move + ComputeSeparation(Unit);
	Unit.Position = ResolveMove(Unit.PreviousPosition, Desired);
	Unit.Velocity = (Unit.Position - Unit.PreviousPosition) / FixedDt;
}

FVector2D FCombatSimulation::UpdateCombat(FCombatUnit& Unit)
{
	Unit.SteerPoint = Unit.PreviousPosition;

	// A player move order overrides the AI, taunts included.
	if (Unit.bHasMoveOrder)
	{
		return UpdateMoveOrder(Unit);
	}

	// A unit winding up an attack stands still and keeps its target until the hit or shot.
	if (Unit.WindupTicks > 0)
	{
		if (--Unit.WindupTicks == 0)
		{
			if (Unit.WindupTargetId == INDEX_NONE || Units[Unit.WindupTargetId].bAlive)
			{
				FireAttack(Unit, Unit.WindupAttackIndex, Unit.WindupTargetId);
			}
			Unit.WindupTargetId = INDEX_NONE;
			Unit.WindupAttackIndex = INDEX_NONE;
		}
		return FVector2D::ZeroVector;
	}

	// Area attacks (taunt) need no target and go first.
	const int32 AreaIndex = FindReadyAreaAttack(Unit);
	if (AreaIndex != INDEX_NONE)
	{
		StartAttack(Unit, INDEX_NONE, AreaIndex);
		return FVector2D::ZeroVector;
	}

	UpdateTarget(Unit);
	if (Unit.TargetId == INDEX_NONE)
	{
		return FVector2D::ZeroVector;
	}

	const FCombatUnit& Target = Units[Unit.TargetId];
	const FVector2D ToTarget = Target.PreviousPosition - Unit.PreviousPosition;
	const double Distance = ToTarget.Size();
	const double Gap = Distance - Unit.Stats.Radius - Target.Stats.Radius;
	const double MaxStep = Unit.Stats.MoveSpeed * Unit.Effects.GetMoveSpeedMultiplier() * FixedDt;
	const bool bClearWalkingLine = Config.Grid.IsLineWalkable(Unit.PreviousPosition, Target.PreviousPosition);
	const bool bLineOfSight = Config.Grid.HasLineOfSight(Unit.PreviousPosition, Target.PreviousPosition);

	const int32 AttackIndex = FindUsableAttack(Unit, Gap, bClearWalkingLine, bLineOfSight);
	if (AttackIndex != INDEX_NONE)
	{
		Unit.SteerPoint = Target.PreviousPosition;
		TryStartAttack(Unit, Target, AttackIndex);
		return FVector2D::ZeroVector;
	}

	if (bClearWalkingLine)
	{
		// Clear line: straight at the target, stopping just inside the longest range that will work from there.
		double StopGap = 0.0;
		for (const FCombatAttackStats& Attack : Unit.Stats.Attacks)
		{
			if (Attack.IsTargeted() && (!Attack.bNeedsLineOfSight || bLineOfSight))
			{
				StopGap = FMath::Max<double>(StopGap, Attack.Range);
			}
		}

		Unit.SteerPoint = Target.PreviousPosition;
		if (Gap <= StopGap)
		{
			return FVector2D::ZeroVector;
		}
		const FVector2D Direction = Distance > UE_KINDA_SMALL_NUMBER ? ToTarget / Distance : FVector2D(1.0, 0.0);
		return Direction * FMath::Min(MaxStep, Gap - StopGap + 1.0);
	}

	// Something in the way: follow a route around it.
	Unit.SteerPoint = FindRouteSteerPoint(Unit);
	const FVector2D ToSteer = Unit.SteerPoint - Unit.PreviousPosition;
	const double SteerDistance = ToSteer.Size();
	if (SteerDistance <= UE_KINDA_SMALL_NUMBER)
	{
		return FVector2D::ZeroVector;
	}
	return ToSteer / SteerDistance * FMath::Min(MaxStep, SteerDistance);
}

FVector2D FCombatSimulation::FindRouteSteerPoint(FCombatUnit& Unit) const
{
	const FCombatGridData& Grid = Config.Grid;
	const FCombatUnit& Target = Units[Unit.TargetId];
	const FIntPoint StartCell = Grid.LocalToCell(Unit.PreviousPosition);
	const FCombatDistanceMap* Map = GetDistanceMap(Unit.Team);

	// Path smoothing: steer to the farthest route cell center that is in a clear line (the first step always counts).
	FVector2D SteerPoint = Unit.PreviousPosition;

	if (Map && Map->GetNearestId(StartCell) == Unit.TargetId)
	{
		// The team's distance map leads to this target: follow it downhill.
		Unit.Path.Reset();
		FIntPoint Cell = StartCell;
		for (int32 Step = 0; Step < Config.PathLookaheadCells; ++Step)
		{
			FIntPoint Next;
			if (!Map->GetNextCell(Grid, Cell, Next))
			{
				break;
			}
			Cell = Next;

			const FVector2D Point = Grid.CellToLocal(Cell);
			if (Step > 0 && !Grid.IsLineWalkable(Unit.PreviousPosition, Point))
			{
				break;
			}
			SteerPoint = Point;
		}
		return SteerPoint;
	}

	// Another target (threat, taunt, hysteresis): an own A* route.
	return SteerAlongPath(Unit, Grid.LocalToCell(Target.PreviousPosition), Target.PreviousPosition);
}

FVector2D FCombatSimulation::SteerAlongPath(FCombatUnit& Unit, const FIntPoint& GoalCell, const FVector2D& Fallback) const
{
	const FCombatGridData& Grid = Config.Grid;
	const FIntPoint StartCell = Grid.LocalToCell(Unit.PreviousPosition);

	// Recomputed only when either end changes cell.
	const bool bPathValid = Unit.Path.Num() >= 2 && Unit.Path[0] == StartCell && Unit.Path.Last() == GoalCell;
	if (!bPathValid && !CombatPathfinding::FindPath(Grid, StartCell, GoalCell, Unit.Path))
	{
		// No route: head straight for it; ResolveMove keeps the unit out of blocked cells.
		return Fallback;
	}

	// Path smoothing: the farthest path cell center in a clear line (the first step always counts).
	FVector2D SteerPoint = Unit.PreviousPosition;
	const int32 LastIndex = FMath::Min(Config.PathLookaheadCells, Unit.Path.Num() - 1);
	for (int32 Index = 1; Index <= LastIndex; ++Index)
	{
		const FVector2D Point = Grid.CellToLocal(Unit.Path[Index]);
		if (Index > 1 && !Grid.IsLineWalkable(Unit.PreviousPosition, Point))
		{
			break;
		}
		SteerPoint = Point;
	}
	return SteerPoint;
}

FVector2D FCombatSimulation::UpdateMoveOrder(FCombatUnit& Unit)
{
	const FCombatGridData& Grid = Config.Grid;
	const FVector2D Goal = Grid.CellToLocal(Unit.MoveTargetCell);
	const double MaxStep = Unit.Stats.MoveSpeed * Unit.Effects.GetMoveSpeedMultiplier() * FixedDt;

	Unit.TargetId = INDEX_NONE;
	Unit.TargetReason = ECombatTargetReason::None;

	const FVector2D ToGoal = Goal - Unit.PreviousPosition;
	const double GoalDistance = ToGoal.Size();
	if (GoalDistance <= FMath::Max(MaxStep, 1.0))
	{
		// Arrived: step onto the cell center and hand back to the AI.
		Unit.bHasMoveOrder = false;
		Unit.Path.Reset();
		Unit.SteerPoint = Goal;
		return ToGoal;
	}

	if (Grid.IsLineWalkable(Unit.PreviousPosition, Goal))
	{
		Unit.SteerPoint = Goal;
	}
	else
	{
		const FIntPoint StartCell = Grid.LocalToCell(Unit.PreviousPosition);
		const bool bPathValid = Unit.Path.Num() >= 2 && Unit.Path[0] == StartCell && Unit.Path.Last() == Unit.MoveTargetCell;
		if (!bPathValid && !CombatPathfinding::FindPath(Grid, StartCell, Unit.MoveTargetCell, Unit.Path))
		{
			// Unreachable: give up the order.
			Unit.bHasMoveOrder = false;
			Unit.Path.Reset();
			return FVector2D::ZeroVector;
		}
		Unit.SteerPoint = SteerAlongPath(Unit, Unit.MoveTargetCell, Goal);
	}

	const FVector2D ToSteer = Unit.SteerPoint - Unit.PreviousPosition;
	const double SteerDistance = ToSteer.Size();
	if (SteerDistance <= UE_KINDA_SMALL_NUMBER)
	{
		return FVector2D::ZeroVector;
	}
	return ToSteer / SteerDistance * FMath::Min(MaxStep, SteerDistance);
}

bool FCombatSimulation::QueueCommand(const FCombatCommand& Command)
{
	if (Command.Tick <= Tick || IsFinished())
	{
		return false;
	}

	// After every command with the same or an earlier tick, so the given order holds within a tick.
	const int32 InsertAt = PendingCommands.IndexOfByPredicate([&Command](const FCombatCommand& Pending) { return Pending.Tick > Command.Tick; });
	PendingCommands.Insert(Command, InsertAt == INDEX_NONE ? PendingCommands.Num() : InsertAt);
	CommandLog.Add(Command);
	return true;
}

void FCombatSimulation::ExecuteDueCommands()
{
	int32 Count = 0;
	while (Count < PendingCommands.Num() && PendingCommands[Count].Tick <= Tick)
	{
		ExecuteCommand(PendingCommands[Count]);
		++Count;
	}
	PendingCommands.RemoveAt(0, Count);
}

void FCombatSimulation::ExecuteCommand(const FCombatCommand& Command)
{
	auto AddEvent = [this, &Command](ECombatEventType Type, int32 AttackIndex)
	{
		FCombatEvent& Event = Events.Add_GetRef({ Type, Command.UnitId, INDEX_NONE, 0.f });
		Event.AttackIndex = AttackIndex;
	};

	if (Command.Type == ECombatCommandType::CallWave)
	{
		if (WavesStarted >= Config.Waves.Num())
		{
			AddEvent(ECombatEventType::CommandRejected, INDEX_NONE);
			return;
		}
		AddEvent(ECombatEventType::CommandExecuted, INDEX_NONE);
		StartWave();
		return;
	}

	if (!Units.IsValidIndex(Command.UnitId) || !Units[Command.UnitId].bAlive)
	{
		AddEvent(ECombatEventType::CommandRejected, INDEX_NONE);
		return;
	}
	FCombatUnit& Unit = Units[Command.UnitId];

	switch (Command.Type)
	{
	case ECombatCommandType::Move:
		if (!Config.Grid.IsWalkable(Command.TargetCell))
		{
			AddEvent(ECombatEventType::CommandRejected, INDEX_NONE);
			return;
		}
		// The order replaces any windup and earlier order.
		Unit.bHasMoveOrder = true;
		Unit.MoveTargetCell = Command.TargetCell;
		Unit.WindupTicks = 0;
		Unit.WindupTargetId = INDEX_NONE;
		Unit.WindupAttackIndex = INDEX_NONE;
		Unit.Path.Reset();
		AddEvent(ECombatEventType::CommandExecuted, INDEX_NONE);
		return;

	case ECombatCommandType::Ability:
	{
		// Only areas around the unit for now (taunt, auras): they need no target.
		const bool bValid = Unit.Stats.PlayerAbilities.IsValidIndex(Command.AbilityIndex)
			&& Unit.Stats.PlayerAbilities[Command.AbilityIndex].AreaShape == ECombatAreaShape::CircleAroundSelf;
		if (!bValid)
		{
			AddEvent(ECombatEventType::CommandRejected, INDEX_NONE);
			return;
		}

		const int32 AttackIndex = Unit.Stats.GetPlayerAbilityAttackIndex(Command.AbilityIndex);
		AddEvent(ECombatEventType::CommandExecuted, AttackIndex);
		FCombatEvent& Event = Events.Add_GetRef({ ECombatEventType::Attack, Unit.Id, INDEX_NONE, 0.f });
		Event.AttackIndex = AttackIndex;
		PlaceArea(Unit, AttackIndex, INDEX_NONE);
		return;
	}

	case ECombatCommandType::CallWave:
		break;
	}
}

FVector2D FCombatSimulation::ComputeSeparation(const FCombatUnit& Unit) const
{
	FVector2D Push = FVector2D::ZeroVector;
	for (const FCombatUnit& Other : Units)
	{
		if (!Other.bAlive || Other.Id == Unit.Id)
		{
			continue;
		}

		const FVector2D Offset = Unit.PreviousPosition - Other.PreviousPosition;
		const double Distance = Offset.Size();
		const double MinDistance = Unit.Stats.Radius + Other.Stats.Radius;
		if (Distance >= MinDistance)
		{
			continue;
		}

		// Exactly on top of each other: the lower ID goes to -X, the higher to +X.
		const FVector2D Direction = Distance > UE_KINDA_SMALL_NUMBER ? Offset / Distance : FVector2D(Unit.Id < Other.Id ? -1.0 : 1.0, 0.0);
		Push += Direction * (MinDistance - Distance) * 0.5 * Config.SeparationStrength;
	}
	return Push;
}

FVector2D FCombatSimulation::ResolveMove(const FVector2D& From, const FVector2D& To) const
{
	const FCombatGridData& Grid = Config.Grid;
	if (Grid.IsWalkable(Grid.LocalToCell(To)))
	{
		return To;
	}

	const FVector2D SlideX(To.X, From.Y);
	if (Grid.IsWalkable(Grid.LocalToCell(SlideX)))
	{
		return SlideX;
	}

	const FVector2D SlideY(From.X, To.Y);
	if (Grid.IsWalkable(Grid.LocalToCell(SlideY)))
	{
		return SlideY;
	}
	return From;
}

void FCombatSimulation::TryStartAttack(FCombatUnit& Unit, const FCombatUnit& Target, int32 AttackIndex)
{
	if (Unit.FirstAttackDelayTicks > 0)
	{
		--Unit.FirstAttackDelayTicks;
		return;
	}
	if (Unit.AttackCooldowns[AttackIndex] > 0)
	{
		return;
	}
	StartAttack(Unit, Target.Id, AttackIndex);
}

void FCombatSimulation::StartAttack(FCombatUnit& Unit, int32 TargetId, int32 AttackIndex)
{
	const FCombatAttackStats& Attack = Unit.Stats.GetAttack(AttackIndex);
	Unit.AttackCooldowns[AttackIndex] = Attack.CooldownTicks;

	FCombatEvent& Event = Events.Add_GetRef({ ECombatEventType::Attack, Unit.Id, TargetId, 0.f });
	Event.AttackIndex = AttackIndex;

	if (Attack.WindupTicks > 0)
	{
		Unit.WindupTicks = Attack.WindupTicks;
		Unit.WindupTargetId = TargetId;
		Unit.WindupAttackIndex = AttackIndex;
	}
	else
	{
		FireAttack(Unit, AttackIndex, TargetId);
	}
}

void FCombatSimulation::FireAttack(const FCombatUnit& Unit, int32 AttackIndex, int32 TargetId)
{
	const FCombatAttackStats& Attack = Unit.Stats.GetAttack(AttackIndex);
	if (Attack.IsArea())
	{
		PlaceArea(Unit, AttackIndex, TargetId);
		return;
	}

	const float Damage = Attack.Damage * Unit.Effects.GetDamageDealtMultiplier();
	if (Attack.ProjectileSpeed <= 0.f)
	{
		PendingHits.Add({ Unit.Id, TargetId, AttackIndex, Damage, Attack.ThreatMultiplier });
		return;
	}

	FCombatProjectile& Projectile = Projectiles.AddDefaulted_GetRef();
	Projectile.Id = NextProjectileId++;
	Projectile.SourceId = Unit.Id;
	Projectile.TargetId = TargetId;
	Projectile.AttackIndex = AttackIndex;
	Projectile.Team = Unit.Team;
	Projectile.PreviousPosition = Unit.PreviousPosition;
	Projectile.Position = Unit.PreviousPosition;
	Projectile.Speed = Attack.ProjectileSpeed;
	Projectile.Damage = Damage;
	Projectile.ThreatMultiplier = Attack.ThreatMultiplier;

	FCombatEvent& Event = Events.Add_GetRef({ ECombatEventType::ProjectileSpawned, Unit.Id, TargetId, 0.f });
	Event.AttackIndex = AttackIndex;
	Event.ProjectileId = Projectile.Id;
}

void FCombatSimulation::PlaceArea(const FCombatUnit& Unit, int32 AttackIndex, int32 TargetId)
{
	const FCombatAttackStats& Attack = Unit.Stats.GetAttack(AttackIndex);

	FCombatArea Area;
	Area.Shape = Attack.AreaShape;
	Area.Radius = Attack.AreaRadius;
	Area.ConeCosHalfAngle = Attack.ConeCosHalfAngle;
	Area.SourceRadius = Unit.Stats.Radius;
	Area.Center = Unit.PreviousPosition;

	const FVector2D TargetPosition = TargetId != INDEX_NONE ? Units[TargetId].PreviousPosition : Unit.PreviousPosition;
	if (Attack.AreaShape == ECombatAreaShape::CircleAtTarget)
	{
		Area.Center = TargetPosition;
	}
	else if (Attack.AreaShape == ECombatAreaShape::Cone)
	{
		const FVector2D ToTarget = TargetPosition - Unit.PreviousPosition;
		const double Length = ToTarget.Size();
		Area.Direction = Length > UE_KINDA_SMALL_NUMBER ? ToTarget / Length : FVector2D(1.0, 0.0);
	}

	const int32 AreaId = NextAreaId++;
	const float DamageDealtMultiplier = Unit.Effects.GetDamageDealtMultiplier();
	if (Attack.TelegraphTicks <= 0)
	{
		ResolveArea(Unit.Id, Unit.Team, AttackIndex, Area, AreaId, DamageDealtMultiplier);
		return;
	}

	// Telegraph: the area stays where it was placed and goes off after TelegraphTicks.
	FCombatPendingArea& Pending = PendingAreas.AddDefaulted_GetRef();
	Pending.Id = AreaId;
	Pending.SourceId = Unit.Id;
	Pending.Team = Unit.Team;
	Pending.AttackIndex = AttackIndex;
	Pending.Area = Area;
	Pending.TotalTicks = Attack.TelegraphTicks;
	Pending.RemainingTicks = Attack.TelegraphTicks;
	Pending.DamageDealtMultiplier = DamageDealtMultiplier;

	FCombatEvent& Event = Events.Add_GetRef({ ECombatEventType::AreaTelegraphStarted, Unit.Id, TargetId, 0.f });
	Event.AttackIndex = AttackIndex;
	Event.AreaId = AreaId;
	Event.Area = Area;
}

void FCombatSimulation::ResolveArea(int32 SourceId, int32 Team, int32 AttackIndex, const FCombatArea& Area, int32 AreaId, float DamageDealtMultiplier)
{
	const FCombatUnit& Source = Units[SourceId];
	const FCombatAttackStats& Attack = Source.Stats.GetAttack(AttackIndex);

	FCombatEvent& Event = Events.Add_GetRef({ ECombatEventType::AreaAttackFired, SourceId, INDEX_NONE, 0.f });
	Event.AttackIndex = AttackIndex;
	Event.AreaId = AreaId;
	Event.Area = Area;
	Event.Cue = Attack.ImpactCue;

	// Every affected unit inside, in ID order. Team comes from the area, so it still works after the source died.
	for (const FCombatUnit& Other : Units)
	{
		const bool bAffected = Other.bAlive && (Other.Team == Team ? Attack.bAffectsAllies : Attack.bAffectsEnemies);
		if (bAffected && Area.Contains(Other.PreviousPosition, Other.Stats.Radius)
			&& (!Attack.bNeedsLineOfSight || Config.Grid.HasLineOfSight(Area.Center, Other.PreviousPosition)))
		{
			PendingHits.Add({ SourceId, Other.Id, AttackIndex, Attack.Damage * DamageDealtMultiplier, Attack.ThreatMultiplier });
		}
	}
}

void FCombatSimulation::UpdatePendingAreas()
{
	for (FCombatPendingArea& Pending : PendingAreas)
	{
		if (--Pending.RemainingTicks <= 0)
		{
			ResolveArea(Pending.SourceId, Pending.Team, Pending.AttackIndex, Pending.Area, Pending.Id, Pending.DamageDealtMultiplier);
		}
	}
	PendingAreas.RemoveAll([](const FCombatPendingArea& Pending) { return Pending.RemainingTicks <= 0; });
}

void FCombatSimulation::UpdateProjectiles()
{
	const FCombatGridData& Grid = Config.Grid;
	for (FCombatProjectile& Projectile : Projectiles)
	{
		const FCombatUnit& Target = Units[Projectile.TargetId];
		if (!Target.bAlive)
		{
			EndProjectile(Projectile);
			continue;
		}

		// Homing: fly straight at the target's current position.
		const FVector2D ToTarget = Target.Position - Projectile.PreviousPosition;
		const double Distance = ToTarget.Size();
		const double MaxStep = Projectile.Speed * FixedDt;

		if (Distance - Target.Stats.Radius <= MaxStep)
		{
			if (Grid.HasLineOfSight(Projectile.PreviousPosition, Target.Position))
			{
				Projectile.Position = Target.Position;
				PendingHits.Add({ Projectile.SourceId, Projectile.TargetId, Projectile.AttackIndex, Projectile.Damage, Projectile.ThreatMultiplier });
			}
			EndProjectile(Projectile);
			continue;
		}

		const FVector2D NewPosition = Projectile.PreviousPosition + ToTarget / Distance * MaxStep;
		if (!Grid.HasLineOfSight(Projectile.PreviousPosition, NewPosition))
		{
			EndProjectile(Projectile);
			continue;
		}
		Projectile.Position = NewPosition;
	}

	Projectiles.RemoveAll([](const FCombatProjectile& Projectile) { return Projectile.bEnded; });
}

void FCombatSimulation::EndProjectile(FCombatProjectile& Projectile)
{
	Projectile.bEnded = true;
	FCombatEvent& Event = Events.Add_GetRef({ ECombatEventType::ProjectileEnded, Projectile.SourceId, Projectile.TargetId, 0.f });
	Event.AttackIndex = Projectile.AttackIndex;
	Event.ProjectileId = Projectile.Id;
}

void FCombatSimulation::ApplyPendingHits()
{
	// Pass 2: all damage of this step lands together, then effects, then deaths are resolved.
	for (const FPendingHit& Hit : PendingHits)
	{
		if (Hit.Damage > 0.f)
		{
			FCombatUnit& Target = Units[Hit.TargetId];
			const float Damage = Hit.Damage * Target.Effects.GetDamageTakenMultiplier();
			Target.HP -= Damage;
			Target.DamageTaken += Damage;
			Units[Hit.SourceId].DamageDealt += Damage;
			AddThreat(Target, Hit.SourceId, Damage * Hit.ThreatMultiplier);

			FCombatEvent& Event = Events.Add_GetRef({ ECombatEventType::Hit, Hit.SourceId, Hit.TargetId, Damage });
			Event.AttackIndex = Hit.AttackIndex;
			Event.Cue = Units[Hit.SourceId].Stats.GetAttack(Hit.AttackIndex).ImpactCue;
		}
	}

	for (const FPendingHit& Hit : PendingHits)
	{
		FCombatUnit& Target = Units[Hit.TargetId];
		if (!Target.bAlive || Target.HP <= 0.f)
		{
			continue;
		}

		const TArray<FCombatEffectStats>& Effects = Units[Hit.SourceId].Stats.GetAttack(Hit.AttackIndex).Effects;
		for (int32 EffectIndex = 0; EffectIndex < Effects.Num(); ++EffectIndex)
		{
			if (Target.Effects.Apply(Effects[EffectIndex], Hit.SourceId, Hit.AttackIndex, EffectIndex, Target.Stats.Tags))
			{
				FCombatEvent& Event = Events.Add_GetRef({ ECombatEventType::EffectApplied, Hit.SourceId, Hit.TargetId, 0.f });
				Event.AttackIndex = Hit.AttackIndex;
			}
		}
	}

	for (FCombatUnit& Unit : Units)
	{
		if (Unit.bAlive && Unit.HP <= 0.f)
		{
			Unit.bAlive = false;
			Unit.HP = 0.f;
			Unit.Velocity = FVector2D::ZeroVector;
			Unit.TargetId = INDEX_NONE;
			Unit.TargetReason = ECombatTargetReason::None;
			Unit.WindupTicks = 0;
			Unit.WindupTargetId = INDEX_NONE;
			Unit.Threat.Reset();
			Unit.Effects.Reset();
			Unit.Path.Reset();
			Events.Add({ ECombatEventType::Death, INDEX_NONE, Unit.Id, 0.f });
			bDistanceMapsDirty = true;
		}
	}
}

void FCombatSimulation::AddThreat(FCombatUnit& Unit, int32 EnemyId, float Amount)
{
	if (Amount <= 0.f)
	{
		return;
	}

	if (FCombatThreatEntry* Entry = Unit.Threat.FindByPredicate([EnemyId](const FCombatThreatEntry& Candidate) { return Candidate.EnemyId == EnemyId; }))
	{
		Entry->Threat += Amount;
		return;
	}

	if (Unit.Threat.Num() < FCombatUnit::MaxThreatEntries)
	{
		Unit.Threat.Add({ EnemyId, Amount });
		return;
	}

	// Full: replace the lowest entry (the first one on ties) if the newcomer has more.
	int32 LowestIndex = 0;
	for (int32 Index = 1; Index < Unit.Threat.Num(); ++Index)
	{
		if (Unit.Threat[Index].Threat < Unit.Threat[LowestIndex].Threat)
		{
			LowestIndex = Index;
		}
	}
	if (Amount > Unit.Threat[LowestIndex].Threat)
	{
		Unit.Threat[LowestIndex] = { EnemyId, Amount };
	}
}

void FCombatSimulation::DecayThreat(FCombatUnit& Unit)
{
	for (FCombatThreatEntry& Entry : Unit.Threat)
	{
		Entry.Threat = Entry.Threat * Config.ThreatDecayFactorPerTick - Config.ThreatDecayAmountPerTick;
	}
	Unit.Threat.RemoveAll([this](const FCombatThreatEntry& Entry)
	{
		return Entry.Threat <= 0.f || !Units[Entry.EnemyId].bAlive;
	});
}

void FCombatSimulation::UpdateOutcome()
{
	TArray<int32, TInlineAllocator<4>> TeamsAlive;
	for (const FCombatUnit& Unit : Units)
	{
		if (Unit.bAlive)
		{
			TeamsAlive.AddUnique(Unit.Team);
		}
	}
	// Enemies still to come keep their team in the fight.
	if (HasWavesLeft())
	{
		TeamsAlive.AddUnique(Config.WaveTeam);
	}

	if (TeamsAlive.Num() == 0)
	{
		Outcome = ECombatOutcome::Draw;
	}
	else if (TeamsAlive.Num() == 1)
	{
		Outcome = ECombatOutcome::TeamWon;
		WinningTeam = TeamsAlive[0];
	}
	else if (Tick >= Config.MaxTicks)
	{
		Outcome = ECombatOutcome::TimeLimit;
	}
}

uint32 FCombatSimulation::ComputeChecksum() const
{
	uint32 Crc = FCrc::MemCrc32(&Tick, sizeof(Tick));
	for (const FCombatUnit& Unit : Units)
	{
		const uint8 bAlive = Unit.bAlive ? 1 : 0;
		Crc = FCrc::MemCrc32(&Unit.Position, sizeof(Unit.Position), Crc);
		Crc = FCrc::MemCrc32(&Unit.HP, sizeof(Unit.HP), Crc);
		Crc = FCrc::MemCrc32(&Unit.TargetId, sizeof(Unit.TargetId), Crc);
		Crc = FCrc::MemCrc32(Unit.AttackCooldowns.GetData(), Unit.AttackCooldowns.Num() * sizeof(int32), Crc);
		Crc = FCrc::MemCrc32(&Unit.WindupAttackIndex, sizeof(Unit.WindupAttackIndex), Crc);
		Crc = FCrc::MemCrc32(&Unit.FirstAttackDelayTicks, sizeof(Unit.FirstAttackDelayTicks), Crc);
		Crc = FCrc::MemCrc32(&Unit.WindupTicks, sizeof(Unit.WindupTicks), Crc);
		Crc = FCrc::MemCrc32(&bAlive, sizeof(bAlive), Crc);
		Crc = FCrc::MemCrc32(&Unit.TargetReason, sizeof(Unit.TargetReason), Crc);
		for (const FCombatThreatEntry& Entry : Unit.Threat)
		{
			Crc = FCrc::MemCrc32(&Entry.EnemyId, sizeof(Entry.EnemyId), Crc);
			Crc = FCrc::MemCrc32(&Entry.Threat, sizeof(Entry.Threat), Crc);
		}
		Crc = Unit.Effects.AppendChecksum(Crc);
		// Only while a move order is active, so a fight without commands keeps its old checksums.
		if (Unit.bHasMoveOrder)
		{
			Crc = FCrc::MemCrc32(&Unit.MoveTargetCell, sizeof(Unit.MoveTargetCell), Crc);
		}
	}
	for (const FCombatProjectile& Projectile : Projectiles)
	{
		Crc = FCrc::MemCrc32(&Projectile.Id, sizeof(Projectile.Id), Crc);
		Crc = FCrc::MemCrc32(&Projectile.Position, sizeof(Projectile.Position), Crc);
		Crc = FCrc::MemCrc32(&Projectile.TargetId, sizeof(Projectile.TargetId), Crc);
	}
	for (const FCombatPendingArea& Pending : PendingAreas)
	{
		Crc = FCrc::MemCrc32(&Pending.Id, sizeof(Pending.Id), Crc);
		Crc = FCrc::MemCrc32(&Pending.RemainingTicks, sizeof(Pending.RemainingTicks), Crc);
		Crc = FCrc::MemCrc32(&Pending.Area.Center, sizeof(Pending.Area.Center), Crc);
		Crc = FCrc::MemCrc32(&Pending.Area.Direction, sizeof(Pending.Area.Direction), Crc);
	}
	// Only with waves, so fights without them keep their old checksums.
	if (!Config.Waves.IsEmpty())
	{
		const int32 PendingSpawnCount = PendingSpawns.Num();
		Crc = FCrc::MemCrc32(&WavesStarted, sizeof(WavesStarted), Crc);
		Crc = FCrc::MemCrc32(&NextWaveTick, sizeof(NextWaveTick), Crc);
		Crc = FCrc::MemCrc32(&PendingSpawnCount, sizeof(PendingSpawnCount), Crc);
	}
	return Crc;
}

const TCHAR* FCombatSimulation::OutcomeToString(ECombatOutcome InOutcome)
{
	switch (InOutcome)
	{
	case ECombatOutcome::InProgress: return TEXT("InProgress");
	case ECombatOutcome::TeamWon: return TEXT("TeamWon");
	case ECombatOutcome::Draw: return TEXT("Draw");
	case ECombatOutcome::TimeLimit: return TEXT("TimeLimit");
	}
	return TEXT("Unknown");
}
