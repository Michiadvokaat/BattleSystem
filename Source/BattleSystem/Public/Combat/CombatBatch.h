// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/CombatSimulation.h"

struct FCombatBatchFight
{
	int32 Seed = 0;
	ECombatOutcome Outcome = ECombatOutcome::InProgress;
	int32 WinningTeam = INDEX_NONE;
	int32 Ticks = 0;
	uint32 Checksum = 0;
};

/** Totals for one unit type (definition) over all fights of a batch. */
struct FCombatBatchUnitType
{
	FString Name;
	/** Units of this type summed over all fights. */
	int32 Units = 0;
	double DamageDealt = 0.0;
	double DamageTaken = 0.0;
	int32 Survived = 0;
};

struct FCombatBatchTeam
{
	int32 Team = 0;
	int32 Wins = 0;
};

struct BATTLESYSTEM_API FCombatBatchResult
{
	TArray<FCombatBatchFight> Fights;
	/** In order of first appearance in the setup. */
	TArray<FCombatBatchTeam> Teams;
	TArray<FCombatBatchUnitType> UnitTypes;
	int32 Draws = 0;
	int32 TimeLimits = 0;
	double ElapsedSeconds = 0.0;

	/** Readable report: win rates, durations, and per unit type averages. */
	FString ToSummary(const FString& SetupName, float TickRate) const;

	/** Writes <BasePath>_fights.csv and <BasePath>_units.csv. */
	bool WriteCsv(const FString& BasePath, float TickRate) const;
};

namespace CombatBatch
{
	/**
	 * Runs Count fights headless with seeds StartSeed, StartSeed + 1, ... on copies of BaseConfig.
	 * UnitTypeNames gives the type name per FCombatUnit::SourceIndex (for the per-type totals).
	 */
	BATTLESYSTEM_API FCombatBatchResult Run(const FCombatSimConfig& BaseConfig, TConstArrayView<FString> UnitTypeNames, int32 Count, int32 StartSeed);
}
