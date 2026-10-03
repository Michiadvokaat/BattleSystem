// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatSimulation.h"
#include "Combat/CombatPathfinding.h"
#include "Combat/CombatTags.h"
#include "Misc/Crc.h"

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

		FCombatUnit& Unit = Units.AddDefaulted_GetRef();
		Unit.Id = Index;
		Unit.Team = Spawn.Team;
		Unit.Stats = Spawn.Stats;
		for (FCombatAttackStats& Attack : Unit.Stats.Attacks)
		{
			Attack.CooldownTicks = FMath::Max(Attack.CooldownTicks, 1);
			Attack.WindupTicks = FMath::Max(Attack.WindupTicks, 0);
		}
		Unit.AttackCooldowns.Init(0, Unit.Stats.Attacks.Num());
		Unit.Position = Config.Grid.CellToLocal(Spawn.StartCell);
		Unit.PreviousPosition = Unit.Position;
		Unit.HP = Unit.Stats.MaxHP;
		Unit.bAlive = Unit.HP > 0.f;
		Unit.FirstAttackDelayTicks = Random.RandRange(0, FMath::Max(Config.MaxFirstAttackDelayTicks, 0));
		Unit.SteerPoint = Unit.Position;

		TeamIds.AddUnique(Unit.Team);
	}
	DistanceMaps.SetNum(TeamIds.Num());

	Checksum = ComputeChecksum();
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
		if (!Attack.bAreaAroundSelf || Unit.AttackCooldowns[Index] > 0)
		{
			continue;
		}

		for (const FCombatUnit& Other : Units)
		{
			if (!Other.bAlive || Other.Team == Unit.Team)
			{
				continue;
			}

			const double Gap = FVector2D::Distance(Unit.PreviousPosition, Other.PreviousPosition) - Unit.Stats.Radius - Other.Stats.Radius;
			if (Gap > Attack.Range || (Attack.bNeedsLineOfSight && !Config.Grid.HasLineOfSight(Unit.PreviousPosition, Other.PreviousPosition)))
			{
				continue;
			}

			// Worth using if it deals damage, or if an enemy in range lacks one of its effects from this unit.
			const bool bMissingEffect = Attack.Effects.ContainsByPredicate([&Other, &Unit](const FCombatEffectStats& Effect)
			{
				return !Other.Effects.HasEffectFromSource(Effect.EffectTag, Unit.Id);
			});
			if (Attack.Damage > 0.f || bMissingEffect)
			{
				return Index;
			}
		}
	}
	return INDEX_NONE;
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
	const double MaxStep = Unit.Stats.MoveSpeed * FixedDt;
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

	// Another target (threat, taunt, hysteresis): an own A* route, recomputed when either end changes cell.
	const FIntPoint GoalCell = Grid.LocalToCell(Target.PreviousPosition);
	const bool bPathValid = Unit.Path.Num() >= 2 && Unit.Path[0] == StartCell && Unit.Path.Last() == GoalCell;
	if (!bPathValid && !CombatPathfinding::FindPath(Grid, StartCell, GoalCell, Unit.Path))
	{
		// No route: head straight at the target; ResolveMove keeps the unit out of blocked cells.
		return Target.PreviousPosition;
	}

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
	const FCombatAttackStats& Attack = Unit.Stats.Attacks[AttackIndex];
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
	const FCombatAttackStats& Attack = Unit.Stats.Attacks[AttackIndex];
	const float Threat = Attack.Damage * Attack.ThreatMultiplier;

	if (Attack.bAreaAroundSelf)
	{
		// Reach from the center: an enemy is hit when its edge is within Range of this unit's edge.
		FCombatEvent& Event = Events.Add_GetRef({ ECombatEventType::AreaAttackFired, Unit.Id, INDEX_NONE, Attack.Range + Unit.Stats.Radius });
		Event.AttackIndex = AttackIndex;

		// Every enemy in range, in ID order.
		for (const FCombatUnit& Other : Units)
		{
			if (!Other.bAlive || Other.Team == Unit.Team)
			{
				continue;
			}
			const double Gap = FVector2D::Distance(Unit.PreviousPosition, Other.PreviousPosition) - Unit.Stats.Radius - Other.Stats.Radius;
			if (Gap <= Attack.Range && (!Attack.bNeedsLineOfSight || Config.Grid.HasLineOfSight(Unit.PreviousPosition, Other.PreviousPosition)))
			{
				PendingHits.Add({ Unit.Id, Other.Id, AttackIndex, Attack.Damage, Threat });
			}
		}
		return;
	}

	if (Attack.ProjectileSpeed <= 0.f)
	{
		PendingHits.Add({ Unit.Id, TargetId, AttackIndex, Attack.Damage, Threat });
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
	Projectile.Damage = Attack.Damage;
	Projectile.Threat = Threat;

	FCombatEvent& Event = Events.Add_GetRef({ ECombatEventType::ProjectileSpawned, Unit.Id, TargetId, 0.f });
	Event.AttackIndex = AttackIndex;
	Event.ProjectileId = Projectile.Id;
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
				PendingHits.Add({ Projectile.SourceId, Projectile.TargetId, Projectile.AttackIndex, Projectile.Damage, Projectile.Threat });
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
			Target.HP -= Hit.Damage;
			AddThreat(Target, Hit.SourceId, Hit.Threat);
			Events.Add({ ECombatEventType::Hit, Hit.SourceId, Hit.TargetId, Hit.Damage });
		}
	}

	for (const FPendingHit& Hit : PendingHits)
	{
		FCombatUnit& Target = Units[Hit.TargetId];
		if (!Target.bAlive || Target.HP <= 0.f)
		{
			continue;
		}

		const TArray<FCombatEffectStats>& Effects = Units[Hit.SourceId].Stats.Attacks[Hit.AttackIndex].Effects;
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
	}
	for (const FCombatProjectile& Projectile : Projectiles)
	{
		Crc = FCrc::MemCrc32(&Projectile.Id, sizeof(Projectile.Id), Crc);
		Crc = FCrc::MemCrc32(&Projectile.Position, sizeof(Projectile.Position), Crc);
		Crc = FCrc::MemCrc32(&Projectile.TargetId, sizeof(Projectile.TargetId), Crc);
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
