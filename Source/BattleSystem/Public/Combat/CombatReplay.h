// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/CombatLevel.h"
#include "Combat/CombatTypes.h"
#include "Combat/CombatUnitData.h"
#include "CombatReplay.generated.h"

struct FCombatSimConfig;

/**
 * Every setting that changes how a fight plays out, in simulation units (ticks, factors per tick).
 * Taken from UCombatSettings (plus the panel's taunt range) for a normal fight, or from a replay file.
 */
USTRUCT()
struct BATTLESYSTEM_API FCombatSimSettings
{
	GENERATED_BODY()

	UPROPERTY() int32 TickRate = 20;
	UPROPERTY() int32 MaxTicks = 2400;
	UPROPERTY() int32 MaxFirstAttackDelayTicks = 10;
	UPROPERTY() int32 RetargetIntervalTicks = 5;
	UPROPERTY() int32 PathLookaheadCells = 8;
	UPROPERTY() float SeparationStrength = 0.5f;
	/** cm; 0 in replays saved before it existed, so they play as they were recorded. */
	UPROPERTY() float WallClearance = 0.f;
	UPROPERTY() float ThreatDecayFactorPerTick = 1.f;
	UPROPERTY() float ThreatDecayAmountPerTick = 0.f;
	UPROPERTY() float ThreatThreshold = 5.f;
	UPROPERTY() float ThreatSwitchRatio = 1.2f;
	UPROPERTY() float RetargetDistanceMargin = 150.f;
	/** cm for every Attack.Taunt; 0 = the skill's Range. */
	UPROPERTY() float TauntRangeOverride = 0.f;
	/** Pause before the first wave and after each cleared wave. */
	UPROPERTY() int32 WavePauseTicks = 100;

	/** The current project settings (Project Settings > Game > Combat), with the given taunt range override. */
	static FCombatSimSettings FromProjectSettings(float InTauntRangeOverride = 0.f);

	/** Writes the settings into a config whose units are already filled (the taunt override changes their stats). */
	void ApplyTo(FCombatSimConfig& Config) const;
};

/** A replay file: enough to play a fight again and check that it comes out the same. */
USTRUCT()
struct BATTLESYSTEM_API FCombatReplay
{
	GENERATED_BODY()

	/**
	 * 1 = no commands; 2 = with the command log and checkpoints; 3 = can hold a level; 4 = always a level (no setup assets);
	 * 5 = holds the unit and skill rows (no unit definition assets). Older ones do not play.
	 */
	UPROPERTY() int32 FormatVersion = 5;
	UPROPERTY() FString SavedAt;
	/** Engine build; a replay is only guaranteed identical on the same build. */
	UPROPERTY() FString BuildVersion;
	/** A full copy of the fight's level, so the replay stays identical when the level file changes. */
	UPROPERTY() FCombatLevel Level;
	/** The rows of the fight's units and their skills, so the replay stays identical when the tables are tuned. */
	UPROPERTY() FCombatUnitCatalog Units;
	UPROPERTY() int32 Seed = 0;
	UPROPERTY() FCombatSimSettings Settings;

	/** Every player command, with the tick it ran at; replayed exactly. */
	UPROPERTY() TArray<FCombatCommand> Commands;
	/** For information: the delay between giving and running a command when it was recorded. */
	UPROPERTY() int32 CommandDelayTicks = 0;

	/** Checksum (hex) after every CheckpointInterval-th tick, to find where a different replay starts to differ. */
	UPROPERTY() int32 CheckpointInterval = 0;
	UPROPERTY() TArray<FString> Checkpoints;

	/** The recorded result, to compare against. */
	UPROPERTY() int32 Ticks = 0;
	UPROPERTY() FString Outcome;
	UPROPERTY() int32 WinningTeam = INDEX_NONE;
	UPROPERTY() FString FinalChecksum;
};

namespace CombatReplay
{
	/** Saved/Replays/ */
	BATTLESYSTEM_API FString GetReplayDirectory();
	/** File names (not paths) of all replays, newest first. */
	BATTLESYSTEM_API TArray<FString> FindReplayFiles();
	/** A bare file name is looked up in the replay directory; anything else is used as a path. */
	BATTLESYSTEM_API FString ResolvePath(const FString& FileOrPath);

	BATTLESYSTEM_API bool SaveToFile(const FCombatReplay& Replay, const FString& Path);
	BATTLESYSTEM_API bool LoadFromFile(const FString& Path, FCombatReplay& OutReplay);

	BATTLESYSTEM_API bool ToJson(const FCombatReplay& Replay, FString& OutJson);
	BATTLESYSTEM_API bool FromJson(const FString& Json, FCombatReplay& OutReplay);

	BATTLESYSTEM_API FString ChecksumToString(uint32 Checksum);
}
