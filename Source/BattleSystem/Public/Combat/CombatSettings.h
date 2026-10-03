// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "CombatSettings.generated.h"

class UCombatCueTable;
class UCombatSetup;

/** How an active effect is shown on a unit. */
USTRUCT()
struct FCombatStatusIcon
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Status", meta = (Categories = "Effect"))
	FGameplayTag EffectTag;

	/** Short text, for example "T". A stack count is added after it (S2). */
	UPROPERTY(EditAnywhere, Category = "Status")
	FString Label;

	UPROPERTY(EditAnywhere, Category = "Status")
	FLinearColor Color = FLinearColor::White;
};

/** How threat decreases over time. */
UENUM()
enum class ECombatThreatDecayMode : uint8
{
	/** Threat halves every ThreatHalfLife seconds: large values drop fast, small ones linger. */
	HalfLife,
	/** ThreatDecayPerSecond is subtracted every second: whoever built up a lot keeps it long. */
	Linear,
};

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

	/** How threat decreases over time. */
	UPROPERTY(Config, EditAnywhere, Category = "Threat")
	ECombatThreatDecayMode ThreatDecayMode = ECombatThreatDecayMode::HalfLife;

	/** HalfLife mode: seconds in which threat halves. */
	UPROPERTY(Config, EditAnywhere, Category = "Threat", meta = (ClampMin = 0.1, Units = "s", EditCondition = "ThreatDecayMode == ECombatThreatDecayMode::HalfLife"))
	float ThreatHalfLife = 4.f;

	/** Linear mode: threat subtracted per second (down to 0). */
	UPROPERTY(Config, EditAnywhere, Category = "Threat", meta = (ClampMin = 0, EditCondition = "ThreatDecayMode == ECombatThreatDecayMode::Linear"))
	float ThreatDecayPerSecond = 5.f;

	/** Threat below this does not count for targeting. */
	UPROPERTY(Config, EditAnywhere, Category = "Threat", meta = (ClampMin = 0))
	float ThreatThreshold = 5.f;

	/** A unit on a threat target only switches to an enemy with this many times more threat (1.2 = 20% more). */
	UPROPERTY(Config, EditAnywhere, Category = "Threat", meta = (ClampMin = 1))
	float ThreatSwitchRatio = 1.2f;

	/** A unit on a nearest target only switches to an enemy that is this much closer. */
	UPROPERTY(Config, EditAnywhere, Category = "Threat", meta = (ClampMin = 0, Units = "cm"))
	float RetargetDistanceMargin = 150.f;

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

	/** Maps cue tags to VFX, sound and debug colors. */
	UPROPERTY(Config, EditAnywhere, Category = "Presentation")
	TSoftObjectPtr<UCombatCueTable> CueTable;

	/** How long the flash of an area attack going off stays visible. */
	UPROPERTY(Config, EditAnywhere, Category = "Presentation", meta = (ClampMin = 0, Units = "s"))
	float AreaPulseDuration = 0.4f;

	/** Outline of a telegraphed area until it goes off. */
	UPROPERTY(Config, EditAnywhere, Category = "Presentation")
	FLinearColor TelegraphColor = FLinearColor(1.f, 0.5f, 0.f);

	/** Flash color of an area attack without a cue in the cue table (taunts use TauntColor). */
	UPROPERTY(Config, EditAnywhere, Category = "Presentation")
	FLinearColor DefaultAreaColor = FLinearColor(1.f, 0.15f, 0.f);

	UPROPERTY(Config, EditAnywhere, Category = "Presentation")
	FLinearColor TauntColor = FLinearColor(1.f, 0.f, 1.f);

	/** Labels for active effects on units. An effect not listed shows the first letter of its tag's last part, in white. */
	UPROPERTY(Config, EditAnywhere, Category = "Presentation")
	TArray<FCombatStatusIcon> StatusIcons;

	/** Unit color per team index (wraps around). */
	UPROPERTY(Config, EditAnywhere, Category = "Presentation")
	TArray<FLinearColor> TeamColors;

	FLinearColor GetTeamColor(int32 Team) const;
	int32 SecondsToTicks(float Seconds) const { return FMath::RoundToInt32(Seconds * TickRate); }
};
