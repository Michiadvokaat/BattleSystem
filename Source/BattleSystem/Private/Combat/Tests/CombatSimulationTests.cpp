// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Combat/CombatPathfinding.h"
#include "Combat/CombatSimulation.h"
#include "Combat/CombatTags.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace CombatTests
{
	/** Test stats, independent of the project's data assets. */
	static FCombatUnitStats MakeStats(float MaxHP, float Damage, int32 CooldownTicks, int32 WindupTicks)
	{
		FCombatUnitStats Stats;
		Stats.MaxHP = MaxHP;
		Stats.MoveSpeed = 300.f;
		Stats.Radius = 40.f;
		FCombatAttackStats& Melee = Stats.Attacks.AddDefaulted_GetRef();
		Melee.Range = 100.f;
		Melee.Damage = Damage;
		Melee.CooldownTicks = CooldownTicks;
		Melee.WindupTicks = WindupTicks;
		return Stats;
	}

	static void AddUnit(FCombatSimConfig& Config, const FCombatUnitStats& Stats, int32 Team, FIntPoint Cell)
	{
		FCombatUnitSpawn& Spawn = Config.Units.AddDefaulted_GetRef();
		Spawn.Stats = Stats;
		Spawn.Team = Team;
		Spawn.StartCell = Cell;
	}

	/** 3 v 3 on an open 20x12 grid. */
	static FCombatSimConfig MakeSkirmish(int32 Seed)
	{
		FCombatSimConfig Config;
		Config.Grid.Init(20, 12, 100.f);
		Config.Seed = Seed;
		Config.TickRate = 20;
		Config.MaxTicks = 2400;
		Config.MaxFirstAttackDelayTicks = 10;

		for (int32 Row = 0; Row < 3; ++Row)
		{
			AddUnit(Config, MakeStats(100.f, 10.f, 20, 6), 0, FIntPoint(2, 3 + Row * 3));
			AddUnit(Config, MakeStats(100.f, 10.f, 20, 6), 1, FIntPoint(17, 3 + Row * 3));
		}
		return Config;
	}

	static TArray<uint32> RunAndCollectChecksums(const FCombatSimConfig& Config)
	{
		TArray<uint32> Checksums;
		FCombatSimulation Simulation(Config);
		while (!Simulation.IsFinished())
		{
			Simulation.Step();
			Checksums.Add(Simulation.GetChecksum());
		}
		return Checksums;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatGridDataTest, "BattleSystem.Combat.GridData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatGridDataTest::RunTest(const FString& Parameters)
{
	FCombatGridData Grid;
	Grid.Init(4, 3, 100.f);

	TestTrue(TEXT("Cell (2,1) center"), Grid.CellToLocal(FIntPoint(2, 1)) == FVector2D(250.0, 150.0));
	TestTrue(TEXT("Center maps back to its cell"), Grid.LocalToCell(FVector2D(250.0, 150.0)) == FIntPoint(2, 1));
	TestTrue(TEXT("Cell lower edge belongs to the cell"), Grid.LocalToCell(FVector2D(200.0, 100.0)) == FIntPoint(2, 1));
	TestTrue(TEXT("Negative positions floor"), Grid.LocalToCell(FVector2D(-1.0, -1.0)) == FIntPoint(-1, -1));

	TestTrue(TEXT("(3,2) in bounds"), Grid.IsInBounds(FIntPoint(3, 2)));
	TestFalse(TEXT("(4,0) out of bounds"), Grid.IsInBounds(FIntPoint(4, 0)));
	TestFalse(TEXT("Out of bounds is not walkable"), Grid.IsWalkable(FIntPoint(-1, 0)));

	Grid.AddFlags(FIntPoint(1, 1), ECombatCellFlags::Blocked);
	TestFalse(TEXT("Blocked cell is not walkable"), Grid.IsWalkable(FIntPoint(1, 1)));
	TestFalse(TEXT("Blocked-only cell does not block sight"), Grid.BlocksSight(FIntPoint(1, 1)));
	TestTrue(TEXT("Other cells stay walkable"), Grid.IsWalkable(FIntPoint(2, 1)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatDeterminismTest, "BattleSystem.Combat.Determinism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatDeterminismTest::RunTest(const FString& Parameters)
{
	const TArray<uint32> First = CombatTests::RunAndCollectChecksums(CombatTests::MakeSkirmish(42));
	const TArray<uint32> Second = CombatTests::RunAndCollectChecksums(CombatTests::MakeSkirmish(42));

	TestTrue(TEXT("The fight takes at least one step"), First.Num() > 0);
	TestEqual(TEXT("Same seed, same number of steps"), Second.Num(), First.Num());
	TestTrue(TEXT("Same seed, same checksum after every step"), First == Second);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatSeedTest, "BattleSystem.Combat.SeedChangesFight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatSeedTest::RunTest(const FString& Parameters)
{
	FCombatSimulation Reference(CombatTests::MakeSkirmish(1));
	Reference.RunToEnd();

	bool bAnyDifferent = false;
	for (int32 Seed = 2; Seed <= 5 && !bAnyDifferent; ++Seed)
	{
		FCombatSimulation Other(CombatTests::MakeSkirmish(Seed));
		Other.RunToEnd();
		bAnyDifferent = Other.GetChecksum() != Reference.GetChecksum();
	}

	TestTrue(TEXT("Another seed gives another final checksum"), bAnyDifferent);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatWinnerTest, "BattleSystem.Combat.StrongerTeamWins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatWinnerTest::RunTest(const FString& Parameters)
{
	FCombatSimConfig Config;
	Config.Grid.Init(20, 12, 100.f);
	Config.Seed = 7;
	CombatTests::AddUnit(Config, CombatTests::MakeStats(30.f, 10.f, 20, 6), 0, FIntPoint(2, 6));
	CombatTests::AddUnit(Config, CombatTests::MakeStats(200.f, 25.f, 20, 6), 1, FIntPoint(17, 5));
	CombatTests::AddUnit(Config, CombatTests::MakeStats(200.f, 25.f, 20, 6), 1, FIntPoint(17, 7));

	FCombatSimulation Simulation(Config);
	Simulation.RunToEnd();

	TestTrue(TEXT("The fight ends with a winner"), Simulation.GetOutcome() == ECombatOutcome::TeamWon);
	TestEqual(TEXT("Team 1 wins"), Simulation.GetWinningTeam(), 1);
	TestTrue(TEXT("Before the time limit"), Simulation.GetTick() < Config.MaxTicks);
	TestFalse(TEXT("The lone unit is dead"), Simulation.GetUnits()[0].bAlive);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatSimultaneousHitsTest, "BattleSystem.Combat.SimultaneousHits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatSimultaneousHitsTest::RunTest(const FString& Parameters)
{
	// Two identical units that kill each other with one hit, with no random start delay:
	// both hits land in the same step, so both die, whatever the processing order.
	FCombatSimConfig Config;
	Config.Grid.Init(10, 3, 100.f);
	Config.MaxFirstAttackDelayTicks = 0;
	CombatTests::AddUnit(Config, CombatTests::MakeStats(10.f, 10.f, 20, 4), 0, FIntPoint(2, 1));
	CombatTests::AddUnit(Config, CombatTests::MakeStats(10.f, 10.f, 20, 4), 1, FIntPoint(7, 1));

	FCombatSimulation Simulation(Config);
	Simulation.RunToEnd();

	TestTrue(TEXT("Both die in the same step"), Simulation.GetOutcome() == ECombatOutcome::Draw);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatTimeLimitTest, "BattleSystem.Combat.TimeLimit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatTimeLimitTest::RunTest(const FString& Parameters)
{
	FCombatSimConfig Config;
	Config.Grid.Init(20, 12, 100.f);
	Config.MaxTicks = 50;
	CombatTests::AddUnit(Config, CombatTests::MakeStats(1000.f, 1.f, 20, 0), 0, FIntPoint(2, 6));
	CombatTests::AddUnit(Config, CombatTests::MakeStats(1000.f, 1.f, 20, 0), 1, FIntPoint(17, 6));

	FCombatSimulation Simulation(Config);
	Simulation.RunToEnd();

	TestTrue(TEXT("Outcome"), Simulation.GetOutcome() == ECombatOutcome::TimeLimit);
	TestEqual(TEXT("Stops at MaxTicks"), Simulation.GetTick(), 50);
	return true;
}

namespace CombatTests
{
	/** 20x12 grid with a wall at x = 6, y = 0..8; the gap is y = 9..11. */
	static FCombatGridData MakeWallGrid()
	{
		FCombatGridData Grid;
		Grid.Init(20, 12, 100.f);
		for (int32 Y = 0; Y <= 8; ++Y)
		{
			Grid.AddFlags(FIntPoint(6, Y), ECombatCellFlags::Blocked | ECombatCellFlags::BlocksSight);
		}
		return Grid;
	}

	/** A unit that neither moves nor attacks. */
	static FCombatUnitStats MakeDummyStats()
	{
		FCombatUnitStats Stats = MakeStats(1000.f, 0.f, 20, 0);
		Stats.Attacks.Reset();
		Stats.MoveSpeed = 0.f;
		return Stats;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatLineWalkableTest, "BattleSystem.Combat.LineWalkable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatLineWalkableTest::RunTest(const FString& Parameters)
{
	const FCombatGridData Grid = CombatTests::MakeWallGrid();
	TestTrue(TEXT("Open line"), Grid.IsLineWalkable(Grid.CellToLocal(FIntPoint(2, 2)), Grid.CellToLocal(FIntPoint(5, 7))));
	TestFalse(TEXT("Line through the wall"), Grid.IsLineWalkable(Grid.CellToLocal(FIntPoint(3, 2)), Grid.CellToLocal(FIntPoint(9, 2))));
	TestTrue(TEXT("Line through the gap"), Grid.IsLineWalkable(Grid.CellToLocal(FIntPoint(3, 10)), Grid.CellToLocal(FIntPoint(9, 10))));
	TestFalse(TEXT("Diagonal past the wall end touches the wall"), Grid.IsLineWalkable(Grid.CellToLocal(FIntPoint(5, 7)), Grid.CellToLocal(FIntPoint(7, 9))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatDistanceMapTest, "BattleSystem.Combat.DistanceMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatDistanceMapTest::RunTest(const FString& Parameters)
{
	const FCombatGridData Grid = CombatTests::MakeWallGrid();

	// Source A behind the wall (ID 1), source B on the open side (ID 2).
	const FCombatDistanceMap::FSource Sources[] = { { FIntPoint(9, 2), 1 }, { FIntPoint(3, 10), 2 } };
	FCombatDistanceMap Map;
	Map.Build(Grid, Sources);

	TestEqual(TEXT("Source cell has distance 0"), Map.GetDistance(FIntPoint(9, 2)), 0);
	TestEqual(TEXT("Straight neighbor costs 10"), Map.GetDistance(FIntPoint(10, 2)), 10);
	TestEqual(TEXT("Diagonal neighbor costs 14"), Map.GetDistance(FIntPoint(10, 3)), 14);
	TestEqual(TEXT("Blocked cells are unreachable"), Map.GetDistance(FIntPoint(6, 4)), FCombatDistanceMap::Unreachable);
	TestEqual(TEXT("(3,2) is nearest to B by walking, although A is nearer as the crow flies"), Map.GetNearestId(FIntPoint(3, 2)), 2);
	TestEqual(TEXT("(8,2) is nearest to A"), Map.GetNearestId(FIntPoint(8, 2)), 1);

	FIntPoint Next;
	TestTrue(TEXT("A route step exists from (3,2)"), Map.GetNextCell(Grid, FIntPoint(3, 2), Next));
	TestTrue(TEXT("The step lowers the distance"), Map.GetDistance(Next) < Map.GetDistance(FIntPoint(3, 2)));

	// Equal distance from two sources: the lowest ID wins.
	FCombatGridData Open;
	Open.Init(5, 1, 100.f);
	const FCombatDistanceMap::FSource TieSources[] = { { FIntPoint(4, 0), 7 }, { FIntPoint(0, 0), 3 } };
	FCombatDistanceMap TieMap;
	TieMap.Build(Open, TieSources);
	TestEqual(TEXT("Tie goes to the lowest ID"), TieMap.GetNearestId(FIntPoint(2, 0)), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatWalkingTargetTest, "BattleSystem.Combat.TargetNearestByWalking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatWalkingTargetTest::RunTest(const FString& Parameters)
{
	FCombatSimConfig Config;
	Config.Grid = CombatTests::MakeWallGrid();
	CombatTests::AddUnit(Config, CombatTests::MakeStats(100.f, 10.f, 20, 6), 0, FIntPoint(3, 2));
	CombatTests::AddUnit(Config, CombatTests::MakeDummyStats(), 1, FIntPoint(9, 2));
	CombatTests::AddUnit(Config, CombatTests::MakeDummyStats(), 1, FIntPoint(3, 10));

	FCombatSimulation Simulation(Config);
	Simulation.Step();
	TestEqual(TEXT("Targets B (reachable sooner), not A (nearer as the crow flies)"), Simulation.GetUnits()[0].TargetId, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatPathAroundWallTest, "BattleSystem.Combat.PathAroundWall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatPathAroundWallTest::RunTest(const FString& Parameters)
{
	FCombatSimConfig Config;
	Config.Grid = CombatTests::MakeWallGrid();
	Config.MaxFirstAttackDelayTicks = 0;
	CombatTests::AddUnit(Config, CombatTests::MakeStats(100.f, 10.f, 20, 6), 0, FIntPoint(3, 2));
	CombatTests::AddUnit(Config, CombatTests::MakeDummyStats(), 1, FIntPoint(9, 2));

	FCombatSimulation Simulation(Config);
	bool bAlwaysWalkable = true;
	bool bAttacked = false;
	for (int32 Step = 0; Step < 600 && !bAttacked; ++Step)
	{
		Simulation.Step();
		const FCombatUnit& Unit = Simulation.GetUnits()[0];
		bAlwaysWalkable &= Config.Grid.IsWalkable(Config.Grid.LocalToCell(Unit.Position));
		bAttacked = Simulation.GetEvents().ContainsByPredicate([](const FCombatEvent& Event)
		{
			return Event.Type == ECombatEventType::Attack && Event.SourceId == 0;
		});
	}

	TestTrue(TEXT("Never in a blocked cell"), bAlwaysWalkable);
	TestTrue(TEXT("Reaches the target behind the wall and attacks"), bAttacked);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatSeparationTest, "BattleSystem.Combat.Separation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatSeparationTest::RunTest(const FString& Parameters)
{
	FCombatSimConfig Config;
	Config.Grid.Init(20, 12, 100.f);
	CombatTests::AddUnit(Config, CombatTests::MakeDummyStats(), 0, FIntPoint(5, 5));
	CombatTests::AddUnit(Config, CombatTests::MakeDummyStats(), 0, FIntPoint(5, 5));
	CombatTests::AddUnit(Config, CombatTests::MakeDummyStats(), 1, FIntPoint(15, 5));

	FCombatSimulation Simulation(Config);
	for (int32 Step = 0; Step < 60; ++Step)
	{
		Simulation.Step();
	}

	const TArray<FCombatUnit>& Units = Simulation.GetUnits();
	const double Distance = FVector2D::Distance(Units[0].Position, Units[1].Position);
	const double MinDistance = Units[0].Stats.Radius + Units[1].Stats.Radius;
	TestTrue(TEXT("Two units that start on the same spot are pushed apart"), Distance >= MinDistance * 0.9);
	return true;
}

namespace CombatTests
{
	static FCombatAttackStats MakeRangedAttack(float Range, float ProjectileSpeed, float Damage)
	{
		FCombatAttackStats Ranged;
		Ranged.Range = Range;
		Ranged.Damage = Damage;
		Ranged.CooldownTicks = 20;
		Ranged.WindupTicks = 0;
		Ranged.bNeedsWalkableLine = false;
		Ranged.bNeedsLineOfSight = true;
		Ranged.ProjectileSpeed = ProjectileSpeed;
		return Ranged;
	}

	static FCombatUnitStats MakeArcherStats(float Range = 600.f, float ProjectileSpeed = 1500.f, float Damage = 14.f)
	{
		FCombatUnitStats Stats = MakeStats(70.f, 0.f, 20, 0);
		Stats.Attacks.Reset();
		Stats.Attacks.Add(MakeRangedAttack(Range, ProjectileSpeed, Damage));
		return Stats;
	}

	/** Index of the attack in the first Attack event of a unit, or INDEX_NONE within MaxSteps. */
	static int32 RunUntilFirstAttack(FCombatSimulation& Simulation, int32 UnitId, int32 MaxSteps)
	{
		for (int32 Step = 0; Step < MaxSteps; ++Step)
		{
			Simulation.Step();
			for (const FCombatEvent& Event : Simulation.GetEvents())
			{
				if (Event.Type == ECombatEventType::Attack && Event.SourceId == UnitId)
				{
					return Event.AttackIndex;
				}
			}
		}
		return INDEX_NONE;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatLineOfSightTest, "BattleSystem.Combat.LineOfSight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatLineOfSightTest::RunTest(const FString& Parameters)
{
	FCombatGridData Grid;
	Grid.Init(10, 3, 100.f);
	Grid.AddFlags(FIntPoint(4, 1), ECombatCellFlags::BlocksSight);
	Grid.AddFlags(FIntPoint(4, 0), ECombatCellFlags::Blocked | ECombatCellFlags::BlocksSight);

	const FVector2D Left = Grid.CellToLocal(FIntPoint(1, 1));
	const FVector2D Right = Grid.CellToLocal(FIntPoint(8, 1));
	TestTrue(TEXT("A sight-only cell can be walked through"), Grid.IsLineWalkable(Left, Right));
	TestFalse(TEXT("A sight-only cell blocks line of sight"), Grid.HasLineOfSight(Left, Right));
	TestFalse(TEXT("A wall blocks walking"), Grid.IsLineWalkable(Grid.CellToLocal(FIntPoint(1, 0)), Grid.CellToLocal(FIntPoint(8, 0))));
	TestFalse(TEXT("A wall blocks sight"), Grid.HasLineOfSight(Grid.CellToLocal(FIntPoint(1, 0)), Grid.CellToLocal(FIntPoint(8, 0))));
	TestTrue(TEXT("An open row is clear"), Grid.HasLineOfSight(Grid.CellToLocal(FIntPoint(1, 2)), Grid.CellToLocal(FIntPoint(8, 2))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatArcherAroundWallTest, "BattleSystem.Combat.ArcherWalksAroundWall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatArcherAroundWallTest::RunTest(const FString& Parameters)
{
	FCombatSimConfig Config;
	Config.Grid = CombatTests::MakeWallGrid();
	Config.MaxFirstAttackDelayTicks = 0;
	CombatTests::AddUnit(Config, CombatTests::MakeArcherStats(), 0, FIntPoint(3, 2));
	CombatTests::AddUnit(Config, CombatTests::MakeDummyStats(), 1, FIntPoint(9, 2));

	FCombatSimulation Simulation(Config);
	bool bFired = false;
	bool bSightWhenFired = false;
	bool bHit = false;
	for (int32 Step = 0; Step < 600 && !bHit; ++Step)
	{
		Simulation.Step();
		for (const FCombatEvent& Event : Simulation.GetEvents())
		{
			if (Event.Type == ECombatEventType::ProjectileSpawned && !bFired)
			{
				bFired = true;
				const TArray<FCombatUnit>& Units = Simulation.GetUnits();
				bSightWhenFired = Config.Grid.HasLineOfSight(Units[0].PreviousPosition, Units[1].PreviousPosition);
			}
			bHit |= Event.Type == ECombatEventType::Hit && Event.TargetId == 1;
		}
	}

	TestTrue(TEXT("The archer fires"), bFired);
	TestTrue(TEXT("Only once it has line of sight (after walking around the wall)"), bSightWhenFired);
	TestTrue(TEXT("The projectile hits"), bHit);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatVisibleTargetTest, "BattleSystem.Combat.ArcherPrefersVisibleTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatVisibleTargetTest::RunTest(const FString& Parameters)
{
	// A hedge (sight only) at x = 5: enemy A behind it is nearer by walking, enemy B is visible and in range.
	FCombatSimConfig Config;
	Config.Grid.Init(20, 12, 100.f);
	for (int32 Y = 0; Y < 12; ++Y)
	{
		Config.Grid.AddFlags(FIntPoint(5, Y), ECombatCellFlags::BlocksSight);
	}
	CombatTests::AddUnit(Config, CombatTests::MakeArcherStats(), 0, FIntPoint(3, 2));
	CombatTests::AddUnit(Config, CombatTests::MakeDummyStats(), 1, FIntPoint(7, 2));
	CombatTests::AddUnit(Config, CombatTests::MakeDummyStats(), 1, FIntPoint(3, 7));

	FCombatSimulation Simulation(Config);
	Simulation.Step();
	TestEqual(TEXT("A is nearest by walking"), Simulation.GetDistanceMap(0)->GetNearestId(FIntPoint(3, 2)), 1);
	TestEqual(TEXT("The archer targets B, which it can shoot"), Simulation.GetUnits()[0].TargetId, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatBestAttackTest, "BattleSystem.Combat.BestAttackPerSituation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatBestAttackTest::RunTest(const FString& Parameters)
{
	// Ranged first in the list, so the choice must come from the range, not the order.
	FCombatUnitStats Mixed = CombatTests::MakeStats(100.f, 5.f, 20, 0);
	Mixed.Attacks.Insert(CombatTests::MakeRangedAttack(600.f, 1500.f, 10.f), 0);

	for (const bool bNear : { true, false })
	{
		FCombatSimConfig Config;
		Config.Grid.Init(20, 12, 100.f);
		Config.MaxFirstAttackDelayTicks = 0;
		CombatTests::AddUnit(Config, Mixed, 0, FIntPoint(5, 5));
		CombatTests::AddUnit(Config, CombatTests::MakeDummyStats(), 1, FIntPoint(bNear ? 6 : 10, 5));

		FCombatSimulation Simulation(Config);
		const int32 AttackIndex = CombatTests::RunUntilFirstAttack(Simulation, 0, 100);
		TestEqual(bNear ? TEXT("Target close: melee") : TEXT("Target far: ranged"), AttackIndex, bNear ? 1 : 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatProjectileTargetDiesTest, "BattleSystem.Combat.ProjectileEndsWhenTargetDies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatProjectileTargetDiesTest::RunTest(const FString& Parameters)
{
	// Two archers fire at once; the near one's shot kills the target, the far one's shot must vanish.
	FCombatSimConfig Config;
	Config.Grid.Init(20, 12, 100.f);
	Config.MaxFirstAttackDelayTicks = 0;
	CombatTests::AddUnit(Config, CombatTests::MakeArcherStats(900.f, 1000.f, 10.f), 0, FIntPoint(7, 5));
	CombatTests::AddUnit(Config, CombatTests::MakeArcherStats(900.f, 1000.f, 10.f), 0, FIntPoint(2, 5));
	FCombatUnitStats Weak = CombatTests::MakeDummyStats();
	Weak.MaxHP = 10.f;
	CombatTests::AddUnit(Config, Weak, 1, FIntPoint(10, 5));
	// Keeps the fight going after the weak target dies; out of range at first.
	CombatTests::AddUnit(Config, CombatTests::MakeDummyStats(), 1, FIntPoint(19, 11));

	FCombatSimulation Simulation(Config);
	int32 HitsOnWeak = 0;
	int32 ProjectilesEnded = 0;
	for (int32 Step = 0; Step < 30; ++Step)
	{
		Simulation.Step();
		for (const FCombatEvent& Event : Simulation.GetEvents())
		{
			HitsOnWeak += Event.Type == ECombatEventType::Hit && Event.TargetId == 2 ? 1 : 0;
			ProjectilesEnded += Event.Type == ECombatEventType::ProjectileEnded && Event.TargetId == 2 ? 1 : 0;
		}
	}

	TestFalse(TEXT("The weak target is dead"), Simulation.GetUnits()[2].bAlive);
	TestEqual(TEXT("Exactly one hit landed on it"), HitsOnWeak, 1);
	TestEqual(TEXT("Both projectiles at it have ended"), ProjectilesEnded, 2);
	TestFalse(TEXT("No projectile still flies at it"), Simulation.GetProjectiles().ContainsByPredicate([](const FCombatProjectile& Projectile)
	{
		return Projectile.TargetId == 2;
	}));
	return true;
}

namespace CombatTests
{
	static FCombatEffectStats MakeTauntEffect(int32 DurationTicks)
	{
		FCombatEffectStats Effect;
		Effect.EffectTag = CombatTags::Effect_Taunt;
		Effect.DurationTicks = DurationTicks;
		Effect.Stacking = ECombatEffectStacking::Refresh;
		Effect.GrantedTags.AddTag(CombatTags::Status_Taunted);
		return Effect;
	}

	/** Threat on EnemyId after a single hit of Damage, then AfterTicks steps. Returns the threat right after the hit in OutStart. */
	static float MeasureThreatDecay(const FCombatSimConfig& BaseConfig, float Damage, int32 AfterTicks, float& OutStart)
	{
		// A dummy (team 0) is shot once by an archer (team 1) whose cooldown outlasts the test.
		FCombatSimConfig Config = BaseConfig;
		Config.Grid.Init(20, 12, 100.f);
		Config.MaxFirstAttackDelayTicks = 0;
		Config.ThreatThreshold = 0.f;
		CombatTests::AddUnit(Config, CombatTests::MakeDummyStats(), 0, FIntPoint(5, 5));
		FCombatUnitStats Archer = CombatTests::MakeArcherStats(900.f, 3000.f, Damage);
		Archer.Attacks[0].CooldownTicks = 10000;
		CombatTests::AddUnit(Config, Archer, 1, FIntPoint(10, 5));

		FCombatSimulation Simulation(Config);
		OutStart = 0.f;
		for (int32 Step = 0; Step < 100 && OutStart <= 0.f; ++Step)
		{
			Simulation.Step();
			OutStart = Simulation.GetUnits()[0].GetThreatOn(1);
		}
		for (int32 Step = 0; Step < AfterTicks; ++Step)
		{
			Simulation.Step();
		}
		return Simulation.GetUnits()[0].GetThreatOn(1);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatPathfindingTest, "BattleSystem.Combat.AStarPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatPathfindingTest::RunTest(const FString& Parameters)
{
	const FCombatGridData Grid = CombatTests::MakeWallGrid();
	const FIntPoint Start(3, 2);
	const FIntPoint Goal(9, 2);

	TArray<FIntPoint> Path;
	TestTrue(TEXT("A path around the wall exists"), CombatPathfinding::FindPath(Grid, Start, Goal, Path));
	TestTrue(TEXT("It starts at the start"), Path.Num() > 0 && Path[0] == Start);
	TestTrue(TEXT("It ends at the goal"), Path.Num() > 0 && Path.Last() == Goal);

	bool bValidSteps = true;
	for (int32 Index = 1; Index < Path.Num(); ++Index)
	{
		bValidSteps &= Grid.CanStep(Path[Index - 1], Path[Index] - Path[Index - 1]);
	}
	TestTrue(TEXT("Every step is a legal step (no walls, no corner cutting)"), bValidSteps);

	// Shortest: the same cost as the distance map from the goal.
	const FCombatDistanceMap::FSource Sources[] = { { Goal, 0 } };
	FCombatDistanceMap Map;
	Map.Build(Grid, Sources);
	TestEqual(TEXT("Shortest route"), CombatPathfinding::PathCost(Path), Map.GetDistance(Start));

	TArray<FIntPoint> Again;
	CombatPathfinding::FindPath(Grid, Start, Goal, Again);
	TestTrue(TEXT("Deterministic"), Path == Again);

	TArray<FIntPoint> Blocked;
	TestFalse(TEXT("No path into a wall"), CombatPathfinding::FindPath(Grid, Start, FIntPoint(6, 3), Blocked));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatEffectListTest, "BattleSystem.Combat.EffectStacking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatEffectListTest::RunTest(const FString& Parameters)
{
	const FGameplayTagContainer NoTags;

	// Refresh: one effect, duration restarted, newest source.
	FCombatEffectList Refresh;
	Refresh.Apply(CombatTests::MakeTauntEffect(10), 1, 0, 0, NoTags);
	for (int32 Tick = 0; Tick < 6; ++Tick)
	{
		Refresh.Tick();
	}
	Refresh.Apply(CombatTests::MakeTauntEffect(10), 2, 0, 0, NoTags);
	TestEqual(TEXT("Refresh keeps one effect"), Refresh.GetEffects().Num(), 1);
	TestEqual(TEXT("Refresh restarts the duration"), Refresh.GetEffects()[0].RemainingTicks, 10);
	TestEqual(TEXT("Refresh takes the new source"), Refresh.FindSourceOfTag(CombatTags::Status_Taunted), 2);

	for (int32 Tick = 0; Tick < 10; ++Tick)
	{
		Refresh.Tick();
	}
	TestEqual(TEXT("Expired effects are removed"), Refresh.GetEffects().Num(), 0);
	TestFalse(TEXT("Expired effects grant nothing"), Refresh.HasGrantedTag(CombatTags::Status_Taunted));

	// Stack: up to MaxStacks.
	FCombatEffectStats Stacking = CombatTests::MakeTauntEffect(10);
	Stacking.Stacking = ECombatEffectStacking::Stack;
	Stacking.MaxStacks = 2;
	FCombatEffectList Stacks;
	for (int32 Count = 0; Count < 3; ++Count)
	{
		Stacks.Apply(Stacking, 1, 0, 0, NoTags);
	}
	TestEqual(TEXT("Stack is capped at MaxStacks"), Stacks.GetEffects()[0].Stacks, 2);

	// Ignore: the first one stays.
	FCombatEffectStats Ignoring = CombatTests::MakeTauntEffect(10);
	Ignoring.Stacking = ECombatEffectStacking::Ignore;
	FCombatEffectList Ignored;
	Ignored.Apply(Ignoring, 1, 0, 0, NoTags);
	TestFalse(TEXT("Ignore rejects a second application"), Ignored.Apply(Ignoring, 2, 0, 0, NoTags));
	TestEqual(TEXT("Ignore keeps the first source"), Ignored.FindSourceOfTag(CombatTags::Status_Taunted), 1);

	// Blocked by an innate tag.
	FCombatEffectStats Blockable = CombatTests::MakeTauntEffect(10);
	Blockable.BlockedByTags.AddTag(CombatTags::Status);
	FGameplayTagContainer Immune;
	Immune.AddTag(CombatTags::Status_Taunted);
	FCombatEffectList Blocked;
	TestFalse(TEXT("BlockedByTags blocks a unit with a matching tag"), Blocked.Apply(Blockable, 1, 0, 0, Immune));
	TestEqual(TEXT("Nothing applied"), Blocked.GetEffects().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatThreatTargetTest, "BattleSystem.Combat.ThreatRedirectsTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatThreatTargetTest::RunTest(const FString& Parameters)
{
	// U's nearest enemy is the dummy B, but archer C keeps shooting U: U must turn to C.
	FCombatSimConfig Config;
	Config.Grid.Init(20, 12, 100.f);
	Config.MaxFirstAttackDelayTicks = 0;
	CombatTests::AddUnit(Config, CombatTests::MakeStats(1000.f, 1.f, 20, 0), 0, FIntPoint(5, 5));
	CombatTests::AddUnit(Config, CombatTests::MakeDummyStats(), 1, FIntPoint(8, 5));
	FCombatUnitStats Archer = CombatTests::MakeArcherStats(900.f, 1500.f, 14.f);
	Archer.MoveSpeed = 0.f;
	CombatTests::AddUnit(Config, Archer, 1, FIntPoint(5, 11));

	FCombatSimulation Simulation(Config);
	Simulation.Step();
	TestEqual(TEXT("First the nearest enemy"), Simulation.GetUnits()[0].TargetId, 1);

	bool bTurned = false;
	for (int32 Step = 0; Step < 100 && !bTurned; ++Step)
	{
		Simulation.Step();
		const FCombatUnit& Unit = Simulation.GetUnits()[0];
		bTurned = Unit.TargetId == 2 && Unit.TargetReason == ECombatTargetReason::Threat;
	}
	TestTrue(TEXT("Turns to the archer because of threat"), bTurned);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatThreatDecayTest, "BattleSystem.Combat.ThreatDecay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatThreatDecayTest::RunTest(const FString& Parameters)
{
	// Half-life: 40 ticks.
	FCombatSimConfig HalfLife;
	HalfLife.ThreatDecayFactorPerTick = FMath::Pow(0.5f, 1.f / 40.f);
	float Start = 0.f;
	const float AfterHalfLife = CombatTests::MeasureThreatDecay(HalfLife, 40.f, 40, Start);
	TestEqual(TEXT("Half-life: the hit adds threat = damage"), Start, 40.f, 0.5f);
	TestEqual(TEXT("Half-life: halved after one half-life"), AfterHalfLife, Start * 0.5f, 0.05f);

	// Linear: 0.25 per tick.
	FCombatSimConfig Linear;
	Linear.ThreatDecayFactorPerTick = 1.f;
	Linear.ThreatDecayAmountPerTick = 0.25f;
	const float AfterLinear = CombatTests::MeasureThreatDecay(Linear, 40.f, 40, Start);
	TestEqual(TEXT("Linear: 40 ticks x 0.25 subtracted"), AfterLinear, Start - 10.f, 0.01f);
	const float Gone = CombatTests::MeasureThreatDecay(Linear, 40.f, 200, Start);
	TestEqual(TEXT("Linear: removed once it reaches 0"), Gone, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatHysteresisTest, "BattleSystem.Combat.TargetHysteresis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatHysteresisTest::RunTest(const FString& Parameters)
{
	// U stands still with dummy B 3 cells away; walker C approaches U. U may only switch once C is more than the margin closer.
	FCombatSimConfig Config;
	Config.Grid.Init(20, 12, 100.f);
	Config.RetargetDistanceMargin = 150.f;
	CombatTests::AddUnit(Config, CombatTests::MakeDummyStats(), 0, FIntPoint(5, 5));
	CombatTests::AddUnit(Config, CombatTests::MakeDummyStats(), 1, FIntPoint(5, 8));
	FCombatUnitStats Walker = CombatTests::MakeDummyStats();
	Walker.MoveSpeed = 100.f;
	CombatTests::AddUnit(Config, Walker, 1, FIntPoint(5, 0));

	FCombatSimulation Simulation(Config);
	Simulation.Step();
	TestEqual(TEXT("First the nearest enemy B"), Simulation.GetUnits()[0].TargetId, 1);

	bool bSwitched = false;
	bool bSwitchedTooEarly = false;
	for (int32 Step = 0; Step < 200 && !bSwitched; ++Step)
	{
		Simulation.Step();
		const TArray<FCombatUnit>& Units = Simulation.GetUnits();
		if (Units[0].TargetId == 2)
		{
			bSwitched = true;
			// The decision used the positions at the start of this step.
			const double ToB = FVector2D::Distance(Units[0].PreviousPosition, Units[1].PreviousPosition);
			const double ToC = FVector2D::Distance(Units[0].PreviousPosition, Units[2].PreviousPosition);
			bSwitchedTooEarly = ToB <= ToC + Config.RetargetDistanceMargin;
		}
	}
	TestTrue(TEXT("Switches to C once it is clearly closer"), bSwitched);
	TestFalse(TEXT("Not before C is more than the margin closer"), bSwitchedTooEarly);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatTauntTest, "BattleSystem.Combat.TauntPullsEnemy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatTauntTest::RunTest(const FString& Parameters)
{
	// The Brute goes for the archer (nearest); the tank's taunt must pull it to the tank.
	FCombatSimConfig Config;
	Config.Grid.Init(20, 12, 100.f);
	Config.MaxFirstAttackDelayTicks = 0;

	FCombatUnitStats Tank = CombatTests::MakeStats(400.f, 12.f, 24, 6);
	FCombatAttackStats& Taunt = Tank.Attacks.AddDefaulted_GetRef();
	Taunt.Range = 600.f;
	Taunt.Damage = 0.f;
	Taunt.CooldownTicks = 120;
	Taunt.WindupTicks = 4;
	Taunt.bNeedsWalkableLine = false;
	Taunt.bAreaAroundSelf = true;
	Taunt.Effects.Add(CombatTests::MakeTauntEffect(80));

	FCombatUnitStats Brute = CombatTests::MakeStats(400.f, 20.f, 30, 10);
	Brute.MoveSpeed = 220.f;

	CombatTests::AddUnit(Config, Tank, 0, FIntPoint(10, 6));
	CombatTests::AddUnit(Config, CombatTests::MakeArcherStats(600.f, 1500.f, 5.f), 0, FIntPoint(13, 1));
	CombatTests::AddUnit(Config, Brute, 1, FIntPoint(17, 2));

	FCombatSimulation Simulation(Config);
	Simulation.Step();
	TestEqual(TEXT("Without taunt the Brute goes for the archer"), Simulation.GetUnits()[2].TargetId, 1);

	bool bPulled = false;
	for (int32 Step = 0; Step < 200 && !bPulled; ++Step)
	{
		Simulation.Step();
		const FCombatUnit& BruteUnit = Simulation.GetUnits()[2];
		bPulled = BruteUnit.TargetId == 0 && BruteUnit.TargetReason == ECombatTargetReason::Taunt
			&& BruteUnit.Effects.HasGrantedTag(CombatTags::Status_Taunted);
	}
	TestTrue(TEXT("The taunt pulls the Brute to the tank"), bPulled);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
