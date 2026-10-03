// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Combat/CombatSimulation.h"

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
		Stats.bHasAttack = true;
		Stats.AttackRange = 100.f;
		Stats.AttackDamage = Damage;
		Stats.AttackCooldownTicks = CooldownTicks;
		Stats.AttackWindupTicks = WindupTicks;
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

#endif // WITH_DEV_AUTOMATION_TESTS
