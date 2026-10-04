// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Combat/CombatBatch.h"
#include "Combat/CombatLevel.h"
#include "Combat/CombatPathfinding.h"
#include "Combat/CombatReplay.h"
#include "Combat/CombatSimulation.h"
#include "Combat/CombatTags.h"
#include "Combat/CombatUnitDefinition.h"
#include "UObject/Package.h"

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
	Taunt.Damage = 0.f;
	Taunt.CooldownTicks = 120;
	Taunt.WindupTicks = 4;
	Taunt.bNeedsWalkableLine = false;
	Taunt.AreaShape = ECombatAreaShape::CircleAroundSelf;
	Taunt.AreaRadius = 600.f;
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

namespace CombatTests
{
	/** An area attack with no windup, a long cooldown and the given shape. */
	static FCombatAttackStats MakeAreaAttack(ECombatAreaShape Shape, float Range, float AreaRadius, float Damage)
	{
		FCombatAttackStats Attack;
		Attack.Range = Range;
		Attack.Damage = Damage;
		Attack.CooldownTicks = 1000;
		Attack.WindupTicks = 0;
		Attack.AreaShape = Shape;
		Attack.AreaRadius = AreaRadius;
		Attack.ConeCosHalfAngle = FMath::Cos(FMath::DegreesToRadians(45.f));
		return Attack;
	}

	static FCombatUnitStats MakeUnitWithAttack(const FCombatAttackStats& Attack)
	{
		FCombatUnitStats Stats = MakeDummyStats();
		Stats.Attacks.Add(Attack);
		return Stats;
	}

