// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CombatSettings.generated.h"

class UCombatSetup;

/** Combat tuning values. Project Settings > Game > Combat, saved to DefaultGame.ini. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Combat"))
class BATTLESYSTEM_API UCombatSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	/** Simulation steps per second. */
	UPROPERTY(Config, EditAnywhere, Category = "Simulation", meta = (ClampMin = 1, ClampMax = 120))
	int32 TickRate = 20;

	/** Maximum simulation steps per frame; protects against a "spiral of death" on slow frames. */
	UPROPERTY(Config, EditAnywhere, Category = "Simulation", meta = (ClampMin = 1))
	int32 MaxStepsPerFrame = 5;

	/** A fight ends as a time-out after this many seconds (rounded to ticks). */
	UPROPERTY(Config, EditAnywhere, Category = "Simulation", meta = (ClampMin = 1, Units = "s"))
	float FightTimeLimit = 120.f;

	/** Once a unit first reaches attack range, its first attack waits a random 0..this many seconds, drawn from the fight's seed. */
	UPROPERTY(Config, EditAnywhere, Category = "Simulation", meta = (ClampMin = 0, Units = "s"))
	float MaxFirstAttackDelay = 0.5f;

	/** How often the per-team distance maps (and so the targets) are rebuilt; they are also rebuilt after every death. */
	UPROPERTY(Config, EditAnywhere, Category = "Movement", meta = (ClampMin = 0.05, Units = "s"))
	float RetargetInterval = 0.25f;

	/** How many cells ahead on its route a unit looks for the farthest visible point to steer to. */
	UPROPERTY(Config, EditAnywhere, Category = "Movement", meta = (ClampMin = 1, ClampMax = 32))
	int32 PathLookaheadCells = 8;

	/** Fraction of the overlap between two units that is pushed apart per tick. */
	UPROPERTY(Config, EditAnywhere, Category = "Movement", meta = (ClampMin = 0, ClampMax = 1))
	float SeparationStrength = 0.5f;

	/** Start a fight automatically when an arena with ACombatGameMode begins play. */
	UPROPERTY(Config, EditAnywhere, Category = "Arena")
	bool bAutoStartFight = false;

	/** Setup used by the auto-start and by Combat.Start/Combat.Simulate without a setup argument. */
	UPROPERTY(Config, EditAnywhere, Category = "Arena")
	TSoftObjectPtr<UCombatSetup> DefaultSetup;

	/** Seed used by the auto-start. */
	UPROPERTY(Config, EditAnywhere, Category = "Arena")
	int32 DefaultSeed = 42;

	/** Grid used by Combat.Simulate when the current level has no ACombatGrid. */
	UPROPERTY(Config, EditAnywhere, Category = "Arena")
	FIntPoint FallbackGridSize = FIntPoint(20, 12);

	UPROPERTY(Config, EditAnywhere, Category = "Arena", meta = (ClampMin = 10, Units = "cm"))
	float FallbackCellSize = 100.f;

	/** Unit color per team index (wraps around). */
	UPROPERTY(Config, EditAnywhere, Category = "Presentation")
	TArray<FLinearColor> TeamColors;

	FLinearColor GetTeamColor(int32 Team) const;
	int32 SecondsToTicks(float Seconds) const { return FMath::RoundToInt32(Seconds * TickRate); }
};
