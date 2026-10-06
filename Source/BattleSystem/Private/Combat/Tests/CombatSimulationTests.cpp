// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Animation/AnimMontage.h"
#include "Combat/CombatAnimation.h"
#include "Combat/CombatAppearance.h"
#include "Combat/CombatBatch.h"
#include "Combat/CombatCamera.h"
#include "Combat/CombatLevel.h"
#include "Combat/CombatNavigation.h"
#include "Combat/CombatPathfinding.h"
#include "Combat/CombatPieces.h"
#include "Combat/CombatReplay.h"
#include "Combat/CombatSimulation.h"
#include "Combat/CombatTags.h"
#include "Combat/CombatUnitDefinition.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/FileHelper.h"
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

	/** 10x10 open grid with edge walls between columns 4 and 5 on rows 0..8: the only way across is row 9. */
	static FCombatGridData MakeEdgeWallGrid()
	{
		FCombatGridData Grid;
		Grid.Init(10, 10, 100.f);
		for (int32 Y = 0; Y < 9; ++Y)
		{
			Grid.AddEdgeWall(FIntPoint(4, Y), FIntPoint(5, Y));
		}
		return Grid;
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
	// The distance map is on sub-cells: the middle sub-cell of cell (3, 2).
	const FIntPoint SubCell = FIntPoint(3, 2) * CombatNavigation::Subdivision + FIntPoint(1, 1);
	TestEqual(TEXT("A is nearest by walking"), Simulation.GetDistanceMap(0)->GetNearestId(SubCell), 1);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatRangeFromCenterTest, "BattleSystem.Combat.RangeFromCenter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatRangeFromCenterTest::RunTest(const FString& Parameters)
{
	// Ranges reach from the attacker's center to the target's edge: the attacker's own size adds nothing.
	FCombatUnit Attacker;
	Attacker.Stats.Radius = 60.f;
	FCombatUnit Target;
	Target.Stats.Radius = 40.f;
	Target.PreviousPosition = FVector2D(150.0, 0.0);
	TestEqual(TEXT("Reach = center distance - target radius"), FCombatSimulation::GetReach(Attacker, Target), 110.0);

	// Areas the same way, around the attacker (CircleAroundSelf, Cone) and around a target point.
	FCombatArea Circle;
	Circle.Shape = ECombatAreaShape::CircleAroundSelf;
	Circle.Radius = 100.f;
	TestTrue(TEXT("Edge just inside"), Circle.Contains(FVector2D(140.0, 0.0), 40.f));
	TestFalse(TEXT("Edge just outside"), Circle.Contains(FVector2D(141.0, 0.0), 40.f));
	FCombatArea Cone;
	Cone.Shape = ECombatAreaShape::Cone;
	Cone.Radius = 100.f;
	Cone.ConeCosHalfAngle = 0.5f;
	TestTrue(TEXT("Cone: edge just inside"), Cone.Contains(FVector2D(140.0, 0.0), 40.f));
	TestFalse(TEXT("Cone: edge just outside"), Cone.Contains(FVector2D(141.0, 0.0), 40.f));

	// A melee unit (radius 40, Range 60) starts its attack with the target's edge 60 cm from its center, not 100.
	FCombatUnitStats Fighter = CombatTests::MakeStats(100.f, 10.f, 20, 5);
	Fighter.Attacks[0].Range = 60.f;
	Fighter.MoveSpeed = 0.f;
	FCombatUnitStats Dummy = CombatTests::MakeDummyStats();
	Dummy.Radius = 40.f;
	auto AttacksAt = [&](int32 Cells)
	{
		FCombatSimConfig Config;
		Config.Grid.Init(10, 3, 100.f);
		Config.MaxFirstAttackDelayTicks = 0;
		CombatTests::AddUnit(Config, Fighter, 0, FIntPoint(1, 1));
		CombatTests::AddUnit(Config, Dummy, 1, FIntPoint(1 + Cells, 1));
		FCombatSimulation Simulation(Config);
		for (int32 Tick = 0; Tick < 5; ++Tick)
		{
			Simulation.Step();
			if (Simulation.GetEvents().ContainsByPredicate([](const FCombatEvent& Event) { return Event.Type == ECombatEventType::Attack; }))
			{
				return true;
			}
		}
		return false;
	};
	TestTrue(TEXT("Center distance 100, edge at 60: attacks"), AttacksAt(1));
	TestFalse(TEXT("Center distance 200, edge at 160: out of range"), AttacksAt(2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatAoEConeTest, "BattleSystem.Combat.AoECone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatAoEConeTest::RunTest(const FString& Parameters)
{
	// 90 degree cleave to the right (+X), reaching 190 cm from the attacker's center: hits in front, not behind or to the side.
	FCombatSimConfig Config;
	Config.Grid.Init(20, 12, 100.f);
	Config.MaxFirstAttackDelayTicks = 0;
	CombatTests::AddUnit(Config, CombatTests::MakeUnitWithAttack(CombatTests::MakeAreaAttack(ECombatAreaShape::Cone, 140.f, 190.f, 10.f)), 0, FIntPoint(5, 5));
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
	Replay.Settings = Settings;
	Replay.FinalChecksum = CombatReplay::ChecksumToString(0xDEADBEEF);

	FString Json;
	TestTrue(TEXT("Writes JSON"), CombatReplay::ToJson(Replay, Json));
	FCombatReplay Loaded;
	TestTrue(TEXT("Reads JSON"), CombatReplay::FromJson(Json, Loaded));
	TestEqual(TEXT("Seed"), Loaded.Seed, 1234);
	TestEqual(TEXT("At the current version"), Loaded.FormatVersion, FCombatReplay().FormatVersion);
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

	/** A 1x1 Cell piece that blocks walking and/or sight. */
	static FCombatLevelPiece MakeBlockPiece(FIntPoint Cell, bool bBlocksWalking, bool bBlocksSight)
	{
		FCombatLevelPiece Piece;
		Piece.Id = TEXT("Test/Block");
		Piece.Layer = ECombatPieceLayer::Cell;
		Piece.Cell = Cell;
		Piece.bBlocksWalking = bBlocksWalking;
		Piece.bBlocksSight = bBlocksSight;
		return Piece;
	}

	/** A level with every kind of blocking piece and three units, for the level tests. */
	static FCombatLevel MakeTestLevel()
	{
		FCombatLevel Level = FCombatLevel::MakeEmpty(TEXT("Test"), 12, 8);
		Level.PlacePiece(MakeBlockPiece(FIntPoint(5, 1), true, true));		// a wall
		Level.PlacePiece(MakeBlockPiece(FIntPoint(5, 2), false, true));		// a screen: blocks sight only
		Level.PlacePiece(MakeBlockPiece(FIntPoint(5, 3), true, false));		// a fence: blocks walking only
		Level.Units.Add(MakeLevelUnit(TEXT("Fighter"), 0, FIntPoint(1, 1)));
		Level.Units.Add(MakeLevelUnit(TEXT("Fighter"), 1, FIntPoint(10, 6)));
		Level.Units.Add(MakeLevelUnit(TEXT("Fighter"), 1, FIntPoint(5, 2)));	// on the screen: allowed
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
	Level.Units[0].Rotation = 3;
	Level.Pieces[0].Color = FColor(10, 120, 230);

	FCombatGridData Grid;
	Level.ToGridData(Grid);
	TestEqual(TEXT("Grid size"), Grid.Width * 100 + Grid.Height, 12 * 100 + 8);
	TestTrue(TEXT("A wall piece blocks walking and sight"), !Grid.IsWalkable(FIntPoint(5, 1)) && Grid.BlocksSight(FIntPoint(5, 1)));
	TestTrue(TEXT("A screen piece blocks sight only"), Grid.IsWalkable(FIntPoint(5, 2)) && Grid.BlocksSight(FIntPoint(5, 2)));
	TestTrue(TEXT("A fence piece blocks walking only"), !Grid.IsWalkable(FIntPoint(5, 3)) && !Grid.BlocksSight(FIntPoint(5, 3)));
	TestTrue(TEXT("Open cells are open"), Grid.IsWalkable(FIntPoint(0, 0)) && !Grid.BlocksSight(FIntPoint(0, 0)));

	FString Json;
	TestTrue(TEXT("Writes JSON"), CombatLevels::ToJson(Level, Json));
	FCombatLevel Loaded;
	TestTrue(TEXT("Reads JSON"), CombatLevels::FromJson(Json, Loaded));
	TestEqual(TEXT("Pieces survive"), Loaded.Pieces.Num(), 3);
	TestTrue(TEXT("Piece color survives"), Loaded.Pieces.Num() == 3 && Loaded.Pieces[0].Color == FColor(10, 120, 230) && Loaded.Pieces[1].Color == FColor::White);
	TestFalse(TEXT("No cell rows in the file"), Json.Contains(TEXT("\"rows\"")));
	TestEqual(TEXT("Units survive"), Loaded.Units.Num(), 3);
	TestTrue(TEXT("Unit cell survives"), Loaded.Units.Num() == 3 && Loaded.Units[1].Cell == FIntPoint(10, 6) && Loaded.Units[1].Team == 1);
	TestEqual(TEXT("Unit rotation survives"), Loaded.Units.Num() == 3 ? Loaded.Units[0].Rotation : -1, 3);
	TestEqual(TEXT("Rotation 12 is 135 degrees"), CombatLevels::GetUnitYaw(12), 135.f);
	TestEqual(TEXT("A step is 11.25 degrees"), CombatLevels::GetUnitYaw(1), 11.25f);

	Level.Resize(8, 8);
	TestEqual(TEXT("Shrinking removes units outside"), Level.Units.Num(), 2);
	Level.Resize(5, 8);
	TestEqual(TEXT("Shrinking removes pieces outside"), Level.Pieces.Num(), 0);

	// Missing fields load as their defaults: no waves or pieces, units in the middle of their cell facing +X, white pieces.
	FCombatLevel Minimal;
	TestTrue(TEXT("Reads a minimal level"), CombatLevels::FromJson(TEXT("{\"name\":\"Min\",\"width\":6,\"height\":5,")
		TEXT("\"units\":[{\"type\":\"A\",\"team\":1,\"cell\":{\"x\":5,\"y\":0}}],")
		TEXT("\"pieces\":[{\"id\":\"A/B\",\"layer\":\"Floor\",\"cell\":{\"x\":0,\"y\":0}}]}"), Minimal));
	TestTrue(TEXT("Without waves, at the current version"), Minimal.Waves.IsEmpty() && Minimal.FormatVersion == FCombatLevel().FormatVersion);
	TestTrue(TEXT("A unit without position or rotation stands in the middle, facing +X"), Minimal.Units.Num() == 1
		&& Minimal.Units[0].Position == CombatLevels::MiddlePosition && Minimal.Units[0].Rotation == 0);
	TestTrue(TEXT("A piece without a color is white"), Minimal.Pieces.Num() == 1 && Minimal.Pieces[0].Color == FColor::White);
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
	Level.Units.Add(CombatTests::MakeLevelUnit(TEXT("Fighter"), 1, FIntPoint(5, 1)));		// on the wall piece: skipped
	Level.Units.Add(CombatTests::MakeLevelUnit(TEXT("Unknown"), 1, FIntPoint(9, 1)));		// unknown type: skipped
	auto Resolve = [Fighter](const FString& Type) { return Type == TEXT("Fighter") ? Fighter : nullptr; };

	FCombatSimConfig Config;
	TArray<const UCombatUnitDefinition*> Definitions;
	TArray<int32> Rotations;
	Level.Units[2].Rotation = 6;
	TestTrue(TEXT("Builds"), CombatLevels::BuildConfig(Level, 20, Resolve, Config, &Definitions, &Rotations));
	TestEqual(TEXT("Three valid units"), Config.Units.Num(), 3);
	TestEqual(TEXT("A definition per unit"), Definitions.Num(), 3);
	TestTrue(TEXT("A rotation per unit, skipped ones left out"), Rotations.Num() == 3 && Rotations[2] == 6);
	TestTrue(TEXT("Units in the middle of their cell have no offset"), Config.Units[0].StartOffset.IsZero());

	// A position in the cell: the unit starts on that sub-cell's center (0 = top left, a third of a cell from the middle).
	{
		FCombatLevel Placed = Level;
		Placed.Units[0].Position = 0;
		FCombatSimConfig PlacedConfig;
		CombatLevels::BuildConfig(Placed, 20, Resolve, PlacedConfig);
		const FVector2D Expected = PlacedConfig.Grid.CellToLocal(Placed.Units[0].Cell) + FVector2D(-100.0 / 3.0, -100.0 / 3.0);
		TestTrue(TEXT("Position 0 is a third of a cell up and left"), PlacedConfig.Units[0].StartOffset.Equals(FVector2D(-100.0 / 3.0, -100.0 / 3.0), 0.01));
		FCombatSimulation PlacedSimulation(PlacedConfig);
		TestTrue(TEXT("The simulation starts it there"), PlacedSimulation.GetUnits()[0].Position.Equals(Expected, 0.01));
	}
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
	Replay.Level = CombatTests::MakeTestLevel();
	Replay.Seed = 9;

	FString Json;
	CombatReplay::ToJson(Replay, Json);
	FCombatReplay Loaded;
	TestTrue(TEXT("Reads JSON"), CombatReplay::FromJson(Json, Loaded));
	TestEqual(TEXT("Has the level"), Loaded.Level.Name, Replay.Level.Name);
	TestEqual(TEXT("Same pieces"), Loaded.Level.Pieces.Num(), Replay.Level.Pieces.Num());
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
	Level.Waves[0].Spawns.Add(CombatTests::MakeLevelSpawn(TEXT("Fighter"), FIntPoint(5, 1), 0.f));	// on the wall piece: skipped
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
	TestTrue(TEXT("RemoveSpawnsAt removes from every wave"), Level.RemoveSpawnsAt(FIntPoint(9, 1)) && Level.FindSpawnAt(0, FIntPoint(9, 1), CombatLevels::MiddlePosition) == INDEX_NONE);

	// One spawn per position: two in the same cell on different positions.
	Level.Waves[0].Spawns.Add(CombatTests::MakeLevelSpawn(TEXT("Fighter"), FIntPoint(2, 2), 0.f));
	Level.Waves[0].Spawns.Last().Position = 0;
	Level.Waves[0].Spawns.Add(CombatTests::MakeLevelSpawn(TEXT("Fighter"), FIntPoint(2, 2), 0.f));
	TestTrue(TEXT("Spawns are found by cell and position"), Level.FindSpawnAt(0, FIntPoint(2, 2), 0) == Level.Waves[0].Spawns.Num() - 2
		&& Level.FindSpawnAt(0, FIntPoint(2, 2), CombatLevels::MiddlePosition) == Level.Waves[0].Spawns.Num() - 1 && Level.FindSpawnAt(0, FIntPoint(2, 2), 8) == INDEX_NONE);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatLevelFileTest, "BattleSystem.Combat.LevelFileRenameDelete",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatLevelFileTest::RunTest(const FString& Parameters)
{
	// Works on real files in Levels/, but only on its own ZZ_AutomationTest_* levels, which are always removed.
	const FString First = TEXT("ZZ_AutomationTest_First");
	const FString Second = TEXT("ZZ_AutomationTest_Second");
	const FString Other = TEXT("ZZ_AutomationTest_Other");
	auto CleanUp = [&]() { for (const FString& Name : { First, Second, Other }) { CombatLevels::Delete(Name); } };
	CleanUp();

	TestEqual(TEXT("CleanName keeps letters, digits, - and _"), CombatLevels::CleanName(TEXT("My Level/2-b_c!")), FString(TEXT("MyLevel2-b_c")));

	FCombatLevel Level = CombatTests::MakeTestLevel();
	Level.Name = First;
	TestTrue(TEXT("Saves"), CombatLevels::Save(Level));
	Level.Name = Other;
	TestTrue(TEXT("Saves another"), CombatLevels::Save(Level));

	TestFalse(TEXT("Rename onto an existing level is refused"), CombatLevels::Rename(First, Other));
	TestTrue(TEXT("Both still exist after the refusal"), CombatLevels::Exists(First) && CombatLevels::Exists(Other));

	TestTrue(TEXT("Renames"), CombatLevels::Rename(First, Second));
	TestFalse(TEXT("The old file is gone"), CombatLevels::Exists(First));
	FCombatLevel Loaded;
	TestTrue(TEXT("The new file loads"), CombatLevels::Load(Second, Loaded));
	TestTrue(TEXT("Same content"), Loaded.Pieces.Num() == Level.Pieces.Num() && Loaded.Units.Num() == Level.Units.Num());
	FString Json;
	FFileHelper::LoadFileToString(Json, *(CombatLevels::GetDirectory() / (Second + TEXT(".json"))));
	TestTrue(TEXT("The name inside the file changed"), Json.Contains(FString::Printf(TEXT("\"%s\""), *Second)));

	TestFalse(TEXT("Renaming a missing level fails"), CombatLevels::Rename(First, TEXT("ZZ_AutomationTest_Never")));
	TestTrue(TEXT("Deletes"), CombatLevels::Delete(Second));
	TestFalse(TEXT("Deleted"), CombatLevels::Exists(Second));
	TestFalse(TEXT("Deleting a missing level fails"), CombatLevels::Delete(Second));

	CleanUp();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatAppearancePicksTest, "BattleSystem.Combat.AppearancePicks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatAppearancePicksTest::RunTest(const FString& Parameters)
{
	// Empty transient meshes: the picks only compare pointers, nothing is rendered or merged.
	auto MakeMesh = []() { return NewObject<USkeletalMesh>(GetTransientPackage()); };
	USkeletalMesh* Body = MakeMesh();
	const TArray<USkeletalMesh*> Hairs = { MakeMesh(), MakeMesh(), MakeMesh() };

	UCombatAppearance* Look = NewObject<UCombatAppearance>(GetTransientPackage());
	auto AddSlot = [Look](const TArray<USkeletalMesh*>& Options, float EmptyChance)
	{
		FCombatAppearanceSlot& Slot = Look->Slots.AddDefaulted_GetRef();
		Slot.Options.Append(Options);
		Slot.EmptyChance = EmptyChance;
	};
	AddSlot({ Body }, 0.f);
	AddSlot(Hairs, 0.f);
	AddSlot({ MakeMesh(), MakeMesh() }, 0.5f);
	AddSlot({ MakeMesh() }, 1.f);
	AddSlot({}, 0.f);

	TestTrue(TEXT("Same seed, same picks"), Look->PickMeshes(7) == Look->PickMeshes(7));

	bool bAnyDifferent = false;
	bool bAlwaysFixed = true;
	TArray<int32> HairCounts = { 0, 0, 0 };
	int32 EmptyHats = 0;
	constexpr int32 Runs = 200;
	for (int32 Seed = 0; Seed < Runs; ++Seed)
	{
		const TArray<USkeletalMesh*> Picks = Look->PickMeshes(Seed);
		bAnyDifferent |= Picks != Look->PickMeshes(0);
		bAlwaysFixed &= Picks.Num() == 5 && Picks[0] == Body && Picks[3] == nullptr && Picks[4] == nullptr;
		const int32 HairIndex = Hairs.IndexOfByKey(Picks[1]);
		if (HairCounts.IsValidIndex(HairIndex))
		{
			++HairCounts[HairIndex];
		}
		EmptyHats += Picks[2] == nullptr ? 1 : 0;
	}

	TestTrue(TEXT("Other seeds give other picks"), bAnyDifferent);
	TestTrue(TEXT("One option is always picked, EmptyChance 1 and no options are always empty"), bAlwaysFixed);
	TestEqual(TEXT("Every hair pick is one of the options"), HairCounts[0] + HairCounts[1] + HairCounts[2], Runs);
	TestTrue(TEXT("Every hair option is picked"), HairCounts[0] > 0 && HairCounts[1] > 0 && HairCounts[2] > 0);
	TestTrue(TEXT("EmptyChance 0.5 leaves about half empty"), EmptyHats > Runs * 3 / 10 && EmptyHats < Runs * 7 / 10);

	Look->UniformScale = 2.f;
	Look->WidthScale = 1.5f;
	Look->HeightScale = 0.5f;
	TestTrue(TEXT("Mesh scale: uniform times width across, times height up"), Look->GetMeshScale().Equals(FVector(3.0, 3.0, 1.0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatCameraMathTest, "BattleSystem.Combat.CameraMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatCameraMathTest::RunTest(const FString& Parameters)
{
	// A camera 10 m back and 10 m up, looking at the pivot.
	const FVector Pivot(500.0, 300.0, 0.0);
	FVector Location = Pivot + FVector(-1000.0, 0.0, 1000.0);
	FRotator Rotation = (Pivot - Location).Rotation();
	const double StartDistance = FVector::Distance(Location, Pivot);

	CombatCamera::Orbit(Location, Rotation, Pivot, 90.0, -10.0, -90.0, 0.0);
	TestTrue(TEXT("Orbit keeps the distance to the pivot"), FMath::IsNearlyEqual(FVector::Distance(Location, Pivot), StartDistance, 0.01));
	TestTrue(TEXT("Orbit keeps looking at the pivot"), (Pivot - Location).GetSafeNormal().Equals(Rotation.Vector(), 1e-4));
	TestTrue(TEXT("Orbit turns by the yaw and pitch"), FMath::IsNearlyEqual(Rotation.Yaw, 90.0, 1e-4) && FMath::IsNearlyEqual(Rotation.Pitch, -55.0, 1e-4));

	CombatCamera::Orbit(Location, Rotation, Pivot, 0.0, -80.0, -80.0, 0.0);
	TestTrue(TEXT("Orbit stops at the pitch limit"), FMath::IsNearlyEqual(Rotation.Pitch, -80.0, 1e-4));
	TestTrue(TEXT("Orbit at the limit still looks at the pivot"), (Pivot - Location).GetSafeNormal().Equals(Rotation.Vector(), 1e-4));

	const FBox Bounds(FVector(0.0, 0.0, 100.0), FVector(2000.0, 1000.0, 5000.0));
	FVector Outside(-50.0, 1500.0, 20.0);
	FRotator Tilted(20.0, 45.0, 10.0);
	CombatCamera::Clamp(Outside, Tilted, Bounds, -90.0, 0.0);
	TestTrue(TEXT("Clamp keeps the location inside the bounds"), Outside.Equals(FVector(0.0, 1000.0, 100.0)));
	TestTrue(TEXT("Clamp limits the pitch and removes the roll"), Tilted.Equals(FRotator(0.0, 45.0, 0.0)));

	FVector Hit;
	TestTrue(TEXT("A ray down hits the plane"), CombatCamera::RayToPlane(FVector(0.0, 0.0, 500.0), FVector(1.0, 0.0, -1.0).GetSafeNormal(), 100.0, Hit));
	TestTrue(TEXT("At the right point"), Hit.Equals(FVector(400.0, 0.0, 100.0), 0.01));
	TestFalse(TEXT("A ray up misses"), CombatCamera::RayToPlane(FVector(0.0, 0.0, 500.0), FVector(0.0, 0.0, 1.0), 100.0, Hit));
	TestFalse(TEXT("A level ray misses"), CombatCamera::RayToPlane(FVector(0.0, 0.0, 500.0), FVector(1.0, 0.0, 0.0), 100.0, Hit));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatLocomotionPlayRateTest, "BattleSystem.Combat.LocomotionPlayRate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatLocomotionPlayRateTest::RunTest(const FString& Parameters)
{
	// The samples of an idle (0), walk (150) and run (380) blend space on its speed axis.
	float Slowest = -1.f;
	float Fastest = -1.f;
	const float Samples[] = { 0.f, 380.f, 150.f };
	TestTrue(TEXT("Finds moving samples"), UCombatAnimInstance::FindMovingSampleRange(Samples, 5.f, Slowest, Fastest));
	TestTrue(TEXT("Slowest and fastest moving sample"), Slowest == 150.f && Fastest == 380.f);
	const float IdleOnly[] = { 0.f, 3.f };
	TestFalse(TEXT("Only idle samples"), UCombatAnimInstance::FindMovingSampleRange(IdleOnly, 5.f, Slowest, Fastest));
	TestTrue(TEXT("Without moving samples both are 0"), Slowest == 0.f && Fastest == 0.f);

	auto Rate = [](float Speed, float UnitRate = 1.f, float Slow = 150.f, float Fast = 380.f)
	{
		return UCombatAnimInstance::ComputeLocomotionPlayRate(Speed, 5.f, Slow, Fast, UnitRate, 0.5f, 2.f);
	};
	TestEqual(TEXT("Standing still: 1, also with a unit rate"), Rate(0.f, 1.5f), 1.f);
	TestEqual(TEXT("Between the samples the blend space blends the stride: 1"), Rate(300.f), 1.f);
	TestEqual(TEXT("Faster than the run sample: speeds up"), Rate(475.f), 1.25f);
	TestEqual(TEXT("Slower than the walk sample: slows down"), Rate(75.f), 0.5f);
	TestEqual(TEXT("The unit rate multiplies"), Rate(300.f, 1.2f), 1.2f);
	TestEqual(TEXT("Clamped to the maximum"), Rate(1000.f, 1.5f), 2.f);
	TestEqual(TEXT("Clamped to the minimum"), Rate(20.f), 0.5f);
	TestEqual(TEXT("Without moving samples only the unit rate counts"), Rate(300.f, 0.8f, 0.f, 0.f), 0.8f);

	// A taller figure takes longer steps: at the standard size it walks slower.
	TestEqual(TEXT("Standard size: the real speed"), UCombatAnimInstance::ComputeStrideSpeed(300.f, 1.f, 1.f), 300.f);
	TestEqual(TEXT("120% tall: slower stride"), UCombatAnimInstance::ComputeStrideSpeed(300.f, 1.2f, 1.f), 250.f);
	TestEqual(TEXT("80% tall: faster stride"), UCombatAnimInstance::ComputeStrideSpeed(300.f, 0.8f, 1.f), 375.f);
	TestEqual(TEXT("No compensation: the real speed"), UCombatAnimInstance::ComputeStrideSpeed(300.f, 1.2f, 0.f), 300.f);
	TestEqual(TEXT("Half compensation"), UCombatAnimInstance::ComputeStrideSpeed(330.f, 1.2f, 0.5f), 300.f);
	TestEqual(TEXT("A zero scale is ignored"), UCombatAnimInstance::ComputeStrideSpeed(300.f, 0.f, 1.f), 300.f);

	// Velocity seen from the figure: X forward, Y to its right (UE: facing +X, right is +Y).
	TestTrue(TEXT("Facing +X, moving +X: forward"), UCombatAnimInstance::ToLocalVelocity(FVector2D(100.0, 0.0), 0.f).Equals(FVector2D(100.0, 0.0), 0.01));
	TestTrue(TEXT("Facing +X, moving +Y: to the right"), UCombatAnimInstance::ToLocalVelocity(FVector2D(0.0, 100.0), 0.f).Equals(FVector2D(0.0, 100.0), 0.01));
	TestTrue(TEXT("Facing +Y, moving +X: to the left"), UCombatAnimInstance::ToLocalVelocity(FVector2D(100.0, 0.0), 90.f).Equals(FVector2D(0.0, -100.0), 0.01));
	TestTrue(TEXT("Facing +Y, moving -Y: backwards"), UCombatAnimInstance::ToLocalVelocity(FVector2D(0.0, -100.0), 90.f).Equals(FVector2D(-100.0, 0.0), 0.01));

	// Blend space coordinates: a Forward/Right blend space gets the local velocity, a Speed one its length.
	TestTrue(TEXT("Forward on X, Right on Y"), UCombatAnimInstance::GetBlendCoordinates(FVector2D(30.0, -40.0), 0, 0, 1).Equals(FVector2D(30.0, -40.0)));
	TestTrue(TEXT("Forward on Y, Right on X"), UCombatAnimInstance::GetBlendCoordinates(FVector2D(30.0, -40.0), 0, 1, 0).Equals(FVector2D(-40.0, 30.0)));
	TestTrue(TEXT("Speed on Y: the length"), UCombatAnimInstance::GetBlendCoordinates(FVector2D(30.0, -40.0), 1, INDEX_NONE, INDEX_NONE).Equals(FVector2D(0.0, 50.0)));
	TestTrue(TEXT("Only a Forward axis: the speed axis"), UCombatAnimInstance::GetBlendCoordinates(FVector2D(30.0, -40.0), 0, 1, INDEX_NONE).Equals(FVector2D(50.0, 0.0)));
	TestEqual(TEXT("No blend space: no axis found"), UCombatAnimInstance::FindAxis(nullptr, TEXT("Forward")), (int32)INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatAnimSetTest, "BattleSystem.Combat.AnimSet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatAnimSetTest::RunTest(const FString& Parameters)
{
	// Empty transient montages: only their identity is compared.
	UAnimMontage* Generic = NewObject<UAnimMontage>(GetTransientPackage());
	UAnimMontage* Throw = NewObject<UAnimMontage>(GetTransientPackage());
	UCombatAnimSet* Set = NewObject<UCombatAnimSet>(GetTransientPackage());
	Set->Actions.Add({ CombatTags::Anim, Generic });
	Set->Actions.Add({ CombatTags::Anim_Throw, Throw });
	Set->Actions.Add({ CombatTags::Attack_Melee, nullptr });

	TestTrue(TEXT("An exact entry wins over its parent"), Set->FindMontage(CombatTags::Anim_Throw) == Throw);
	TestTrue(TEXT("A tag without an entry uses its nearest parent"), Set->FindMontage(CombatTags::Anim_Push) == Generic);
	TestTrue(TEXT("An entry without a montage counts as none"), Set->FindMontage(CombatTags::Attack_Melee) == nullptr);
	TestTrue(TEXT("No entry, no montage"), Set->FindMontage(CombatTags::Attack_Ranged) == nullptr);
	TestTrue(TEXT("An empty tag has no montage"), Set->FindMontage(FGameplayTag()) == nullptr);

	Set->MinPlayRate = 0.25f;
	Set->MaxPlayRate = 4.f;
	TestEqual(TEXT("Impact at 0.6 s, hit after 0.3 s: twice as fast"), Set->GetPlayRateForImpact(0.6f, 0.3f), 2.f);
	TestEqual(TEXT("Impact at 0.2 s, hit after 0.4 s: half as fast"), Set->GetPlayRateForImpact(0.2f, 0.4f), 0.5f);
	TestEqual(TEXT("Clamped to MaxPlayRate"), Set->GetPlayRateForImpact(2.f, 0.1f), 4.f);
	TestEqual(TEXT("Clamped to MinPlayRate"), Set->GetPlayRateForImpact(0.1f, 2.f), 0.25f);
	TestEqual(TEXT("Without an Impact notify: normal speed"), Set->GetPlayRateForImpact(-1.f, 0.3f), 1.f);
	TestEqual(TEXT("Without a windup: normal speed"), Set->GetPlayRateForImpact(0.5f, 0.f), 1.f);
	TestTrue(TEXT("A montage without notifies has no impact time"), UCombatAnimSet::FindImpactTime(Throw) < 0.f);

	// Without a blend space (or without an axis named Speed) the speed goes on X.
	// Anim instances must live in a skeletal mesh component.
	UCombatAnimInstance* Instance = NewObject<UCombatAnimInstance>(NewObject<USkeletalMeshComponent>(GetTransientPackage()));
	TestEqual(TEXT("No blend space: speed axis X"), UCombatAnimInstance::FindSpeedAxis(nullptr), 0);
	Instance->SetLocomotion(nullptr);
	Instance->SetLocalVelocity(FVector2D(0.0, 220.0));
	TestTrue(TEXT("Speed on X, Y stays 0"), Instance->Speed == 220.f && Instance->LocomotionX == 220.f && Instance->LocomotionY == 0.f);
	TestEqual(TEXT("Moving to its right: direction 90"), Instance->Direction, 90.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatEdgeWallGridTest, "BattleSystem.Combat.EdgeWallGrid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatEdgeWallGridTest::RunTest(const FString& Parameters)
{
	FCombatGridData Grid;
	Grid.Init(5, 5, 100.f);
	const uint32 OpenChecksum = Grid.ComputeChecksum();
	Grid.AddEdgeWall(FIntPoint(1, 1), FIntPoint(3, 1));
	Grid.AddEdgeWall(FIntPoint(0, 0), FIntPoint(-1, 0));
	TestFalse(TEXT("Non-neighbors and grid borders add no edge wall"), Grid.HasEdgeWalls());
	TestEqual(TEXT("Without edge walls the checksum is the old one"), Grid.ComputeChecksum(), OpenChecksum);

	Grid.AddEdgeWall(FIntPoint(2, 1), FIntPoint(1, 1));
	TestTrue(TEXT("The wall is on the border, seen from both sides"), Grid.HasEdgeWall(FIntPoint(1, 1), FIntPoint(2, 1)) && Grid.HasEdgeWall(FIntPoint(2, 1), FIntPoint(1, 1)));
	TestFalse(TEXT("Other borders stay open"), Grid.HasEdgeWall(FIntPoint(1, 0), FIntPoint(2, 0)) || Grid.HasEdgeWall(FIntPoint(1, 1), FIntPoint(1, 2)));
	TestNotEqual(TEXT("An edge wall changes the checksum"), Grid.ComputeChecksum(), OpenChecksum);

	TestFalse(TEXT("No step across the wall"), Grid.CanStep(FIntPoint(1, 1), FIntPoint(1, 0)) || Grid.CanStep(FIntPoint(2, 1), FIntPoint(-1, 0)));
	TestTrue(TEXT("A step beside it is fine"), Grid.CanStep(FIntPoint(1, 0), FIntPoint(1, 0)) && Grid.CanStep(FIntPoint(1, 1), FIntPoint(0, 1)));
	TestFalse(TEXT("No diagonal step past the end of the wall"), Grid.CanStep(FIntPoint(1, 1), FIntPoint(1, 1)) || Grid.CanStep(FIntPoint(1, 0), FIntPoint(1, 1)));
	TestTrue(TEXT("A diagonal step away from the wall is fine"), Grid.CanStep(FIntPoint(1, 2), FIntPoint(1, 1)));

	const FVector2D Left = Grid.CellToLocal(FIntPoint(1, 1));
	const FVector2D Right = Grid.CellToLocal(FIntPoint(3, 1));
	TestFalse(TEXT("No walking line through the wall"), Grid.IsLineWalkable(Left, Right));
	TestFalse(TEXT("No line of sight through the wall"), Grid.HasLineOfSight(Left, Right));
	TestFalse(TEXT("A move through the wall crosses it"), Grid.CrossesNoEdgeWall(Left, Right));
	TestTrue(TEXT("A line one row lower is clear"), Grid.IsLineWalkable(Grid.CellToLocal(FIntPoint(1, 2)), Grid.CellToLocal(FIntPoint(3, 2))));
	TestFalse(TEXT("A line exactly through the wall's end corner is blocked"), Grid.HasLineOfSight(FVector2D(150.0, 150.0), FVector2D(250.0, 250.0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatEdgeWallPathTest, "BattleSystem.Combat.EdgeWallPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatEdgeWallPathTest::RunTest(const FString& Parameters)
{
	const FCombatGridData Grid = CombatTests::MakeEdgeWallGrid();
	TArray<FIntPoint> Path;
	TestTrue(TEXT("A path exists around the wall"), CombatPathfinding::FindPath(Grid, FIntPoint(2, 2), FIntPoint(7, 2), Path));

	bool bNeverCrosses = true;
	for (int32 Index = 1; Index < Path.Num(); ++Index)
	{
		bNeverCrosses &= Grid.CanStep(Path[Index - 1], Path[Index] - Path[Index - 1]);
	}
	TestTrue(TEXT("Every step of the path is allowed"), bNeverCrosses);
	TestTrue(TEXT("It goes through the gap in row 9"), Path.ContainsByPredicate([](const FIntPoint& Cell) { return Cell.Y == 9; }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatEdgeWallFightTest, "BattleSystem.Combat.EdgeWallFight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatEdgeWallFightTest::RunTest(const FString& Parameters)
{
	FCombatSimConfig Config;
	Config.Grid = CombatTests::MakeEdgeWallGrid();
	Config.MaxFirstAttackDelayTicks = 0;
	CombatTests::AddUnit(Config, CombatTests::MakeStats(100.f, 10.f, 20, 6), 0, FIntPoint(2, 2));
	CombatTests::AddUnit(Config, CombatTests::MakeDummyStats(), 1, FIntPoint(7, 2));

	FCombatSimulation Simulation(Config);
	bool bNeverThroughWall = true;
	bool bAttacked = false;
	for (int32 Step = 0; Step < 900 && !bAttacked; ++Step)
	{
		Simulation.Step();
		const FCombatUnit& Unit = Simulation.GetUnits()[0];
		bNeverThroughWall &= Config.Grid.CrossesNoEdgeWall(Unit.PreviousPosition, Unit.Position);
		bAttacked = Simulation.GetEvents().ContainsByPredicate([](const FCombatEvent& Event)
		{
			return Event.Type == ECombatEventType::Attack && Event.SourceId == 0;
		});
	}
	TestTrue(TEXT("Never moves through the edge wall"), bNeverThroughWall);
	TestTrue(TEXT("Walks around it and attacks"), bAttacked);

	TestTrue(TEXT("Same seed, same checksum after every step, with edge walls"),
		CombatTests::RunAndCollectChecksums(Config) == CombatTests::RunAndCollectChecksums(Config));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatPieceFootprintTest, "BattleSystem.Combat.PieceFootprint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatPieceFootprintTest::RunTest(const FString& Parameters)
{
	FCombatLevelPiece Table;
	Table.Layer = ECombatPieceLayer::Cell;
	Table.Cell = FIntPoint(2, 3);
	Table.Size = FIntPoint(2, 1);
	TArray<FIntPoint> Cells;
	Table.GetCells(Cells);
	TestTrue(TEXT("A 2x1 piece covers two cells along X"), Cells == TArray<FIntPoint>({ FIntPoint(2, 3), FIntPoint(3, 3) }));
	Table.Rotation = 1;
	Table.GetCells(Cells);
	TestTrue(TEXT("Turned a quarter it covers two cells along Y"), Cells == TArray<FIntPoint>({ FIntPoint(2, 3), FIntPoint(2, 4) }));

	FCombatLevelPiece Wall;
	Wall.Layer = ECombatPieceLayer::Edge;
	Wall.Cell = FIntPoint(1, 2);
	Wall.Size = FIntPoint(3, 1);
	TArray<TPair<FIntPoint, FIntPoint>> Edges;
	Wall.GetEdges(Edges);
	TestEqual(TEXT("A wall of 3 covers 3 borders"), Edges.Num(), 3);
	TestTrue(TEXT("Horizontal: between rows 1 and 2, from column 1"), Edges[0] == TPair<FIntPoint, FIntPoint>(FIntPoint(1, 1), FIntPoint(1, 2)) && Edges[2].Value == FIntPoint(3, 2));
	Wall.Rotation = 3;
	Wall.GetEdges(Edges);
	TestTrue(TEXT("Vertical: between columns 0 and 1, from row 2"), Edges[0] == TPair<FIntPoint, FIntPoint>(FIntPoint(0, 2), FIntPoint(1, 2)) && Edges[2].Value == FIntPoint(1, 4));
	Wall.GetCells(Cells);
	TestEqual(TEXT("A border piece covers no cells"), Cells.Num(), 0);

	FCombatLevel Level = FCombatLevel::MakeEmpty(TEXT("Pieces"), 6, 5);
	FCombatLevelPiece Floor;
	Floor.Layer = ECombatPieceLayer::Floor;
	Floor.Size = FIntPoint(4, 4);
	TestTrue(TEXT("A 4x4 floor fits at (0,0)"), Level.PlacePiece(Floor));
	Floor.Cell = FIntPoint(3, 0);
	TestFalse(TEXT("... but not at (3,0) of a 6 wide level"), Level.PlacePiece(Floor));
	Table.Rotation = 0;
	TestTrue(TEXT("A table goes on the floor (another layer)"), Level.PlacePiece(Table));
	FCombatLevelPiece Chair = Table;
	Chair.Size = FIntPoint(1, 1);
	Chair.Cell = FIntPoint(3, 3);
	TestTrue(TEXT("A chair on the table's second cell replaces the table"), Level.PlacePiece(Chair));
	TestEqual(TEXT("Floor and chair are left"), Level.Pieces.Num(), 2);
	TestEqual(TEXT("FindPieceAt finds the chair"), Level.FindPieceAt(ECombatPieceLayer::Cell, FIntPoint(3, 3)), 1);
	TestEqual(TEXT("... and nothing where the table was"), Level.FindPieceAt(ECombatPieceLayer::Cell, FIntPoint(2, 3)), INDEX_NONE);
	FCombatLevelPiece Border = Wall;
	Border.Rotation = 0;
	Border.Cell = FIntPoint(3, 5);
	TestTrue(TEXT("A border piece may lie on the outer border"), Level.IsPieceInBounds(Border));
	Border.Cell = FIntPoint(4, 5);
	TestFalse(TEXT("... but not stick out of it"), Level.IsPieceInBounds(Border));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatPieceGridTest, "BattleSystem.Combat.PieceGrid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatPieceGridTest::RunTest(const FString& Parameters)
{
	FCombatPieceDefinition WallDefinition;
	WallDefinition.Id = TEXT("Walls/TestWall");
	WallDefinition.Layer = ECombatPieceLayer::Edge;
	WallDefinition.Size = FIntPoint(2, 1);
	FCombatPieceDefinition DoorDefinition = WallDefinition;
	DoorDefinition.Id = TEXT("Doors/TestDoor");
	DoorDefinition.Size = FIntPoint(1, 1);
	DoorDefinition.bBlocksWalking = false;
	FCombatPieceDefinition ClosetDefinition;
	ClosetDefinition.Id = TEXT("Furniture/TestCloset");
	ClosetDefinition.bBlocksSight = true;
	FCombatPieceDefinition FloorDefinition;
	FloorDefinition.Id = TEXT("Floors/TestFloor");
	FloorDefinition.Layer = ECombatPieceLayer::Floor;

	FCombatLevel Level = FCombatLevel::MakeEmpty(TEXT("PieceGrid"), 8, 6);
	TestTrue(TEXT("Wall placed"), Level.PlacePiece(WallDefinition.MakePiece(FIntPoint(2, 2), 0)));
	TestTrue(TEXT("Door placed"), Level.PlacePiece(DoorDefinition.MakePiece(FIntPoint(5, 1), 1)));
	TestTrue(TEXT("Closet placed"), Level.PlacePiece(ClosetDefinition.MakePiece(FIntPoint(6, 4), 0)));
	TestTrue(TEXT("Floor placed"), Level.PlacePiece(FloorDefinition.MakePiece(FIntPoint(0, 0), 0)));
	TestFalse(TEXT("A floor never blocks"), Level.Pieces.Last().bBlocksWalking);

	FCombatGridData Grid;
	Level.ToGridData(Grid);
	TestTrue(TEXT("The wall blocks its two borders"), Grid.HasEdgeWall(FIntPoint(2, 1), FIntPoint(2, 2)) && Grid.HasEdgeWall(FIntPoint(3, 1), FIntPoint(3, 2)));
	TestFalse(TEXT("... and not the next one"), Grid.HasEdgeWall(FIntPoint(4, 1), FIntPoint(4, 2)));
	TestFalse(TEXT("A door frame does not block"), Grid.HasEdgeWall(FIntPoint(4, 1), FIntPoint(5, 1)));
	TestTrue(TEXT("The closet blocks walking and sight"), !Grid.IsWalkable(FIntPoint(6, 4)) && Grid.BlocksSight(FIntPoint(6, 4)));
	TestTrue(TEXT("The floor cell stays open"), Grid.IsWalkable(FIntPoint(0, 0)) && !Grid.BlocksSight(FIntPoint(0, 0)));

	FString Json;
	FCombatLevel Loaded;
	TestTrue(TEXT("JSON round trip"), CombatLevels::ToJson(Level, Json) && CombatLevels::FromJson(Json, Loaded));
	FCombatGridData LoadedGrid;
	Loaded.ToGridData(LoadedGrid);
	TestEqual(TEXT("Same pieces after loading"), Loaded.Pieces.Num(), Level.Pieces.Num());
	TestEqual(TEXT("Same grid after loading"), LoadedGrid.ComputeChecksum(), Grid.ComputeChecksum());

	Level.Resize(5, 6);
	TestTrue(TEXT("Resize removes pieces that no longer fit"), Level.FindPieceAt(ECombatPieceLayer::Cell, FIntPoint(6, 4)) == INDEX_NONE && Level.Pieces.Num() == 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatPieceTransformTest, "BattleSystem.Combat.PieceTransform",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatPieceTransformTest::RunTest(const FString& Parameters)
{
	FCombatLevelPiece Wall;
	Wall.Layer = ECombatPieceLayer::Edge;
	Wall.Cell = FIntPoint(2, 3);
	Wall.Size = FIntPoint(4, 1);
	const FBox WallBounds(FVector(-200.0, -10.0, 0.0), FVector(200.0, 10.0, 400.0));
	FTransform Transform = CombatPieces::ComputeMeshTransform(Wall, 100.f, WallBounds, 0.f, FVector::ZeroVector);
	TestTrue(TEXT("Horizontal wall: centered on its border line"), Transform.GetLocation().Equals(FVector(400.0, 300.0, 0.0), 0.01));
	Wall.Rotation = 1;
	Transform = CombatPieces::ComputeMeshTransform(Wall, 100.f, WallBounds, 0.f, FVector::ZeroVector);
	TestTrue(TEXT("Vertical wall: along the column border, turned 90"), Transform.GetLocation().Equals(FVector(200.0, 500.0, 0.0), 0.01)
		&& FMath::IsNearlyEqual(Transform.Rotator().Yaw, 90.0, 0.01));

	// A door with its pivot at one end and a window centered on its height.
	FCombatLevelPiece Door = Wall;
	Door.Size = FIntPoint(1, 1);
	Door.Rotation = 0;
	Door.Cell = FIntPoint(5, 5);
	Transform = CombatPieces::ComputeMeshTransform(Door, 100.f, FBox(FVector(0.0, -11.0, 0.0), FVector(104.0, 11.0, 240.0)), 0.f, FVector::ZeroVector);
	TestTrue(TEXT("Off-center pivot: the bounds are centered, not the pivot"), Transform.GetLocation().Equals(FVector(498.0, 500.0, 0.0), 0.01));
	Transform = CombatPieces::ComputeMeshTransform(Door, 100.f, FBox(FVector(-80.0, -10.0, -100.0), FVector(80.0, 10.0, 100.0)), 0.f, FVector(0.0, 0.0, 90.0));
	TestTrue(TEXT("Centered height: the bottom on the floor, plus the offset"), FMath::IsNearlyEqual(Transform.GetLocation().Z, 190.0, 0.01));

	FCombatLevelPiece Floor;
	Floor.Layer = ECombatPieceLayer::Floor;
	Floor.Size = FIntPoint(4, 2);
	Floor.Rotation = 1;
	Transform = CombatPieces::ComputeMeshTransform(Floor, 100.f, FBox(FVector(-200.0, -100.0, 0.0), FVector(200.0, 100.0, 0.0)), 0.f, FVector::ZeroVector);
	TestTrue(TEXT("A turned 4x2 floor is centered on its 2x4 cells"), Transform.GetLocation().Equals(FVector(100.0, 200.0, 0.0), 0.01));

	// A mesh long along Y gets MeshYaw 90 so that it lies along the border.
	Wall.Rotation = 0;
	Transform = CombatPieces::ComputeMeshTransform(Wall, 100.f, FBox(FVector(-10.0, -200.0, 0.0), FVector(10.0, 200.0, 400.0)), 90.f, FVector::ZeroVector);
	TestTrue(TEXT("MeshYaw turns the mesh onto the border"), FMath::IsNearlyEqual(Transform.Rotator().Yaw, 90.0, 0.01) && Transform.GetLocation().Equals(FVector(400.0, 300.0, 0.0), 0.01));

	// Scale to fit: a 4 m wall as a 1 m piece, and an off-center 4 m wall as a 2 m piece.
	Wall.Size = FIntPoint(1, 1);
	Transform = CombatPieces::ComputeMeshTransform(Wall, 100.f, WallBounds, 0.f, FVector::ZeroVector, true);
	TestTrue(TEXT("Scale to fit: a quarter along the length, depth and height kept"), Transform.GetScale3D().Equals(FVector(0.25, 1.0, 1.0), 0.001));
	TestTrue(TEXT("... centered on its one border"), Transform.GetLocation().Equals(FVector(250.0, 300.0, 0.0), 0.01));
	Wall.Size = FIntPoint(2, 1);
	Transform = CombatPieces::ComputeMeshTransform(Wall, 100.f, FBox(FVector(0.0, -10.0, 0.0), FVector(400.0, 10.0, 400.0)), 0.f, FVector::ZeroVector, true);
	TestTrue(TEXT("Off-center pivot scaled: the scaled bounds are centered"), Transform.GetLocation().Equals(FVector(200.0, 300.0, 0.0), 0.01));
	Transform = CombatPieces::ComputeMeshTransform(Wall, 100.f, FBox(FVector(-10.0, -200.0, 0.0), FVector(10.0, 200.0, 400.0)), 90.f, FVector::ZeroVector, true);
	TestTrue(TEXT("A mesh long along Y scales its Y"), Transform.GetScale3D().Equals(FVector(1.0, 0.5, 1.0), 0.001));
	Floor.Size = FIntPoint(2, 2);
	Floor.Rotation = 0;
	Transform = CombatPieces::ComputeMeshTransform(Floor, 100.f, FBox(FVector(-200.0, -100.0, 0.0), FVector(200.0, 100.0, 0.0)), 0.f, FVector::ZeroVector, true);
	TestTrue(TEXT("A floor scales in X and Y"), Transform.GetScale3D().Equals(FVector(0.5, 1.0, 1.0), 0.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatPiecePlacementTest, "BattleSystem.Combat.PiecePlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatPiecePlacementTest::RunTest(const FString& Parameters)
{
	FCombatPieceDefinition Chair;
	Chair.Layer = ECombatPieceLayer::Cell;
	TestEqual(TEXT("A 1x1 piece goes on the cell under the point"), CombatPieces::PlaceAt(Chair, FVector2D(350.0, 120.0), 0, 100.f).Cell, FIntPoint(3, 1));

	FCombatPieceDefinition Floor;
	Floor.Layer = ECombatPieceLayer::Floor;
	Floor.Size = FIntPoint(4, 2);
	TestEqual(TEXT("A 4x2 floor is centered on the cell under the point (rounded down)"), CombatPieces::PlaceAt(Floor, FVector2D(550.0, 550.0), 0, 100.f).Cell, FIntPoint(4, 5));
	TestEqual(TEXT("... turned it is 2x4"), CombatPieces::PlaceAt(Floor, FVector2D(550.0, 550.0), 1, 100.f).Cell, FIntPoint(5, 4));

	FCombatPieceDefinition Wall;
	Wall.Layer = ECombatPieceLayer::Edge;
	Wall.Size = FIntPoint(4, 1);
	FCombatLevelPiece Piece = CombatPieces::PlaceAt(Wall, FVector2D(520.0, 340.0), 0, 100.f);
	TestEqual(TEXT("Horizontal wall: nearest row border, centered along it"), Piece.Cell, FIntPoint(3, 3));
	Piece = CombatPieces::PlaceAt(Wall, FVector2D(520.0, 340.0), 1, 100.f);
	TestEqual(TEXT("Vertical wall: nearest column border, centered along it"), Piece.Cell, FIntPoint(5, 1));
	TestEqual(TEXT("The rotation is kept"), Piece.Rotation, 1);

	FCombatPieceDefinition Door = Wall;
	Door.Size = FIntPoint(1, 1);
	TestEqual(TEXT("A 1-border door goes on the border segment under the point"), CombatPieces::PlaceAt(Door, FVector2D(520.0, 340.0), 0, 100.f).Cell, FIntPoint(5, 3));
	TestEqual(TEXT("Rotation 7 is rotation 3"), CombatPieces::PlaceAt(Door, FVector2D(0.0, 0.0), 7, 100.f).Rotation, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatDetailPiecesTest, "BattleSystem.Combat.DetailPieces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatDetailPiecesTest::RunTest(const FString& Parameters)
{
	FCombatPieceDefinition Toy;
	Toy.Layer = ECombatPieceLayer::Detail;
	Toy.bBlocksWalking = false;
	Toy.DetailGrid = 3;

	// Cell (2,1), third column and second row of its 3x3 grid.
	FCombatLevelPiece Piece = CombatPieces::PlaceAt(Toy, FVector2D(290.0, 150.0), 9, 100.f);
	TestEqual(TEXT("The cell under the point"), Piece.Cell, FIntPoint(2, 1));
	TestEqual(TEXT("The detail position under the point"), Piece.Detail, 2 + 1 * 3);
	TestEqual(TEXT("Details turn in eighths: 9 is 1"), Piece.Rotation, 1);
	TestTrue(TEXT("Its center"), Piece.GetDetailCenter(100.f).Equals(FVector2D(283.333, 150.0), 0.01));

	FTransform Transform = CombatPieces::ComputeMeshTransform(Piece, 100.f, FBox(FVector(-10.0), FVector(10.0)), 0.f, FVector::ZeroVector, true, 75.0);
	TestTrue(TEXT("A detail stands on its base height, centered on its position"), Transform.GetLocation().Equals(FVector(283.333, 150.0, 85.0), 0.01));
	TestTrue(TEXT("... turned 45 degrees, never scaled"), FMath::IsNearlyEqual(Transform.Rotator().Yaw, 45.0, 0.01) && Transform.GetScale3D().Equals(FVector::OneVector));

	FCombatLevel Level = FCombatLevel::MakeEmpty(TEXT("Details"), 6, 5);
	TestTrue(TEXT("Placed"), Level.PlacePiece(Piece));
	FCombatLevelPiece Other = CombatPieces::PlaceAt(Toy, FVector2D(210.0, 110.0), 0, 100.f);
	TestTrue(TEXT("Another position in the same cell"), Level.PlacePiece(Other) && Level.Pieces.Num() == 2);
	TestTrue(TEXT("The same position replaces"), Level.PlacePiece(CombatPieces::PlaceAt(Toy, FVector2D(295.0, 160.0), 0, 100.f)) && Level.Pieces.Num() == 2);

	FCombatGridData Grid;
	Level.ToGridData(Grid);
	TestTrue(TEXT("Open details keep the cell open"), Grid.IsWalkable(FIntPoint(2, 1)));
	Toy.bBlocksWalking = true;
	Level.PlacePiece(CombatPieces::PlaceAt(Toy, FVector2D(410.0, 410.0), 0, 100.f));
	Level.ToGridData(Grid);
	TestFalse(TEXT("A blocking detail blocks its whole cell"), Grid.IsWalkable(FIntPoint(4, 4)));

	const FBox Table(FVector(200.0, 100.0, 0.0), FVector(260.0, 200.0, 80.0));
	TestEqual(TEXT("Above the table: on its top"), CombatPieces::GetSurfaceHeight(Table, -1.f, FVector2D(230.0, 150.0)), 80.0);
	TestEqual(TEXT("... or on its SurfaceHeight"), CombatPieces::GetSurfaceHeight(Table, 45.f, FVector2D(230.0, 150.0)), 45.0);
	TestEqual(TEXT("Beside the table: the floor"), CombatPieces::GetSurfaceHeight(Table, -1.f, FVector2D(283.0, 150.0)), 0.0);
	TestEqual(TEXT("SurfaceHeight 0: nothing on it"), CombatPieces::GetSurfaceHeight(Table, 0.f, FVector2D(230.0, 150.0)), 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatPieceUnderTest, "BattleSystem.Combat.PieceUnder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatPieceUnderTest::RunTest(const FString& Parameters)
{
	FCombatPieceDefinition Floor;
	Floor.Layer = ECombatPieceLayer::Floor;
	Floor.Size = FIntPoint(4, 4);
	FCombatPieceDefinition Table;
	Table.Id = TEXT("Table");
	FCombatPieceDefinition Wall;
	Wall.Layer = ECombatPieceLayer::Edge;
	Wall.Size = FIntPoint(2, 1);
	FCombatPieceDefinition Toy;
	Toy.Layer = ECombatPieceLayer::Detail;

	FCombatLevel Level = FCombatLevel::MakeEmpty(TEXT("Under"), 6, 6);
	Level.PlacePiece(Floor.MakePiece(FIntPoint(0, 0), 0));
	Level.PlacePiece(Table.MakePiece(FIntPoint(1, 1), 0));
	Level.PlacePiece(Wall.MakePiece(FIntPoint(2, 3), 0));
	Level.PlacePiece(CombatPieces::PlaceAt(Toy, FVector2D(110.0, 110.0), 0, 100.f));

	TestEqual(TEXT("The detail on its position comes first"), Level.FindPieceUnder(FVector2D(115.0, 105.0)), 3);
	TestEqual(TEXT("Elsewhere in that cell: the table"), Level.FindPieceUnder(FVector2D(180.0, 180.0)), 1);
	TestEqual(TEXT("Near a wall's border: the wall"), Level.FindPieceUnder(FVector2D(250.0, 290.0)), 2);
	TestEqual(TEXT("Further from it: the floor"), Level.FindPieceUnder(FVector2D(250.0, 250.0)), 0);
	TestEqual(TEXT("Nothing outside every piece"), Level.FindPieceUnder(FVector2D(550.0, 550.0)), INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatWallClearanceTest, "BattleSystem.Combat.WallClearance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatWallClearanceTest::RunTest(const FString& Parameters)
{
	FCombatGridData Grid = CombatTests::MakeEdgeWallGrid();
	Grid.AddFlags(FIntPoint(2, 5), ECombatCellFlags::Blocked);

	TestTrue(TEXT("Next to a blocked cell: pushed out to the clearance"), Grid.PushClear(FVector2D(250.0, 610.0), 45.f).Equals(FVector2D(250.0, 645.0), 0.01));
	TestTrue(TEXT("Next to an edge wall: pushed out"), Grid.PushClear(FVector2D(490.0, 250.0), 45.f).Equals(FVector2D(455.0, 250.0), 0.01));
	TestTrue(TEXT("Next to the grid's border: pushed out"), Grid.PushClear(FVector2D(20.0, 250.0), 45.f).Equals(FVector2D(45.0, 250.0), 0.01));
	TestTrue(TEXT("Far enough: unchanged"), Grid.PushClear(FVector2D(150.0, 150.0), 45.f).Equals(FVector2D(150.0, 150.0)));
	TestTrue(TEXT("Clearance 0: unchanged"), Grid.PushClear(FVector2D(490.0, 250.0), 0.f).Equals(FVector2D(490.0, 250.0)));

	// A fight through the gap of the edge wall: the unit keeps its distance on the way and still gets through.
	FCombatSimConfig Config;
	Config.Grid = CombatTests::MakeEdgeWallGrid();
	Config.MaxFirstAttackDelayTicks = 0;
	Config.WallClearance = 45.f;
	CombatTests::AddUnit(Config, CombatTests::MakeStats(100.f, 10.f, 20, 6), 0, FIntPoint(2, 2));
	CombatTests::AddUnit(Config, CombatTests::MakeDummyStats(), 1, FIntPoint(7, 2));
	FCombatSimulation Simulation(Config);
	const float Clearance = FMath::Min(Config.Units[0].Stats.Radius, Config.WallClearance);
	bool bKeptDistance = true;
	bool bAttacked = false;
	for (int32 Step = 0; Step < 900 && !bAttacked; ++Step)
	{
		Simulation.Step();
		const FVector2D Position = Simulation.GetUnits()[0].Position;
		bKeptDistance &= Config.Grid.PushClear(Position, Clearance - 0.5f).Equals(Position, 0.01);
		bAttacked = Simulation.GetEvents().ContainsByPredicate([](const FCombatEvent& Event)
		{
			return Event.Type == ECombatEventType::Attack && Event.SourceId == 0;
		});
	}
	TestTrue(TEXT("Never closer to a wall than its clearance"), bKeptDistance);
	TestTrue(TEXT("Still gets through the gap and attacks"), bAttacked);
	TestTrue(TEXT("Deterministic with clearance"), CombatTests::RunAndCollectChecksums(Config) == CombatTests::RunAndCollectChecksums(Config));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatClearanceDoorTest, "BattleSystem.Combat.ClearanceDoor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatClearanceDoorTest::RunTest(const FString& Parameters)
{
	// An edge wall between columns 4 and 5 with a one-cell door in row 5; two wide units come from the right, side by
	// side, for a target on the left. Without the wide steering line they pressed each other against the door frame.
	FCombatSimConfig Config;
	Config.Grid.Init(10, 11, 100.f);
	for (int32 Y = 0; Y < 11; ++Y)
	{
		if (Y != 5)
		{
			Config.Grid.AddEdgeWall(FIntPoint(4, Y), FIntPoint(5, Y));
		}
	}
	Config.MaxFirstAttackDelayTicks = 0;
	Config.WallClearance = 45.f;
	FCombatUnitStats Wide = CombatTests::MakeStats(100.f, 1.f, 20, 6);
	Wide.Radius = 60.f;
	CombatTests::AddUnit(Config, CombatTests::MakeDummyStats(), 0, FIntPoint(1, 5));
	CombatTests::AddUnit(Config, Wide, 1, FIntPoint(7, 4));
	CombatTests::AddUnit(Config, Wide, 1, FIntPoint(7, 6));

	FCombatSimulation Simulation(Config);
	bool bCrossed[2] = { false, false };
	for (int32 Step = 0; Step < 400 && !(bCrossed[0] && bCrossed[1]); ++Step)
	{
		Simulation.Step();
		for (int32 Index = 0; Index < 2; ++Index)
		{
			bCrossed[Index] |= Simulation.GetUnits()[Index + 1].Position.X < 500.0;
		}
	}
	TestTrue(TEXT("Both wide units get through the one-cell door within 20 s"), bCrossed[0] && bCrossed[1]);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatPieceSlotsTest, "BattleSystem.Combat.PieceSlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatPieceSlotsTest::RunTest(const FString& Parameters)
{
	FCombatPieceDefinition Frame;
	Frame.Id = TEXT("Doors/Frame");
	Frame.Layer = ECombatPieceLayer::Edge;
	Frame.bBlocksWalking = false;
	FCombatPieceDefinition Leaf = Frame;
	Leaf.Id = TEXT("DoorLeaves/Leaf");
	Leaf.Slot = TEXT("Leaf");
	Leaf.bBlocksWalking = true;
	Leaf.bBlocksSight = true;
	FCombatPieceDefinition OtherLeaf = Leaf;
	OtherLeaf.Id = TEXT("DoorLeaves/OtherLeaf");

	FCombatLevel Level = FCombatLevel::MakeEmpty(TEXT("Slots"), 6, 6);
	Level.PlacePiece(Frame.MakePiece(FIntPoint(2, 3), 1));
	TestTrue(TEXT("A leaf goes on the frame's border"), Level.PlacePiece(Leaf.MakePiece(FIntPoint(2, 3), 1)) && Level.Pieces.Num() == 2);
	TestTrue(TEXT("Another leaf replaces the leaf, not the frame"), Level.PlacePiece(OtherLeaf.MakePiece(FIntPoint(2, 3), 1)) && Level.Pieces.Num() == 2
		&& Level.Pieces[0].Id == Frame.Id && Level.Pieces[1].Id == OtherLeaf.Id);

	FCombatGridData Grid;
	Level.ToGridData(Grid);
	TestTrue(TEXT("The closed door blocks the border"), Grid.HasEdgeWall(FIntPoint(1, 3), FIntPoint(2, 3)));
	TestEqual(TEXT("Picking the border takes the leaf first"), Level.FindPieceUnder(FVector2D(205.0, 350.0)), 1);
	Level.Pieces.RemoveAt(1);
	Level.ToGridData(Grid);
	TestFalse(TEXT("Without the leaf the frame is an opening"), Grid.HasEdgeWall(FIntPoint(1, 3), FIntPoint(2, 3)));
	TestEqual(TEXT("... and picking takes the frame"), Level.FindPieceUnder(FVector2D(205.0, 350.0)), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatWallOpeningsTest, "BattleSystem.Combat.WallOpenings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatWallOpeningsTest::RunTest(const FString& Parameters)
{
	FCombatPieceDefinition Wall;
	Wall.Id = TEXT("Walls/Wall");
	Wall.Layer = ECombatPieceLayer::Edge;
	Wall.Size = FIntPoint(4, 1);
	Wall.bBlocksSight = true;
	FCombatPieceDefinition Frame;
	Frame.Id = TEXT("Doors/Frame");
	Frame.Layer = ECombatPieceLayer::Edge;
	Frame.Slot = FCombatLevelPiece::OpeningSlot;
	Frame.bBlocksWalking = false;
	FCombatPieceDefinition Window = Frame;
	Window.Id = TEXT("Windows/Window");
	Window.Size = FIntPoint(2, 1);
	Window.bBlocksWalking = true;
	Window.bBlocksSight = true;
	FCombatPieceDefinition Leaf = Frame;
	Leaf.Id = TEXT("DoorLeaves/Leaf");
	Leaf.Slot = FCombatLevelPiece::LeafSlot;
	Leaf.bBlocksWalking = true;

	// A 4 m wall on the border between rows 1 and 2, columns 0..3; a door frame on column 1, a window on 2..3.
	FCombatLevel Level = FCombatLevel::MakeEmpty(TEXT("Openings"), 6, 6);
	Level.PlacePiece(Wall.MakePiece(FIntPoint(0, 2), 0));
	TestTrue(TEXT("A door frame stands in the wall"), Level.PlacePiece(Frame.MakePiece(FIntPoint(1, 2), 0)) && Level.Pieces.Num() == 2);
	TestTrue(TEXT("A window stands in the wall too"), Level.PlacePiece(Window.MakePiece(FIntPoint(2, 2), 0)) && Level.Pieces.Num() == 3);

	FCombatGridData Grid;
	Level.ToGridData(Grid);
	TestTrue(TEXT("The wall blocks where nothing is in it"), Grid.HasEdgeWall(FIntPoint(0, 1), FIntPoint(0, 2)));
	TestFalse(TEXT("The door frame makes its border passable"), Grid.HasEdgeWall(FIntPoint(1, 1), FIntPoint(1, 2)));
	TestTrue(TEXT("The window still blocks"), Grid.HasEdgeWall(FIntPoint(2, 1), FIntPoint(2, 2)) && Grid.HasEdgeWall(FIntPoint(3, 1), FIntPoint(3, 2)));
	Level.PlacePiece(Leaf.MakePiece(FIntPoint(1, 2), 0));
	Level.ToGridData(Grid);
	TestTrue(TEXT("With a door leaf the door is closed"), Grid.HasEdgeWall(FIntPoint(1, 1), FIntPoint(1, 2)));

	TestTrue(TEXT("The frame shares a border with the wall"), CombatPieces::SharesBorder(Level.Pieces[0], Level.Pieces[1]));
	TestFalse(TEXT("... the window not with the frame"), CombatPieces::SharesBorder(Level.Pieces[1], Level.Pieces[2]));

	// The cut box: the frame's box, a cell deep on both sides of the border line (row border at Y = 200).
	const FCombatLevelPiece& FramePiece = Level.Pieces[1];
	const FBox FrameBox(FVector(-60.0, -10.0, 0.0), FVector(60.0, 10.0, 240.0));
	const FTransform FrameTransform(FVector(150.0, 200.0, 0.0));
	const FBox Cut = CombatPieces::ComputeCutBox(FramePiece, FrameTransform, FrameBox, 100.f, 100.f);
	TestTrue(TEXT("Cut box: the frame's width and height, through the wall"), Cut.Min.Equals(FVector(90.0, 100.0, 0.0)) && Cut.Max.Equals(FVector(210.0, 300.0, 240.0)));

	// In the space of a wall turned 90 degrees and scaled to half its length.
	const FTransform WallTransform(FRotator(0.0, 90.0, 0.0), FVector(100.0, 0.0, 0.0), FVector(0.5, 1.0, 1.0));
	const FBox Local(FVector(80.0, 10.0, 0.0), FVector(120.0, 30.0, 50.0));
	const FBox InMesh = CombatPieces::ToMeshSpace(Local, WallTransform);
	TestTrue(TEXT("Back into the mesh's own space"), InMesh.Min.Equals(FVector(20.0, -20.0, 0.0), 0.01) && InMesh.Max.Equals(FVector(60.0, 20.0, 50.0), 0.01));

	const FBox LowCut = CombatPieces::ComputeLowCutBox(FBox(FVector(0.0, -10.0, 0.0), FVector(400.0, 10.0, 300.0)), 40.f);
	TestTrue(TEXT("Lowering cuts everything above 40 cm"), LowCut.IsValid && LowCut.Min.Z == 40.0 && LowCut.Max.Z > 300.0 && LowCut.Min.X < 0.0 && LowCut.Max.X > 400.0);
	TestFalse(TEXT("A piece wholly above it has no low version"), CombatPieces::ComputeLowCutBox(FBox(FVector(0.0, -10.0, 90.0), FVector(200.0, 10.0, 250.0)), 40.f).IsValid != 0);

	TestEqual(TEXT("Equal cuts, equal key"), CombatPieces::MakeCutKey(TEXT("Wall"), { Cut }), CombatPieces::MakeCutKey(TEXT("Wall"), { Cut.ShiftBy(FVector(0.01)) }));
	TestNotEqual(TEXT("Another cut, another key"), CombatPieces::MakeCutKey(TEXT("Wall"), { Cut }), CombatPieces::MakeCutKey(TEXT("Wall"), { Cut.ShiftBy(FVector(5.0)) }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatWallItemsTest, "BattleSystem.Combat.WallItems",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatWallItemsTest::RunTest(const FString& Parameters)
{
	// A wall along the border between rows 4 and 5, columns 2..4.
	FCombatLevelPiece Wall;
	Wall.Id = TEXT("Test/Wall");
	Wall.Layer = ECombatPieceLayer::Edge;
	Wall.Cell = FIntPoint(2, 5);
	Wall.Size = FIntPoint(3, 1);
	Wall.bBlocksWalking = true;
	Wall.bBlocksSight = true;
	auto MakeItem = [](int32 Facing, FIntPoint Cell, int32 Detail, int32 Width)
	{
		FCombatLevelPiece Item;
		Item.Id = TEXT("Test/Painting");
		Item.Layer = ECombatPieceLayer::Wall;
		Item.Facing = Facing;
		Item.Cell = Cell;
		Item.Detail = Detail;
		Item.DetailGrid = 4;
		Item.Size = FIntPoint(Width, 1);
		Item.Height = 150.f;
		return Item;
	};

	FCombatLevel Level = FCombatLevel::MakeEmpty(TEXT("Wall items"), 10, 10);
	Level.PlacePiece(Wall);
	FCombatGridData Bare;
	Level.ToGridData(Bare);
	TestTrue(TEXT("An item on the wall (positions 9..12: borders of columns 2 and 3)"), Level.PlacePiece(MakeItem(1, FIntPoint(2, 5), 1, 4)));
	TestFalse(TEXT("Not where there is no wall"), Level.PlacePiece(MakeItem(1, FIntPoint(6, 5), 0, 2)));
	TestFalse(TEXT("Not sticking out past the wall's end"), Level.PlacePiece(MakeItem(1, FIntPoint(4, 5), 2, 4)));
	TestTrue(TEXT("The other side of the wall too"), Level.PlacePiece(MakeItem(3, FIntPoint(2, 5), 1, 4)) && Level.Pieces.Num() == 3);
	TestTrue(TEXT("An overlapping item on the same side replaces the first"), Level.PlacePiece(MakeItem(1, FIntPoint(3, 5), 0, 2))
		&& Level.Pieces.Num() == 3 && Level.Pieces[2].Facing == 1 && Level.Pieces[2].Cell == FIntPoint(3, 5));

	FCombatGridData Grid;
	Level.ToGridData(Grid);
	TestTrue(TEXT("Wall items do not change the grid"), Grid.IsWalkable(FIntPoint(2, 5)) && !Grid.BlocksSight(FIntPoint(2, 5))
		&& Grid.HasEdgeWall(FIntPoint(2, 4), FIntPoint(2, 5)) && !Grid.HasEdgeWall(FIntPoint(5, 4), FIntPoint(5, 5))
		&& Grid.IsWalkable(FIntPoint(3, 4)) == Bare.IsWalkable(FIntPoint(3, 4)));

	// Placing under the cursor: the nearest border line, the cursor's side, centered on it.
	FCombatPieceDefinition Definition;
	Definition.Id = TEXT("Test/Painting");
	Definition.Layer = ECombatPieceLayer::Wall;
	Definition.Size = FIntPoint(4, 1);
	Definition.DetailGrid = 4;
	Definition.MountHeight = 120.f;
	const FCombatLevelPiece Below = CombatPieces::PlaceAt(Definition, FVector2D(250.0, 520.0), 0, 100.f);
	TestTrue(TEXT("Below the line it faces +Y, starting at position 8"), Below.Facing == 1 && Below.Cell == FIntPoint(2, 5) && Below.Detail == 0
		&& Below.Height == 120.f && !Below.bBlocksWalking);
	TestEqual(TEXT("Above the line it faces -Y"), CombatPieces::PlaceAt(Definition, FVector2D(250.0, 480.0), 0, 100.f).Facing, 3);
	TestEqual(TEXT("Beside a column line it faces +-X"), CombatPieces::PlaceAt(Definition, FVector2D(310.0, 250.0), 0, 100.f).Facing, 0);

	TestEqual(TEXT("Picking takes the item on the cursor's side"), Level.FindPieceUnder(FVector2D(330.0, 510.0)), 2);
	TestEqual(TEXT("... and on the other side the other one"), Level.FindPieceUnder(FVector2D(260.0, 490.0)), 1);

	// An opening in the wall takes the items on its border with it.
	FCombatLevelPiece Frame = Wall;
	Frame.Slot = FCombatLevelPiece::OpeningSlot;
	Frame.Cell = FIntPoint(3, 5);
	Frame.Size = FIntPoint(1, 1);
	Frame.bBlocksWalking = false;
	Frame.bBlocksSight = false;
	TestTrue(TEXT("An opening removes the items on its border"), Level.PlacePiece(Frame) && Level.Pieces.Num() == 2);

	// Without its wall an item goes too.
	FCombatLevel Bare2 = FCombatLevel::MakeEmpty(TEXT("Wall items 2"), 10, 10);
	Bare2.PlacePiece(Wall);
	Bare2.PlacePiece(MakeItem(1, FIntPoint(2, 5), 1, 4));
	Bare2.Pieces.RemoveAt(0);
	TestEqual(TEXT("Removing the wall leaves the item unsupported"), Bare2.RemoveUnsupportedWallItems(), 1);

	// Where the mesh goes: centered over positions 9..12 (x 275), in front of the wall's face (5) by half its depth (2).
	const FBox Bounds(FVector(-50.0, -2.0, -25.0), FVector(50.0, 2.0, 25.0));
	FCombatLevelPiece Item = MakeItem(1, FIntPoint(2, 5), 1, 4);
	FTransform Transform = CombatPieces::ComputeWallItemTransform(Item, 100.f, Bounds, 0.f, FVector::ZeroVector, 5.0);
	TestTrue(TEXT("At its height in front of the wall"), Transform.GetLocation().Equals(FVector(275.0, 507.0, 150.0), 0.01));
	TestTrue(TEXT("Its front (-Y in the mesh) faces +Y"), Transform.TransformVectorNoScale(FVector(0.0, -1.0, 0.0)).Equals(FVector(0.0, 1.0, 0.0), 0.001));
	Item.Rotation = 8;
	Transform = CombatPieces::ComputeWallItemTransform(Item, 100.f, Bounds, 0.f, FVector::ZeroVector, 5.0);
	TestTrue(TEXT("Tilted a quarter turn its width stands upright"), FMath::IsNearlyEqual(FMath::Abs(Transform.TransformVectorNoScale(FVector::XAxisVector).Z), 1.0, 0.001));

	FString Json;
	FCombatLevel Loaded;
	FCombatLevel WithItem = FCombatLevel::MakeEmpty(TEXT("Wall items 3"), 10, 10);
	WithItem.PlacePiece(Wall);
	WithItem.PlacePiece(MakeItem(3, FIntPoint(2, 5), 1, 4));
	WithItem.Pieces.Last().Height = 165.f;
	TestTrue(TEXT("Writes and reads"), CombatLevels::ToJson(WithItem, Json) && CombatLevels::FromJson(Json, Loaded));
	TestTrue(TEXT("Facing and height survive"), Loaded.Pieces.Num() == 2 && Loaded.Pieces[1].Layer == ECombatPieceLayer::Wall
		&& Loaded.Pieces[1].Facing == 3 && Loaded.Pieces[1].Height == 165.f && Loaded.Pieces[1].Detail == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatNavClearanceTest, "BattleSystem.Combat.NavClearance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCombatNavClearanceTest::RunTest(const FString& Parameters)
{
	// Classes: the radius rounded up to (class + 0.5) sub-cells of 33.3 cm.
	TestEqual(TEXT("10 cm: class 0"), CombatNavigation::GetClearanceClass(10.f, 100.f), 0);
	TestEqual(TEXT("40 cm: class 1"), CombatNavigation::GetClearanceClass(40.f, 100.f), 1);
	TestEqual(TEXT("50 cm: still class 1"), CombatNavigation::GetClearanceClass(50.f, 100.f), 1);
	TestEqual(TEXT("60 cm: capped at class 1, so it still fits through a one-cell door"), CombatNavigation::GetClearanceClass(60.f, 100.f), 1);
	TestEqual(TEXT("The cap is class 1"), CombatNavigation::MaxClass, 1);
	TestTrue(TEXT("Class 1 keeps 50 cm"), FMath::IsNearlyEqual(CombatNavigation::GetClassClearance(1, 100.f), 50.f, 0.01f));

	// An edge wall between columns 4 and 5 with a one-cell door in row 5, and a blocked cell (7, 1).
	FCombatGridData Grid;
	Grid.Init(10, 11, 100.f);
	for (int32 Y = 0; Y < 11; ++Y)
	{
		if (Y != 5)
		{
			Grid.AddEdgeWall(FIntPoint(4, Y), FIntPoint(5, Y));
		}
	}
	Grid.AddFlags(FIntPoint(7, 1), ECombatCellFlags::Blocked);
	FCombatGridData Small;
	FCombatGridData Large;
	CombatNavigation::BuildNavGrid(Grid, 1, Small);
	CombatNavigation::BuildNavGrid(Grid, 2, Large);
	TestTrue(TEXT("Three sub-cells per cell side"), Small.Width == 30 && Small.Height == 33 && FMath::IsNearlyEqual(Small.CellSize, 100.f / 3.f));
	TestTrue(TEXT("Class 1: the door's middle sub-cells are open, its sides are not"), Small.IsWalkable(FIntPoint(14, 16)) && Small.IsWalkable(FIntPoint(15, 16))
		&& !Small.IsWalkable(FIntPoint(14, 15)) && !Small.IsWalkable(FIntPoint(15, 17)));
	TestTrue(TEXT("... and the wall's border is copied onto the sub-cells"), Small.HasEdgeWall(FIntPoint(14, 3), FIntPoint(15, 3)));
	TestTrue(TEXT("Class 1: sub-cells beside a blocked cell close, the middle of the next cell stays open"),
		!Small.IsWalkable(FIntPoint(20, 4)) && Small.IsWalkable(FIntPoint(19, 4)) && !Small.IsWalkable(FIntPoint(21, 4)));
	TestFalse(TEXT("Class 2: the door is closed"), Large.IsWalkable(FIntPoint(14, 16)) || Large.IsWalkable(FIntPoint(15, 16)));

	TArray<FIntPoint> Path;
	const FIntPoint Right(25, 16);
	const FIntPoint Left(4, 16);
	TestTrue(TEXT("Class 1 finds a route through the door"), CombatPathfinding::FindPath(Small, Right, Left, Path));
	TestFalse(TEXT("Class 2 finds none"), CombatPathfinding::FindPath(Large, Right, Left, Path));

	FIntPoint Open;
	TestTrue(TEXT("A position in a closed sub-cell snaps to the nearest open one"),
		CombatNavigation::FindOpenCell(Small, FVector2D(510.0, 530.0), Open) && Open == FIntPoint(15, 16));

	// In a fight: a small unit (class 0) and a 60 cm one (capped at class 1) each use their own nav grid; both reach the
	// target behind the wall through the door.
	FCombatSimConfig Config;
	Config.Grid = Grid;
	Config.MaxFirstAttackDelayTicks = 0;
	FCombatUnitStats Small15 = CombatTests::MakeStats(100.f, 1.f, 20, 6);
	Small15.Radius = 15.f;
	FCombatUnitStats Wide = Small15;
	Wide.Radius = 60.f;
	CombatTests::AddUnit(Config, CombatTests::MakeDummyStats(), 0, FIntPoint(1, 5));
	CombatTests::AddUnit(Config, Small15, 1, FIntPoint(8, 5));
	CombatTests::AddUnit(Config, Wide, 1, FIntPoint(8, 8));
	FCombatSimulation Simulation(Config);
	Simulation.Step();
	TestEqual(TEXT("Two classes in this fight (the dummy's 40 cm is class 1 too)"), Simulation.GetNavClassCount(), 2);
	const FCombatUnit& SmallUnit = Simulation.GetUnits()[1];
	const FCombatUnit& WideUnit = Simulation.GetUnits()[2];
	TestTrue(TEXT("Each unit has its own class"), Simulation.GetClearanceClass(SmallUnit.NavClass) == 0 && Simulation.GetClearanceClass(WideUnit.NavClass) == 1);
	const FIntPoint WideCell = Simulation.GetNavGrid(WideUnit.NavClass).LocalToCell(WideUnit.Position);
	TestEqual(TEXT("The wide unit's map reaches the target through the door"), Simulation.GetDistanceMap(1, WideUnit.NavClass)->GetNearestId(WideCell), 0);

	bool bCrossed[2] = { false, false };
	for (int32 Step = 0; Step < 400 && !(bCrossed[0] && bCrossed[1]); ++Step)
	{
		Simulation.Step();
		for (int32 Index = 0; Index < 2; ++Index)
		{
			bCrossed[Index] |= Simulation.GetUnits()[Index + 1].Position.X < 500.0;
		}
	}
	TestTrue(TEXT("Both walk through the door"), bCrossed[0] && bCrossed[1]);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
