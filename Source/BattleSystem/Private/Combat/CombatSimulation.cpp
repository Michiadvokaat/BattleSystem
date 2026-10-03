// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatSimulation.h"
#include "Misc/Crc.h"

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
		Unit.Stats.AttackCooldownTicks = FMath::Max(Unit.Stats.AttackCooldownTicks, 1);
		Unit.Stats.AttackWindupTicks = FMath::Max(Unit.Stats.AttackWindupTicks, 0);
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

int32 FCombatSimulation::ChooseTarget(const FCombatUnit& Unit) const
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

void FCombatSimulation::UpdateUnit(FCombatUnit& Unit)
{
	if (Unit.CooldownTicks > 0)
	{
		--Unit.CooldownTicks;
	}

	const FVector2D Move = UpdateCombat(Unit);
	const FVector2D Desired = Unit.PreviousPosition + Move + ComputeSeparation(Unit);
	Unit.Position = ResolveMove(Unit.PreviousPosition, Desired);
	Unit.Velocity = (Unit.Position - Unit.PreviousPosition) / FixedDt;
}

FVector2D FCombatSimulation::UpdateCombat(FCombatUnit& Unit)
{
	Unit.SteerPoint = Unit.PreviousPosition;

	// A unit winding up an attack stands still and keeps its target until the hit.
	if (Unit.WindupTicks > 0)
	{
		if (--Unit.WindupTicks == 0)
		{
			if (Units[Unit.WindupTargetId].bAlive)
			{
				PendingHits.Add({ Unit.Id, Unit.WindupTargetId, Unit.Stats.AttackDamage });
			}
			Unit.WindupTargetId = INDEX_NONE;
		}
		return FVector2D::ZeroVector;
	}

	Unit.TargetId = ChooseTarget(Unit);
	if (Unit.TargetId == INDEX_NONE)
	{
		return FVector2D::ZeroVector;
	}

	const FCombatUnit& Target = Units[Unit.TargetId];
	const FVector2D ToTarget = Target.PreviousPosition - Unit.PreviousPosition;
	const double Distance = ToTarget.Size();
	const double Gap = Distance - Unit.Stats.Radius - Target.Stats.Radius;
	const double StopGap = Unit.Stats.bHasAttack ? Unit.Stats.AttackRange : 0.0;
	const double MaxStep = Unit.Stats.MoveSpeed * FixedDt;

	if (Config.Grid.IsLineWalkable(Unit.PreviousPosition, Target.PreviousPosition))
	{
		Unit.SteerPoint = Target.PreviousPosition;
		if (Gap <= StopGap)
		{
			TryStartAttack(Unit, Target);
			return FVector2D::ZeroVector;
		}

		// Clear line: straight at the target, stopping just inside attack range.
		const FVector2D Direction = Distance > UE_KINDA_SMALL_NUMBER ? ToTarget / Distance : FVector2D(1.0, 0.0);
		return Direction * FMath::Min(MaxStep, Gap - StopGap + 1.0);
	}

	// Something in the way: follow the route of the distance map.
	Unit.SteerPoint = FindRouteSteerPoint(Unit);
	const FVector2D ToSteer = Unit.SteerPoint - Unit.PreviousPosition;
	const double SteerDistance = ToSteer.Size();
	if (SteerDistance <= UE_KINDA_SMALL_NUMBER)
	{
		return FVector2D::ZeroVector;
	}
	return ToSteer / SteerDistance * FMath::Min(MaxStep, SteerDistance);
}

FVector2D FCombatSimulation::FindRouteSteerPoint(const FCombatUnit& Unit) const
{
	const FCombatDistanceMap* Map = GetDistanceMap(Unit.Team);
	const FIntPoint StartCell = Config.Grid.LocalToCell(Unit.PreviousPosition);
	if (!Map || Map->GetDistance(StartCell) == FCombatDistanceMap::Unreachable)
	{
		// No route: head straight at the target; ResolveMove keeps the unit out of blocked cells.
		return Units[Unit.TargetId].PreviousPosition;
	}

	// Path smoothing: steer to the farthest cell center along the route that is in a clear line.
	FIntPoint Cell = StartCell;
	FVector2D SteerPoint = Unit.PreviousPosition;
	for (int32 Step = 0; Step < Config.PathLookaheadCells; ++Step)
	{
		FIntPoint Next;
		if (!Map->GetNextCell(Config.Grid, Cell, Next))
		{
			break;
		}
		Cell = Next;

		const FVector2D Point = Config.Grid.CellToLocal(Cell);
		if (Step > 0 && !Config.Grid.IsLineWalkable(Unit.PreviousPosition, Point))
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

void FCombatSimulation::TryStartAttack(FCombatUnit& Unit, const FCombatUnit& Target)
{
	if (Unit.FirstAttackDelayTicks > 0)
	{
		--Unit.FirstAttackDelayTicks;
		return;
	}
	if (!Unit.Stats.bHasAttack || Unit.CooldownTicks > 0)
	{
		return;
	}

	Unit.CooldownTicks = Unit.Stats.AttackCooldownTicks;
	Events.Add({ ECombatEventType::Attack, Unit.Id, Target.Id, 0.f });

	if (Unit.Stats.AttackWindupTicks > 0)
	{
		Unit.WindupTicks = Unit.Stats.AttackWindupTicks;
		Unit.WindupTargetId = Target.Id;
	}
	else
	{
		PendingHits.Add({ Unit.Id, Target.Id, Unit.Stats.AttackDamage });
	}
}

void FCombatSimulation::ApplyPendingHits()
{
	// Pass 2: all hits of this step land together, then deaths are resolved.
	for (const FPendingHit& Hit : PendingHits)
	{
		Units[Hit.TargetId].HP -= Hit.Damage;
		Events.Add({ ECombatEventType::Hit, Hit.SourceId, Hit.TargetId, Hit.Damage });
	}

	for (FCombatUnit& Unit : Units)
	{
		if (Unit.bAlive && Unit.HP <= 0.f)
		{
			Unit.bAlive = false;
			Unit.HP = 0.f;
			Unit.Velocity = FVector2D::ZeroVector;
			Unit.TargetId = INDEX_NONE;
			Unit.WindupTicks = 0;
			Unit.WindupTargetId = INDEX_NONE;
			Events.Add({ ECombatEventType::Death, INDEX_NONE, Unit.Id, 0.f });
			bDistanceMapsDirty = true;
		}
	}
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
		Crc = FCrc::MemCrc32(&Unit.CooldownTicks, sizeof(Unit.CooldownTicks), Crc);
		Crc = FCrc::MemCrc32(&Unit.FirstAttackDelayTicks, sizeof(Unit.FirstAttackDelayTicks), Crc);
		Crc = FCrc::MemCrc32(&Unit.WindupTicks, sizeof(Unit.WindupTicks), Crc);
		Crc = FCrc::MemCrc32(&bAlive, sizeof(bAlive), Crc);
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