	/** Steps until a step has an AreaAttackFired event (or MaxSteps); returns the IDs hit in that step. */
	static TArray<int32> RunUntilAreaFired(FCombatSimulation& Simulation, int32 MaxSteps, int32* OutTick = nullptr)
	{
		TArray<int32> HitIds;
		for (int32 Step = 0; Step < MaxSteps; ++Step)
		{
			Simulation.Step();
			const bool bFired = Simulation.GetEvents().ContainsByPredicate([](const FCombatEvent& Event) { return Event.Type == ECombatEventType::AreaAttackFired; });
			if (bFired)
			{
				for (const FCombatEvent& Event : Simulation.GetEvents())
				{
					if (Event.Type == ECombatEventType::Hit || Event.Type == ECombatEventType::EffectApplied)
					{
						HitIds.AddUnique(Event.TargetId);
					}
				}
				if (OutTick)
				{
					*OutTick = Simulation.GetTick();
				}
				break;
			}
		}
		return HitIds;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatAoECircleTest, "BattleSystem.Combat.AoECircleAtTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatAoECircleTest::RunTest(const FString& Parameters)
{
	FCombatAttackStats Fireball = CombatTests::MakeAreaAttack(ECombatAreaShape::CircleAtTarget, 900.f, 150.f, 10.f);
	Fireball.bNeedsWalkableLine = false;
	Fireball.bNeedsLineOfSight = true;

	FCombatSimConfig Config;
	Config.Grid.Init(20, 12, 100.f);
	Config.MaxFirstAttackDelayTicks = 0;
	CombatTests::AddUnit(Config, CombatTests::MakeUnitWithAttack(Fireball), 0, FIntPoint(5, 5));	// 0: caster
	CombatTests::AddUnit(Config, CombatTests::MakeDummyStats(), 1, FIntPoint(10, 5));				// 1: target (nearest)
	CombatTests::AddUnit(Config, CombatTests::MakeDummyStats(), 1, FIntPoint(11, 5));				// 2: next to it
	CombatTests::AddUnit(Config, CombatTests::MakeDummyStats(), 1, FIntPoint(14, 5));				// 3: far away
	CombatTests::AddUnit(Config, CombatTests::MakeDummyStats(), 0, FIntPoint(10, 6));				// 4: ally in the area

	FCombatSimulation Simulation(Config);
	const TArray<int32> Hit = CombatTests::RunUntilAreaFired(Simulation, 50);
	TestTrue(TEXT("The target is hit"), Hit.Contains(1));
	TestTrue(TEXT("The enemy next to it is hit"), Hit.Contains(2));
	TestFalse(TEXT("The far enemy is not hit"), Hit.Contains(3));
	TestFalse(TEXT("The ally in the area is not hit (no friendly fire)"), Hit.Contains(4));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatAoEConeTest, "BattleSystem.Combat.AoECone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatAoEConeTest::RunTest(const FString& Parameters)
{
	// 90 degree cleave to the right (+X): hits in front, not behind or to the side.
	FCombatSimConfig Config;
	Config.Grid.Init(20, 12, 100.f);
	Config.MaxFirstAttackDelayTicks = 0;
	CombatTests::AddUnit(Config, CombatTests::MakeUnitWithAttack(CombatTests::MakeAreaAttack(ECombatAreaShape::Cone, 100.f, 150.f, 10.f)), 0, FIntPoint(5, 5));
	CombatTests::AddUnit(Config, CombatTests::MakeDummyStats(), 1, FIntPoint(6, 5));	// 1: target, in front
	CombatTests::AddUnit(Config, CombatTests::MakeDummyStats(), 1, FIntPoint(7, 5));	// 2: further in front
	CombatTests::AddUnit(Config, CombatTests::MakeDummyStats(), 1, FIntPoint(4, 5));	// 3: behind
	CombatTests::AddUnit(Config, CombatTests::MakeDummyStats(), 1, FIntPoint(5, 7));	// 4: to the side

	FCombatSimulation Simulation(Config);
	const TArray<int32> Hit = CombatTests::RunUntilAreaFired(Simulation, 50);
	TestTrue(TEXT("The target is hit"), Hit.Contains(1));
	TestTrue(TEXT("The enemy behind the target is hit"), Hit.Contains(2));
	TestFalse(TEXT("The enemy behind the attacker is not hit"), Hit.Contains(3));
	TestFalse(TEXT("The enemy to the side is not hit"), Hit.Contains(4));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatTelegraphTest, "BattleSystem.Combat.AoETelegraph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatTelegraphTest::RunTest(const FString& Parameters)
{
	// The area is placed on the dummy; a walker inside it at cast time walks out before it goes off.
	FCombatAttackStats Fireball = CombatTests::MakeAreaAttack(ECombatAreaShape::CircleAtTarget, 900.f, 100.f, 10.f);
	Fireball.bNeedsWalkableLine = false;
	Fireball.bNeedsLineOfSight = true;
	Fireball.TelegraphTicks = 20;

	FCombatSimConfig Config;
	Config.Grid.Init(20, 12, 100.f);
	Config.MaxFirstAttackDelayTicks = 0;
	CombatTests::AddUnit(Config, CombatTests::MakeUnitWithAttack(Fireball), 0, FIntPoint(5, 5));
	CombatTests::AddUnit(Config, CombatTests::MakeDummyStats(), 1, FIntPoint(10, 5));
	FCombatUnitStats Walker = CombatTests::MakeDummyStats();
	Walker.MoveSpeed = 300.f;
	CombatTests::AddUnit(Config, Walker, 1, FIntPoint(10, 6));

	FCombatSimulation Simulation(Config);
	Simulation.Step();
	TestEqual(TEXT("The area is placed on the first step"), Simulation.GetPendingAreas().Num(), 1);
	const FVector2D PlacedAt = Simulation.GetPendingAreas().Num() > 0 ? Simulation.GetPendingAreas()[0].Area.Center : FVector2D::ZeroVector;
	TestTrue(TEXT("Placed on the dummy"), PlacedAt.Equals(Simulation.GetUnits()[1].PreviousPosition));

	int32 FiredTick = 0;
	const TArray<int32> Hit = CombatTests::RunUntilAreaFired(Simulation, 100, &FiredTick);
	TestEqual(TEXT("Goes off TelegraphTicks after it was placed"), FiredTick, 1 + 20);
	TestTrue(TEXT("The dummy that stayed is hit"), Hit.Contains(1));
	TestFalse(TEXT("The walker that left is not hit"), Hit.Contains(2));
	TestEqual(TEXT("No areas left"), Simulation.GetPendingAreas().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatAllyAuraTest, "BattleSystem.Combat.AoEAllyAura",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatAllyAuraTest::RunTest(const FString& Parameters)
{
	// An allies-only aura around the bearer: the bearer and the ally get the buff, the enemy does not.
	FCombatEffectStats Rally;
	Rally.EffectTag = CombatTags::Effect_Rally;
	Rally.DurationTicks = 60;
	Rally.DamageDealtMultiplier = 1.5f;

	FCombatAttackStats Aura = CombatTests::MakeAreaAttack(ECombatAreaShape::CircleAroundSelf, 0.f, 300.f, 0.f);
	Aura.bNeedsWalkableLine = false;
	Aura.bAffectsEnemies = false;
	Aura.bAffectsAllies = true;
	Aura.Effects.Add(Rally);

	FCombatSimConfig Config;
	Config.Grid.Init(20, 12, 100.f);
	CombatTests::AddUnit(Config, CombatTests::MakeUnitWithAttack(Aura), 0, FIntPoint(5, 5));
	CombatTests::AddUnit(Config, CombatTests::MakeDummyStats(), 0, FIntPoint(6, 5));
	CombatTests::AddUnit(Config, CombatTests::MakeDummyStats(), 1, FIntPoint(5, 7));

	FCombatSimulation Simulation(Config);
	CombatTests::RunUntilAreaFired(Simulation, 20);
	const TArray<FCombatUnit>& Units = Simulation.GetUnits();
	TestEqual(TEXT("The bearer is buffed"), Units[0].Effects.GetDamageDealtMultiplier(), 1.5f);
	TestEqual(TEXT("The ally is buffed"), Units[1].Effects.GetDamageDealtMultiplier(), 1.5f);
	TestEqual(TEXT("The enemy is not"), Units[2].Effects.GetDamageDealtMultiplier(), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatModifiersTest, "BattleSystem.Combat.EffectModifiers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatModifiersTest::RunTest(const FString& Parameters)
{
	// Stacked multipliers multiply.
	FCombatEffectStats Slow;
	Slow.EffectTag = CombatTags::Effect_Slow;
	Slow.DurationTicks = 100;
	Slow.Stacking = ECombatEffectStacking::Stack;
	Slow.MaxStacks = 3;
	Slow.MoveSpeedMultiplier = 0.5f;
	FCombatEffectList List;
	List.Apply(Slow, 1, 0, 0, FGameplayTagContainer());
	List.Apply(Slow, 1, 0, 0, FGameplayTagContainer());
	TestEqual(TEXT("Two stacks of 0.5 = 0.25"), List.GetMoveSpeedMultiplier(), 0.25f);

	// In a fight: a slowing AoE (landing in the first step) halves the walker's speed from the next step on.
	FCombatAttackStats SlowBlast = CombatTests::MakeAreaAttack(ECombatAreaShape::CircleAtTarget, 900.f, 50.f, 0.f);
	SlowBlast.bNeedsWalkableLine = false;
	Slow.Stacking = ECombatEffectStacking::Refresh;
	SlowBlast.Effects.Add(Slow);

	FCombatSimConfig Config;
	Config.Grid.Init(20, 12, 100.f);
	Config.MaxFirstAttackDelayTicks = 0;
	CombatTests::AddUnit(Config, CombatTests::MakeUnitWithAttack(SlowBlast), 0, FIntPoint(2, 5));
	FCombatUnitStats Walker = CombatTests::MakeDummyStats();
	Walker.MoveSpeed = 200.f;
	CombatTests::AddUnit(Config, Walker, 1, FIntPoint(10, 5));

	FCombatSimulation Simulation(Config);
	Simulation.Step();
	const double SlowedStep = FVector2D::Distance(Simulation.GetUnits()[1].PreviousPosition, Simulation.GetUnits()[1].Position);
	Simulation.Step();
	const double NextStep = FVector2D::Distance(Simulation.GetUnits()[1].PreviousPosition, Simulation.GetUnits()[1].Position);
	TestTrue(TEXT("The walker is slowed"), Simulation.GetUnits()[1].Effects.GetMoveSpeedMultiplier() == 0.5f);
	TestEqual(TEXT("Moves 200 * 0.5 / 20 = 5 cm per step"), NextStep, 5.0, 0.01);
	TestTrue(TEXT("The first step was before the slow landed"), SlowedStep > NextStep);

	// Damage dealt and taken: a self-buff (x2 dealt) and a melee hit that leaves the target vulnerable (x1.5 taken).
	FCombatEffectStats Rally;
	Rally.EffectTag = CombatTags::Effect_Rally;
	Rally.DurationTicks = 200;
	Rally.DamageDealtMultiplier = 2.f;
	FCombatAttackStats SelfBuff = CombatTests::MakeAreaAttack(ECombatAreaShape::CircleAroundSelf, 0.f, 10.f, 0.f);
	SelfBuff.bAffectsEnemies = false;
	SelfBuff.bAffectsAllies = true;
	SelfBuff.Effects.Add(Rally);

	FCombatEffectStats Vulnerable;
	Vulnerable.EffectTag = CombatTags::Effect_Slow;
	Vulnerable.DurationTicks = 200;
	Vulnerable.DamageTakenMultiplier = 1.5f;
	FCombatUnitStats Fighter = CombatTests::MakeStats(100.f, 10.f, 5, 0);
	Fighter.Attacks[0].Effects.Add(Vulnerable);
	Fighter.Attacks.Add(SelfBuff);

	FCombatSimConfig DamageConfig;
	DamageConfig.Grid.Init(20, 12, 100.f);
	DamageConfig.MaxFirstAttackDelayTicks = 0;
	CombatTests::AddUnit(DamageConfig, Fighter, 0, FIntPoint(5, 5));
	CombatTests::AddUnit(DamageConfig, CombatTests::MakeDummyStats(), 1, FIntPoint(6, 5));

	FCombatSimulation DamageSimulation(DamageConfig);
	TArray<float> HitAmounts;
	for (int32 Step = 0; Step < 30 && HitAmounts.Num() < 2; ++Step)
	{
		DamageSimulation.Step();
		for (const FCombatEvent& Event : DamageSimulation.GetEvents())
		{
			if (Event.Type == ECombatEventType::Hit && Event.TargetId == 1)
			{
				HitAmounts.Add(Event.Amount);
			}
		}
	}
	TestEqual(TEXT("Two hits"), HitAmounts.Num(), 2);
	if (HitAmounts.Num() == 2)
	{
		TestEqual(TEXT("First hit: 10 x 2 (dealt)"), HitAmounts[0], 20.f);
		TestEqual(TEXT("Second hit: 10 x 2 x 1.5 (target now vulnerable)"), HitAmounts[1], 30.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatReplayRoundTripTest, "BattleSystem.Combat.ReplayRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatReplayRoundTripTest::RunTest(const FString& Parameters)
{
	// Settings with awkward floats (a half-life factor) must survive JSON exactly, or a replay would drift.
	FCombatSimSettings Settings;
	Settings.ThreatDecayFactorPerTick = FMath::Pow(0.5f, 1.f / 80.f);
	Settings.ThreatSwitchRatio = 1.2f;
	Settings.SeparationStrength = 0.37f;
	Settings.MaxFirstAttackDelayTicks = 7;

	FCombatReplay Replay;
	Replay.Seed = 1234;
	Replay.SetupPath = TEXT("/Game/Combat/DA_Setup_Test.DA_Setup_Test");
	Replay.Settings = Settings;
	Replay.FinalChecksum = CombatReplay::ChecksumToString(0xDEADBEEF);

	FString Json;
	TestTrue(TEXT("Writes JSON"), CombatReplay::ToJson(Replay, Json));
	FCombatReplay Loaded;
	TestTrue(TEXT("Reads JSON"), CombatReplay::FromJson(Json, Loaded));
	TestEqual(TEXT("Seed"), Loaded.Seed, 1234);
	TestEqual(TEXT("Setup"), Loaded.SetupPath, Replay.SetupPath);
	TestEqual(TEXT("Checksum text"), Loaded.FinalChecksum, FString(TEXT("0xDEADBEEF")));
	TestTrue(TEXT("Float settings are bit-exact"),
		Loaded.Settings.ThreatDecayFactorPerTick == Settings.ThreatDecayFactorPerTick
		&& Loaded.Settings.ThreatSwitchRatio == Settings.ThreatSwitchRatio
		&& Loaded.Settings.SeparationStrength == Settings.SeparationStrength);

	// The same fight with the original and the loaded settings ends the same.
	auto RunWith = [](const FCombatSimSettings& SimSettings)
	{
		FCombatSimConfig Config = CombatTests::MakeSkirmish(1234);
		SimSettings.ApplyTo(Config);
		FCombatSimulation Simulation(Config);
		Simulation.RunToEnd();
		return Simulation.GetChecksum();
	};
	TestTrue(TEXT("Same checksum after a JSON round trip"), RunWith(Loaded.Settings) == RunWith(Settings));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatBatchTest, "BattleSystem.Combat.BatchStatistics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatBatchTest::RunTest(const FString& Parameters)
{
	const FCombatSimConfig Base = CombatTests::MakeSkirmish(0);
	TArray<FString> Names;
	for (int32 Index = 0; Index < Base.Units.Num(); ++Index)
	{
		Names.Add(Base.Units[Index].Team == 0 ? TEXT("Blue") : TEXT("Red"));
	}

	const FCombatBatchResult Result = CombatBatch::Run(Base, Names, 5, 10);
	TestEqual(TEXT("Five fights"), Result.Fights.Num(), 5);

	int32 Outcomes = Result.Draws + Result.TimeLimits;
	for (const FCombatBatchTeam& Team : Result.Teams)
	{
		Outcomes += Team.Wins;
	}
	TestEqual(TEXT("Wins + draws + time limits = fights"), Outcomes, 5);
	TestEqual(TEXT("Two teams"), Result.Teams.Num(), 2);
	TestEqual(TEXT("Two unit types"), Result.UnitTypes.Num(), 2);
	TestEqual(TEXT("3 units per type per fight"), Result.UnitTypes.Num() == 2 ? Result.UnitTypes[0].Units : 0, 15);

	// Every fight in the batch is the same as running that seed on its own.
	bool bSameAsSingle = true;
	for (const FCombatBatchFight& Fight : Result.Fights)
	{
		FCombatSimConfig Config = Base;
		Config.Seed = Fight.Seed;
		FCombatSimulation Simulation(Config);
		Simulation.RunToEnd();
		bSameAsSingle &= Simulation.GetChecksum() == Fight.Checksum;
	}
	TestTrue(TEXT("Batch fights match single runs"), bSameAsSingle);
	TestEqual(TEXT("Seeds count up from the start seed"), Result.Fights[4].Seed, 14);
	return true;
}

namespace CombatTests
{
	static FCombatCommand MakeMove(int32 Tick, int32 UnitId, FIntPoint Cell)
	{
		FCombatCommand Command;
		Command.Tick = Tick;
		Command.UnitId = UnitId;
		Command.Type = ECombatCommandType::Move;
		Command.TargetCell = Cell;
		return Command;
	}

	static FCombatCommand MakeAbility(int32 Tick, int32 UnitId, int32 AbilityIndex)
	{
		FCombatCommand Command;
		Command.Tick = Tick;
		Command.UnitId = UnitId;
		Command.Type = ECombatCommandType::Ability;
		Command.AbilityIndex = AbilityIndex;
		return Command;
	}

	/** A taunt around the unit (no damage) with a long cooldown, as an AI attack or a player ability. */
	static FCombatAttackStats MakeTauntArea(float Radius, int32 DurationTicks)
	{
		FCombatAttackStats Taunt = MakeAreaAttack(ECombatAreaShape::CircleAroundSelf, 0.f, Radius, 0.f);
		Taunt.bNeedsWalkableLine = false;
		Taunt.Effects.Add(MakeTauntEffect(DurationTicks));
		return Taunt;
	}

	static int32 CountEvents(const FCombatSimulation& Simulation, ECombatEventType Type, int32 SourceId)
	{
		int32 Count = 0;
		for (const FCombatEvent& Event : Simulation.GetEvents())
		{
			Count += Event.Type == Type && Event.SourceId == SourceId ? 1 : 0;
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatMoveCommandTest, "BattleSystem.Combat.CommandMove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatMoveCommandTest::RunTest(const FString& Parameters)
{
	// A fighter next to an enemy is ordered away: no attacks on the way, arrives, then the AI fights again.
	FCombatSimConfig Config;
	Config.Grid.Init(20, 12, 100.f);
	Config.MaxFirstAttackDelayTicks = 0;
	CombatTests::AddUnit(Config, CombatTests::MakeStats(100.f, 10.f, 10, 0), 0, FIntPoint(2, 5));
	CombatTests::AddUnit(Config, CombatTests::MakeDummyStats(), 1, FIntPoint(3, 5));
	Config.Commands.Add(CombatTests::MakeMove(1, 0, FIntPoint(2, 10)));

	FCombatSimulation Simulation(Config);
	bool bAttackedWhileMoving = false;
	int32 ArrivedTick = INDEX_NONE;
	for (int32 Step = 0; Step < 100 && ArrivedTick == INDEX_NONE; ++Step)
	{
		Simulation.Step();
		bAttackedWhileMoving |= CombatTests::CountEvents(Simulation, ECombatEventType::Attack, 0) > 0;
		if (!Simulation.GetUnits()[0].bHasMoveOrder)
		{
			ArrivedTick = Simulation.GetTick();
		}
	}

	TestFalse(TEXT("No attacks while moving"), bAttackedWhileMoving);
	TestTrue(TEXT("Arrives"), ArrivedTick != INDEX_NONE);
	TestTrue(TEXT("At the target cell"), Config.Grid.LocalToCell(Simulation.GetUnits()[0].Position) == FIntPoint(2, 10));

	bool bFightsAgain = false;
	for (int32 Step = 0; Step < 200 && !bFightsAgain; ++Step)
	{
		Simulation.Step();
		bFightsAgain = CombatTests::CountEvents(Simulation, ECombatEventType::Attack, 0) > 0;
	}
	TestTrue(TEXT("The AI takes over again after arrival"), bFightsAgain);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatMoveOverridesTauntTest, "BattleSystem.Combat.CommandMoveOverridesTaunt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatMoveOverridesTauntTest::RunTest(const FString& Parameters)
{
	FCombatSimConfig Config;
	Config.Grid.Init(20, 12, 100.f);
	Config.MaxFirstAttackDelayTicks = 0;
	CombatTests::AddUnit(Config, CombatTests::MakeStats(100.f, 1.f, 20, 0), 0, FIntPoint(5, 5));
	CombatTests::AddUnit(Config, CombatTests::MakeUnitWithAttack(CombatTests::MakeTauntArea(600.f, 200)), 1, FIntPoint(8, 5));
	Config.Commands.Add(CombatTests::MakeMove(3, 0, FIntPoint(1, 1)));

	FCombatSimulation Simulation(Config);
	for (int32 Step = 0; Step < 2; ++Step)
	{
		Simulation.Step();
	}
	TestTrue(TEXT("Taunted first"), Simulation.GetUnits()[0].Effects.HasGrantedTag(CombatTags::Status_Taunted));

	const double StartDistance = FVector2D::Distance(Simulation.GetUnits()[0].Position, Simulation.GetUnits()[1].Position);
	bool bTargetedTaunter = false;
	for (int32 Step = 0; Step < 20; ++Step)
	{
		Simulation.Step();
		const FCombatUnit& Unit = Simulation.GetUnits()[0];
		bTargetedTaunter |= Unit.bHasMoveOrder && Unit.TargetId == 1;
	}
	const double EndDistance = FVector2D::Distance(Simulation.GetUnits()[0].Position, Simulation.GetUnits()[1].Position);

	TestTrue(TEXT("Still taunted"), Simulation.GetUnits()[0].Effects.HasGrantedTag(CombatTags::Status_Taunted));
	TestFalse(TEXT("Does not target the taunter while ordered to move"), bTargetedTaunter);
	TestTrue(TEXT("Walks away from the taunter"), EndDistance > StartDistance + 100.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatPlayerAbilityTest, "BattleSystem.Combat.CommandPlayerAbility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatPlayerAbilityTest::RunTest(const FString& Parameters)
{
	// A player taunt has no cooldown: two commands in two ticks both go off. The AI never uses it itself.
	FCombatUnitStats Shouter = CombatTests::MakeDummyStats();
	Shouter.PlayerAbilities.Add(CombatTests::MakeTauntArea(300.f, 60));

	FCombatSimConfig Config;
	Config.Grid.Init(20, 12, 100.f);
	CombatTests::AddUnit(Config, Shouter, 0, FIntPoint(5, 5));
	CombatTests::AddUnit(Config, CombatTests::MakeDummyStats(), 1, FIntPoint(7, 5));
	Config.Commands.Add(CombatTests::MakeAbility(10, 0, 0));
	Config.Commands.Add(CombatTests::MakeAbility(11, 0, 0));

	FCombatSimulation Simulation(Config);
	int32 Executed = 0;
	int32 Fired = 0;
	for (int32 Step = 0; Step < 12; ++Step)
	{
		Simulation.Step();
		if (Simulation.GetTick() < 10)
		{
			TestEqual(TEXT("The AI does not use a player ability"), CombatTests::CountEvents(Simulation, ECombatEventType::AreaAttackFired, 0), 0);
		}
		Executed += CombatTests::CountEvents(Simulation, ECombatEventType::CommandExecuted, 0);
		Fired += CombatTests::CountEvents(Simulation, ECombatEventType::AreaAttackFired, 0);
	}

	TestEqual(TEXT("Both commands run"), Executed, 2);
	TestEqual(TEXT("Both taunts go off (no cooldown)"), Fired, 2);
	TestEqual(TEXT("The enemy is taunted by the shouter"), Simulation.GetUnits()[1].Effects.FindSourceOfTag(CombatTags::Status_Taunted), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatRejectedCommandTest, "BattleSystem.Combat.CommandRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatRejectedCommandTest::RunTest(const FString& Parameters)
{
	FCombatSimConfig Config;
	Config.Grid = CombatTests::MakeWallGrid();
	CombatTests::AddUnit(Config, CombatTests::MakeDummyStats(), 0, FIntPoint(3, 2));
	CombatTests::AddUnit(Config, CombatTests::MakeDummyStats(), 1, FIntPoint(15, 2));
	Config.Commands.Add(CombatTests::MakeMove(2, 0, FIntPoint(6, 3)));	// into the wall
	Config.Commands.Add(CombatTests::MakeAbility(2, 0, 0));				// no such ability

	FCombatSimulation Simulation(Config);
	Simulation.Step();
	TestFalse(TEXT("A command for a tick that already ran is refused"), Simulation.QueueCommand(CombatTests::MakeMove(1, 0, FIntPoint(2, 2))));
	Simulation.Step();

	TestEqual(TEXT("Both commands are rejected"), CombatTests::CountEvents(Simulation, ECombatEventType::CommandRejected, 0), 2);
	TestFalse(TEXT("No move order"), Simulation.GetUnits()[0].bHasMoveOrder);
	TestEqual(TEXT("Rejected commands stay in the log"), Simulation.GetCommandLog().Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatCommandReplayTest, "BattleSystem.Combat.CommandsReplayIdentically",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatCommandReplayTest::RunTest(const FString& Parameters)
{
	// Record: commands given live during the fight (as the player would, 3 ticks ahead).
	FCombatSimulation Recorded(CombatTests::MakeSkirmish(42));
	TArray<uint32> RecordedChecksums;
	while (!Recorded.IsFinished())
	{
		const int32 Now = Recorded.GetTick();
		if (Now == 20)
		{
			Recorded.QueueCommand(CombatTests::MakeMove(Now + 3, 0, FIntPoint(2, 0)));
		}
		if (Now == 25)
		{
			Recorded.QueueCommand(CombatTests::MakeMove(Now + 3, 2, FIntPoint(4, 11)));
		}
		Recorded.Step();
		RecordedChecksums.Add(Recorded.GetChecksum());
	}

	// Replay: the same commands from the log, known in advance.
	FCombatSimConfig ReplayConfig = CombatTests::MakeSkirmish(42);
	ReplayConfig.Commands = Recorded.GetCommandLog();
	const TArray<uint32> ReplayChecksums = CombatTests::RunAndCollectChecksums(ReplayConfig);
	TestEqual(TEXT("Two commands in the log"), Recorded.GetCommandLog().Num(), 2);
	TestTrue(TEXT("The replay has the same checksum after every step"), RecordedChecksums == ReplayChecksums);

	// Without commands the fight is different (and identical to itself, as before).
	const TArray<uint32> WithoutCommands = CombatTests::RunAndCollectChecksums(CombatTests::MakeSkirmish(42));
	TestFalse(TEXT("Commands change the fight"), WithoutCommands == RecordedChecksums);

	// The command log survives a JSON replay round trip.
	FCombatReplay Replay;
	Replay.Commands = Recorded.GetCommandLog();
	FString Json;
	CombatReplay::ToJson(Replay, Json);
	FCombatReplay Loaded;
	CombatReplay::FromJson(Json, Loaded);
	bool bSameCommands = Loaded.Commands.Num() == Replay.Commands.Num();
	for (int32 Index = 0; bSameCommands && Index < Replay.Commands.Num(); ++Index)
	{
		const FCombatCommand& A = Replay.Commands[Index];
		const FCombatCommand& B = Loaded.Commands[Index];
		bSameCommands = A.Tick == B.Tick && A.UnitId == B.UnitId && A.Type == B.Type && A.TargetCell == B.TargetCell && A.AbilityIndex == B.AbilityIndex;
	}
	TestTrue(TEXT("Commands survive JSON"), bSameCommands);
	return true;
}

namespace CombatTests
{
	static FCombatLevelUnit MakeLevelUnit(const FString& Type, int32 Team, FIntPoint Cell)
	{
		FCombatLevelUnit Unit;
		Unit.Type = Type;
		Unit.Team = Team;
		Unit.Cell = Cell;
		return Unit;
	}

	/** A level with every cell kind and three units, for the level tests. */
	static FCombatLevel MakeTestLevel()
	{
		FCombatLevel Level = FCombatLevel::MakeEmpty(TEXT("Test"), 12, 8);
		Level.SetCell(FIntPoint(5, 1), FCombatLevel::Wall);
		Level.SetCell(FIntPoint(5, 2), FCombatLevel::Hedge);
		Level.SetCell(FIntPoint(5, 3), FCombatLevel::Water);
		Level.Units.Add(MakeLevelUnit(TEXT("Fighter"), 0, FIntPoint(1, 1)));
		Level.Units.Add(MakeLevelUnit(TEXT("Fighter"), 1, FIntPoint(10, 6)));
		Level.Units.Add(MakeLevelUnit(TEXT("Fighter"), 1, FIntPoint(5, 2)));	// on the hedge: allowed
		return Level;
	}

	/** A melee definition made in code (not a project asset), for building configs from levels. */
	static const UCombatUnitDefinition* MakeTestDefinition()
	{
		UCombatUnitDefinition* Definition = NewObject<UCombatUnitDefinition>(GetTransientPackage());
		FCombatAttackDefinition& Melee = Definition->Attacks.AddDefaulted_GetRef();
		Melee.Type = CombatTags::Attack_Melee;
		return Definition;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatLevelFormatTest, "BattleSystem.Combat.LevelFormat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatLevelFormatTest::RunTest(const FString& Parameters)
{
	FCombatLevel Level = CombatTests::MakeTestLevel();

	FCombatGridData Grid;
	Level.ToGridData(Grid);
	TestEqual(TEXT("Grid size"), Grid.Width * 100 + Grid.Height, 12 * 100 + 8);
	TestTrue(TEXT("Wall blocks walking and sight"), !Grid.IsWalkable(FIntPoint(5, 1)) && Grid.BlocksSight(FIntPoint(5, 1)));
	TestTrue(TEXT("Hedge blocks sight only"), Grid.IsWalkable(FIntPoint(5, 2)) && Grid.BlocksSight(FIntPoint(5, 2)));
	TestTrue(TEXT("Water blocks walking only"), !Grid.IsWalkable(FIntPoint(5, 3)) && !Grid.BlocksSight(FIntPoint(5, 3)));
	TestTrue(TEXT("Open cells are open"), Grid.IsWalkable(FIntPoint(0, 0)) && !Grid.BlocksSight(FIntPoint(0, 0)));

	FString Json;
	TestTrue(TEXT("Writes JSON"), CombatLevels::ToJson(Level, Json));
	FCombatLevel Loaded;
	TestTrue(TEXT("Reads JSON"), CombatLevels::FromJson(Json, Loaded));
	TestTrue(TEXT("Rows survive"), Loaded.Rows == Level.Rows);
	TestEqual(TEXT("Units survive"), Loaded.Units.Num(), 3);
	TestTrue(TEXT("Unit cell survives"), Loaded.Units.Num() == 3 && Loaded.Units[1].Cell == FIntPoint(10, 6) && Loaded.Units[1].Team == 1);

	Level.Resize(8, 8);
	TestEqual(TEXT("Shrinking removes units outside"), Level.Units.Num(), 2);
	TestEqual(TEXT("Rows are cut to the new width"), Level.Rows[0].Len(), 8);
	Level.Resize(2, 100);
	TestTrue(TEXT("Size is clamped"), Level.Width == FCombatLevel::MinSize && Level.Height == FCombatLevel::MaxSize);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatLevelConfigTest, "BattleSystem.Combat.LevelToConfig",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatLevelConfigTest::RunTest(const FString& Parameters)
{
	const UCombatUnitDefinition* Fighter = CombatTests::MakeTestDefinition();
	FCombatLevel Level = CombatTests::MakeTestLevel();
	Level.Units.Add(CombatTests::MakeLevelUnit(TEXT("Fighter"), 1, FIntPoint(5, 1)));		// on the wall: skipped
	Level.Units.Add(CombatTests::MakeLevelUnit(TEXT("Unknown"), 1, FIntPoint(9, 1)));		// unknown type: skipped
	auto Resolve = [Fighter](const FString& Type) { return Type == TEXT("Fighter") ? Fighter : nullptr; };

	FCombatSimConfig Config;
	TArray<const UCombatUnitDefinition*> Definitions;
	TestTrue(TEXT("Builds"), CombatLevels::BuildConfig(Level, 20, Resolve, Config, &Definitions));
	TestEqual(TEXT("Three valid units"), Config.Units.Num(), 3);
	TestEqual(TEXT("A definition per unit"), Definitions.Num(), 3);
	TestTrue(TEXT("The grid comes from the level"), Config.Grid.Width == 12 && !Config.Grid.IsWalkable(FIntPoint(5, 1)));

	// A fight from a level is deterministic like any other.
	Config.Seed = 5;
	FCombatSimulation First(Config);
	First.RunToEnd();
	FCombatSimulation Second(Config);
	Second.RunToEnd();
	TestTrue(TEXT("Same level and seed, same checksum"), First.GetChecksum() == Second.GetChecksum());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatLevelReplayTest, "BattleSystem.Combat.ReplayWithLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatLevelReplayTest::RunTest(const FString& Parameters)
{
	// The replay holds a full copy of the level, so the fight can be rebuilt from the replay alone.
	FCombatReplay Replay;
	Replay.bHasLevel = true;
	Replay.Level = CombatTests::MakeTestLevel();
	Replay.Seed = 9;

	FString Json;
	CombatReplay::ToJson(Replay, Json);
	FCombatReplay Loaded;
	TestTrue(TEXT("Reads JSON"), CombatReplay::FromJson(Json, Loaded));
	TestTrue(TEXT("Has the level"), Loaded.bHasLevel);
	TestTrue(TEXT("Same rows"), Loaded.Level.Rows == Replay.Level.Rows);
	TestEqual(TEXT("Same units"), Loaded.Level.Units.Num(), Replay.Level.Units.Num());

	const UCombatUnitDefinition* Fighter = CombatTests::MakeTestDefinition();
	auto Resolve = [Fighter](const FString& Type) { return Type == TEXT("Fighter") ? Fighter : nullptr; };
	auto RunLevel = [&Resolve](const FCombatLevel& Level, int32 Seed)
	{
		FCombatSimConfig Config;
		CombatLevels::BuildConfig(Level, 20, Resolve, Config);
		Config.Seed = Seed;
		FCombatSimulation Simulation(Config);
		Simulation.RunToEnd();
		return Simulation.GetChecksum();
	};
	TestTrue(TEXT("Same fight from the replay's copy"), RunLevel(Loaded.Level, Loaded.Seed) == RunLevel(Replay.Level, Replay.Seed));
	return true;
}

namespace CombatTests
{
	static void AddWaveSpawn(FCombatSimConfig& Config, int32 WaveIndex, const FCombatUnitStats& Stats, FIntPoint Cell, int32 DelayTicks)
	{
		while (Config.Waves.Num() <= WaveIndex)
		{
			Config.Waves.AddDefaulted();
		}
		FCombatWaveSpawn& Spawn = Config.Waves[WaveIndex].Spawns.AddDefaulted_GetRef();
		Spawn.Stats = Stats;
		Spawn.Cell = Cell;
		Spawn.DelayTicks = DelayTicks;
	}

	static FCombatCommand MakeCallWave(int32 Tick)
	{
		FCombatCommand Command;
		Command.Tick = Tick;
		Command.UnitId = INDEX_NONE;
		Command.Type = ECombatCommandType::CallWave;
		return Command;
	}

	static FCombatLevelSpawn MakeLevelSpawn(const FString& Type, FIntPoint Cell, float Time)
	{
		FCombatLevelSpawn Spawn;
		Spawn.Type = Type;
		Spawn.Cell = Cell;
		Spawn.Time = Time;
		return Spawn;
	}

	/** A hero (team 0) that kills a 10 HP enemy in one hit, against waves of one such enemy each next to it. */
	static FCombatSimConfig MakeClearableWaves(int32 WaveCount, int32 PauseTicks)
	{
		FCombatSimConfig Config;
		Config.Grid.Init(20, 12, 100.f);
		Config.MaxFirstAttackDelayTicks = 0;
		Config.WavePauseTicks = PauseTicks;
		AddUnit(Config, MakeStats(1000.f, 100.f, 5, 0), 0, FIntPoint(2, 5));
		for (int32 Wave = 0; Wave < WaveCount; ++Wave)
		{
			AddWaveSpawn(Config, Wave, MakeStats(10.f, 1.f, 20, 0), FIntPoint(4, 5), 0);
		}
		return Config;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatWaveTimingTest, "BattleSystem.Combat.WaveSpawnTiming",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatWaveTimingTest::RunTest(const FString& Parameters)
{
	// Only a hero at the start: the fight waits for the wave instead of ending at once.
	FCombatSimConfig Config;
	Config.Grid.Init(20, 12, 100.f);
	Config.WavePauseTicks = 20;
	CombatTests::AddUnit(Config, CombatTests::MakeDummyStats(), 0, FIntPoint(2, 5));
	CombatTests::AddWaveSpawn(Config, 0, CombatTests::MakeDummyStats(), FIntPoint(10, 5), 0);
	CombatTests::AddWaveSpawn(Config, 0, CombatTests::MakeDummyStats(), FIntPoint(12, 5), 10);

	FCombatSimulation Simulation(Config);
	int32 WaveStartTick = INDEX_NONE;
	TArray<int32> SpawnTicks;
	for (int32 Step = 0; Step < 40; ++Step)
	{
		Simulation.Step();
		for (const FCombatEvent& Event : Simulation.GetEvents())
		{
			if (Event.Type == ECombatEventType::WaveStarted && Event.WaveIndex == 0)
			{
				WaveStartTick = Simulation.GetTick();
			}
			if (Event.Type == ECombatEventType::UnitSpawned)
			{
				SpawnTicks.Add(Simulation.GetTick());
			}
		}
		if (Simulation.GetTick() < 20)
		{
			TestFalse(TEXT("Not over while a wave is to come"), Simulation.IsFinished());
		}
	}

	TestEqual(TEXT("The first wave starts after the pause"), WaveStartTick, 20);
	TestTrue(TEXT("Spawns at wave start + delay"), SpawnTicks == TArray<int32>({ 20, 30 }));
	TestEqual(TEXT("Three units"), Simulation.GetUnits().Num(), 3);
	if (Simulation.GetUnits().Num() == 3)
	{
		const FCombatUnit& Spawned = Simulation.GetUnits()[2];
		TestEqual(TEXT("Spawned on the wave team"), Spawned.Team, 1);
		TestTrue(TEXT("Spawned on its cell"), Config.Grid.LocalToCell(Spawned.Position) == FIntPoint(12, 5));
		TestEqual(TEXT("SourceIndex after the config units"), Spawned.SourceIndex, 2);
	}
	TestFalse(TEXT("Still running (nobody can attack)"), Simulation.IsFinished());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatWaveClearTest, "BattleSystem.Combat.WaveWaitsForClear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatWaveClearTest::RunTest(const FString& Parameters)
{
	const FCombatSimConfig Config = CombatTests::MakeClearableWaves(2, 20);
	FCombatSimulation Simulation(Config);

	TArray<int32> StartTicks;
	TArray<int32> DeathTicks;
	bool bSecondWhileFirstAlive = false;
	while (!Simulation.IsFinished() && Simulation.GetTick() < 500)
	{
		Simulation.Step();
		for (const FCombatEvent& Event : Simulation.GetEvents())
		{
			if (Event.Type == ECombatEventType::WaveStarted)
			{
				StartTicks.Add(Simulation.GetTick());
				bSecondWhileFirstAlive |= Event.WaveIndex == 1 && Simulation.GetUnits()[1].bAlive;
			}
			if (Event.Type == ECombatEventType::Death)
			{
				DeathTicks.Add(Simulation.GetTick());
			}
		}
	}

	TestEqual(TEXT("Both waves started"), StartTicks.Num(), 2);
	TestEqual(TEXT("Both enemies died"), DeathTicks.Num(), 2);
	TestFalse(TEXT("Wave 2 waits until wave 1 is dead"), bSecondWhileFirstAlive);
	if (StartTicks.Num() == 2 && DeathTicks.Num() == 2)
	{
		TestEqual(TEXT("Wave 2 starts a pause after the clear"), StartTicks[1], DeathTicks[0] + 20);
	}
	TestTrue(TEXT("The hero wins after the last wave"), Simulation.GetOutcome() == ECombatOutcome::TeamWon && Simulation.GetWinningTeam() == 0);
	TestEqual(TEXT("Not before the last enemy died"), Simulation.GetTick(), DeathTicks.IsEmpty() ? -1 : DeathTicks.Last());

	// Batch statistics count spawned units by their source (definition) index.
	const TArray<FString> TypeNames = { TEXT("Hero"), TEXT("Grunt"), TEXT("Grunt") };
	const FCombatBatchResult Result = CombatBatch::Run(Config, TypeNames, 2, 1);
	const FCombatBatchUnitType* Grunts = Result.UnitTypes.FindByPredicate([](const FCombatBatchUnitType& Type) { return Type.Name == TEXT("Grunt"); });
	TestTrue(TEXT("Two spawned grunts per fight"), Grunts && Grunts->Units == 4);
	TestEqual(TEXT("Both teams in the report"), Result.Teams.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatWaveCallTest, "BattleSystem.Combat.WaveCallEarly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatWaveCallTest::RunTest(const FString& Parameters)
{
	// Nobody can attack, so only calls start the waves (the pause is long).
	FCombatSimConfig Config;
	Config.Grid.Init(20, 12, 100.f);
	Config.WavePauseTicks = 1000;
	CombatTests::AddUnit(Config, CombatTests::MakeDummyStats(), 0, FIntPoint(2, 5));
	for (int32 Wave = 0; Wave < 3; ++Wave)
	{
		CombatTests::AddWaveSpawn(Config, Wave, CombatTests::MakeDummyStats(), FIntPoint(10, 2 + Wave * 3), 0);
	}
	Config.Commands.Add(CombatTests::MakeCallWave(5));
	Config.Commands.Add(CombatTests::MakeCallWave(8));	// wave 1 still alive: overlaps
	Config.Commands.Add(CombatTests::MakeCallWave(9));
	Config.Commands.Add(CombatTests::MakeCallWave(10));	// no wave left: rejected

	FCombatSimulation Simulation(Config);
	TArray<int32> StartTicks;
	int32 Rejected = 0;
	for (int32 Step = 0; Step < 12; ++Step)
	{
		Simulation.Step();
		for (const FCombatEvent& Event : Simulation.GetEvents())
		{
			if (Event.Type == ECombatEventType::WaveStarted)
			{
				StartTicks.Add(Simulation.GetTick());
			}
		}
		Rejected += CombatTests::CountEvents(Simulation, ECombatEventType::CommandRejected, INDEX_NONE);
		if (Simulation.GetTick() == 5)
		{
			TestEqual(TEXT("A called wave's units appear in the same step"), Simulation.GetUnits().Num(), 2);
			TestEqual(TEXT("After a call the next wave waits for a clear"), Simulation.GetNextWaveTick(), static_cast<int32>(INDEX_NONE));
		}
	}

	TestTrue(TEXT("Waves start at the calls"), StartTicks == TArray<int32>({ 5, 8, 9 }));
	TestEqual(TEXT("All three waves started"), Simulation.GetWavesStarted(), 3);
	TestEqual(TEXT("The call after the last wave is rejected"), Rejected, 1);
	TestEqual(TEXT("All enemies are there at once"), Simulation.GetUnits().Num(), 4);
	TestFalse(TEXT("No waves left"), Simulation.HasWavesLeft());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatWaveOutcomeTest, "BattleSystem.Combat.WaveWinCondition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatWaveOutcomeTest::RunTest(const FString& Parameters)
{
	// The heroes die while a wave is still to come: the wave team wins.
	FCombatSimConfig Config;
	Config.Grid.Init(20, 12, 100.f);
	Config.MaxFirstAttackDelayTicks = 0;
	Config.WavePauseTicks = 1;
	CombatTests::AddUnit(Config, CombatTests::MakeStats(10.f, 1.f, 20, 0), 0, FIntPoint(2, 5));
	CombatTests::AddWaveSpawn(Config, 0, CombatTests::MakeStats(1000.f, 100.f, 5, 0), FIntPoint(4, 5), 0);
	CombatTests::AddWaveSpawn(Config, 1, CombatTests::MakeDummyStats(), FIntPoint(10, 5), 0);

	FCombatSimulation Simulation(Config);
	Simulation.RunToEnd();
	TestTrue(TEXT("The wave team wins"), Simulation.GetOutcome() == ECombatOutcome::TeamWon && Simulation.GetWinningTeam() == 1);
	TestTrue(TEXT("Wave 2 never started"), Simulation.GetWavesStarted() == 1 && Simulation.HasWavesLeft());

	// Every enemy of every wave dead: the heroes win, but only after the last wave.
	FCombatSimulation Cleared(CombatTests::MakeClearableWaves(3, 10));
	Cleared.RunToEnd();
	TestTrue(TEXT("The heroes win"), Cleared.GetOutcome() == ECombatOutcome::TeamWon && Cleared.GetWinningTeam() == 0);
	TestEqual(TEXT("After all waves"), Cleared.GetWavesStarted(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatWaveLevelTest, "BattleSystem.Combat.WaveLevelFormat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatWaveLevelTest::RunTest(const FString& Parameters)
{
	FCombatLevel Level = CombatTests::MakeTestLevel();
	Level.Waves.AddDefaulted(3);
	Level.Waves[0].Spawns.Add(CombatTests::MakeLevelSpawn(TEXT("Fighter"), FIntPoint(9, 1), 1.5f));
	Level.Waves[0].Spawns.Add(CombatTests::MakeLevelSpawn(TEXT("Fighter"), FIntPoint(5, 1), 0.f));	// on the wall: skipped
	Level.Waves[0].Spawns.Add(CombatTests::MakeLevelSpawn(TEXT("Unknown"), FIntPoint(9, 2), 0.f));	// unknown type: skipped
	Level.Waves[2].Spawns.Add(CombatTests::MakeLevelSpawn(TEXT("Fighter"), FIntPoint(11, 7), 0.f));	// wave 2 stays empty

	FString Json;
	TestTrue(TEXT("Writes JSON"), CombatLevels::ToJson(Level, Json));
	TestTrue(TEXT("JSON has waves"), Json.Contains(TEXT("\"waves\"")));
	FCombatLevel Loaded;
	TestTrue(TEXT("Reads JSON"), CombatLevels::FromJson(Json, Loaded));
	TestEqual(TEXT("Waves survive"), Loaded.Waves.Num(), 3);
	TestTrue(TEXT("A spawn survives"), Loaded.Waves.Num() == 3 && Loaded.Waves[0].Spawns.Num() == 3
		&& Loaded.Waves[0].Spawns[0].Cell == FIntPoint(9, 1) && Loaded.Waves[0].Spawns[0].Time == 1.5f);

	// A version 1 file (no waves) still loads.
	FCombatLevel Old;
	TestTrue(TEXT("Reads a version 1 level"), CombatLevels::FromJson(TEXT("{\"formatVersion\":1,\"name\":\"Old\",\"width\":6,\"height\":5,\"rows\":[],\"units\":[]}"), Old));
	TestTrue(TEXT("Without waves, at the current version"), Old.Waves.IsEmpty() && Old.FormatVersion == FCombatLevel().FormatVersion);

	const UCombatUnitDefinition* Fighter = CombatTests::MakeTestDefinition();
	auto Resolve = [Fighter](const FString& Type) { return Type == TEXT("Fighter") ? Fighter : nullptr; };
	FCombatSimConfig Config;
	TArray<const UCombatUnitDefinition*> Definitions;
	TestTrue(TEXT("Builds"), CombatLevels::BuildConfig(Level, 20, Resolve, Config, &Definitions));
	TestEqual(TEXT("Every wave is kept"), Config.Waves.Num(), 3);
	TestTrue(TEXT("Invalid spawns are skipped"), Config.Waves.Num() == 3 && Config.Waves[0].Spawns.Num() == 1 && Config.Waves[1].Spawns.IsEmpty());
	TestTrue(TEXT("Seconds become ticks"), Config.Waves.Num() == 3 && Config.Waves[0].Spawns.Num() == 1 && Config.Waves[0].Spawns[0].DelayTicks == 30);
	TestEqual(TEXT("A definition per unit and valid spawn"), Definitions.Num(), 3 + 2);

	Level.Resize(10, 8);
	TestTrue(TEXT("Shrinking removes spawns outside"), Level.Waves[0].Spawns.Num() == 3 && Level.Waves[2].Spawns.IsEmpty());
	TestTrue(TEXT("RemoveSpawnsAt removes from every wave"), Level.RemoveSpawnsAt(FIntPoint(9, 1)) && Level.FindSpawnAt(0, FIntPoint(9, 1)) == INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatWaveReplayTest, "BattleSystem.Combat.WaveReplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatWaveReplayTest::RunTest(const FString& Parameters)
{
	// Record: a wave called live during the fight, right after the first wave started.
	const FCombatSimConfig Base = CombatTests::MakeClearableWaves(3, 40);
	FCombatSimulation Recorded(Base);
	TArray<uint32> RecordedChecksums;
	while (!Recorded.IsFinished())
	{
		if (Recorded.GetTick() == 41)
		{
			Recorded.QueueCommand(CombatTests::MakeCallWave(Recorded.GetTick() + 3));
		}
		Recorded.Step();
		RecordedChecksums.Add(Recorded.GetChecksum());
	}

	// Replay: the command log known in advance, after a JSON round trip.
	FCombatReplay Replay;
	Replay.Commands = Recorded.GetCommandLog();
	FString Json;
	CombatReplay::ToJson(Replay, Json);
	FCombatReplay Loaded;
	CombatReplay::FromJson(Json, Loaded);
	TestTrue(TEXT("CallWave survives JSON"), Loaded.Commands.Num() == 1 && Loaded.Commands[0].Type == ECombatCommandType::CallWave);

	FCombatSimConfig ReplayConfig = Base;
	ReplayConfig.Commands = Loaded.Commands;
	TestTrue(TEXT("The replay has the same checksum after every step"), CombatTests::RunAndCollectChecksums(ReplayConfig) == RecordedChecksums);
	TestFalse(TEXT("The call changes the fight"), CombatTests::RunAndCollectChecksums(Base) == RecordedChecksums);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
