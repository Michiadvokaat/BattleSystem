// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatBatch.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"

FCombatBatchResult CombatBatch::Run(const FCombatSimConfig& BaseConfig, TConstArrayView<FString> UnitTypeNames, int32 Count, int32 StartSeed)
{
	FCombatBatchResult Result;
	const double StartTime = FPlatformTime::Seconds();

	// Teams and unit types in order of first appearance, so the report order is stable.
	TArray<int32> UnitTypeIndex;
	for (int32 UnitId = 0; UnitId < BaseConfig.Units.Num(); ++UnitId)
	{
		const int32 Team = BaseConfig.Units[UnitId].Team;
		if (!Result.Teams.ContainsByPredicate([Team](const FCombatBatchTeam& Entry) { return Entry.Team == Team; }))
		{
			Result.Teams.Add({ Team, 0 });
		}

		const FString Name = UnitTypeNames.IsValidIndex(UnitId) ? UnitTypeNames[UnitId] : FString::Printf(TEXT("Unit %d"), UnitId);
		int32 TypeIndex = Result.UnitTypes.IndexOfByPredicate([&Name](const FCombatBatchUnitType& Type) { return Type.Name == Name; });
		if (TypeIndex == INDEX_NONE)
		{
			TypeIndex = Result.UnitTypes.Num();
			Result.UnitTypes.AddDefaulted_GetRef().Name = Name;
		}
		UnitTypeIndex.Add(TypeIndex);
	}

	Result.Fights.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FCombatSimConfig Config = BaseConfig;
		Config.Seed = StartSeed + Index;

		FCombatSimulation Simulation(Config);
		Simulation.RunToEnd();

		FCombatBatchFight& Fight = Result.Fights.AddDefaulted_GetRef();
		Fight.Seed = Config.Seed;
		Fight.Outcome = Simulation.GetOutcome();
		Fight.WinningTeam = Simulation.GetWinningTeam();
		Fight.Ticks = Simulation.GetTick();
		Fight.Checksum = Simulation.GetChecksum();

		switch (Fight.Outcome)
		{
		case ECombatOutcome::TeamWon:
			for (FCombatBatchTeam& Team : Result.Teams)
			{
				Team.Wins += Team.Team == Fight.WinningTeam ? 1 : 0;
			}
			break;
		case ECombatOutcome::Draw:
			++Result.Draws;
			break;
		default:
			++Result.TimeLimits;
			break;
		}

		for (const FCombatUnit& Unit : Simulation.GetUnits())
		{
			FCombatBatchUnitType& Type = Result.UnitTypes[UnitTypeIndex[Unit.Id]];
			++Type.Units;
			Type.DamageDealt += Unit.DamageDealt;
			Type.DamageTaken += Unit.DamageTaken;
			Type.Survived += Unit.bAlive ? 1 : 0;
		}
	}

	Result.ElapsedSeconds = FPlatformTime::Seconds() - StartTime;
	return Result;
}

FString FCombatBatchResult::ToSummary(const FString& SetupName, float TickRate) const
{
	const int32 Count = FMath::Max(Fights.Num(), 1);
	int32 MinTicks = MAX_int32;
	int32 MaxTicks = 0;
	double TotalTicks = 0.0;
	for (const FCombatBatchFight& Fight : Fights)
	{
		MinTicks = FMath::Min(MinTicks, Fight.Ticks);
		MaxTicks = FMath::Max(MaxTicks, Fight.Ticks);
		TotalTicks += Fight.Ticks;
	}
	if (Fights.IsEmpty())
	{
		MinTicks = 0;
	}

	FString Summary = FString::Printf(TEXT("Batch %s: %d fights in %.2f s (%.2f ms per fight)\n"),
		*SetupName, Fights.Num(), ElapsedSeconds, ElapsedSeconds * 1000.0 / Count);
	for (const FCombatBatchTeam& Team : Teams)
	{
		Summary += FString::Printf(TEXT("  Team %d wins: %d (%.1f%%)\n"), Team.Team, Team.Wins, 100.0 * Team.Wins / Count);
	}
	Summary += FString::Printf(TEXT("  Draws: %d, time limits: %d\n"), Draws, TimeLimits);
	Summary += FString::Printf(TEXT("  Duration: avg %.1f s, min %.1f s, max %.1f s\n"),
		TotalTicks / Count / TickRate, MinTicks / TickRate, MaxTicks / TickRate);
	for (const FCombatBatchUnitType& Type : UnitTypes)
	{
		const int32 Units = FMath::Max(Type.Units, 1);
		Summary += FString::Printf(TEXT("  %s: dealt %.0f, taken %.0f, survives %.0f%% (per unit, avg)\n"),
			*Type.Name, Type.DamageDealt / Units, Type.DamageTaken / Units, 100.0 * Type.Survived / Units);
	}
	return Summary;
}

bool FCombatBatchResult::WriteCsv(const FString& BasePath, float TickRate) const
{
	FString FightsCsv = TEXT("Seed,Outcome,WinningTeam,Ticks,Seconds,Checksum\n");
	for (const FCombatBatchFight& Fight : Fights)
	{
		FightsCsv += FString::Printf(TEXT("%d,%s,%d,%d,%.2f,0x%08X\n"), Fight.Seed, FCombatSimulation::OutcomeToString(Fight.Outcome),
			Fight.WinningTeam, Fight.Ticks, Fight.Ticks / TickRate, Fight.Checksum);
	}

	FString UnitsCsv = TEXT("UnitType,Units,AvgDamageDealt,AvgDamageTaken,SurvivalRate\n");
	for (const FCombatBatchUnitType& Type : UnitTypes)
	{
		const int32 Units = FMath::Max(Type.Units, 1);
		UnitsCsv += FString::Printf(TEXT("%s,%d,%.2f,%.2f,%.4f\n"), *Type.Name, Type.Units,
			Type.DamageDealt / Units, Type.DamageTaken / Units, static_cast<double>(Type.Survived) / Units);
	}

	return FFileHelper::SaveStringToFile(FightsCsv, *(BasePath + TEXT("_fights.csv")))
		&& FFileHelper::SaveStringToFile(UnitsCsv, *(BasePath + TEXT("_units.csv")));
}
