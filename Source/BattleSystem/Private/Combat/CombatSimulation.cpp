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
	}

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

void FCombatSimulation::UpdateUnit(FCombatUnit& Unit)
{
	if (Unit.CooldownTicks > 0)
	{
		--Unit.CooldownTicks;
	}

	// A unit winding up an attack stands still and keeps its target until the hit.
	if (Unit.WindupTicks > 0)
	{
		Unit.Velocity = FVector2D::ZeroVector;
		if (--Unit.WindupTicks == 0)
		{
			if (Units[Unit.WindupTargetId].bAlive)
			{
				PendingHits.Add({ Unit.Id, Unit.WindupTargetId, Unit.Stats.AttackDamage });
			}
			Unit.WindupTargetId = INDEX_NONE;
		}
		return;
	}

	Unit.TargetId = FindNearestEnemy(Unit);
	if (Unit.TargetId == INDEX_NONE)
	{
		Unit.Velocity = FVector2D::ZeroVector;
		return;
	}

	const FCombatUnit& Target = Units[Unit.TargetId];
	const FVector2D ToTarget = Target.PreviousPosition - Unit.PreviousPosition;
	const double Distance = ToTarget.Size();
	const double Gap = Distance - Unit.Stats.Radius - Target.Stats.Radius;
	const double StopGap = Unit.Stats.bHasAttack ? Unit.Stats.AttackRange : 0.0;

	if (Gap <= StopGap)
	{
		Unit.Velocity = FVector2D::ZeroVector;
		if (Unit.FirstAttackDelayTicks > 0)
		{
			--Unit.FirstAttackDelayTicks;
		}
		else if (Unit.Stats.bHasAttack && Unit.CooldownTicks == 0)
		{
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
		return;
	}

	// Phase 1 movement: straight at the target, stopping just inside attack range. Obstacles are ignored.
	const FVector2D Direction = Distance > UE_KINDA_SMALL_NUMBER ? ToTarget / Distance : FVector2D(1.0, 0.0);
	const double MoveDistance = FMath::Min<double>(Unit.Stats.MoveSpeed * FixedDt, Gap - StopGap + 1.0);

	const FVector2D GridSize = Config.Grid.GetLocalSize();
	Unit.Position = Unit.PreviousPosition + Direction * MoveDistance;
	Unit.Position.X = FMath::Clamp(Unit.Position.X, 0.0, GridSize.X);
	Unit.Position.Y = FMath::Clamp(Unit.Position.Y, 0.0, GridSize.Y);
	Unit.Velocity = (Unit.Position - Unit.PreviousPosition) / FixedDt;
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
