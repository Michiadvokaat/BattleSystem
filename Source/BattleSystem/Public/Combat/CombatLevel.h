// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/CombatGridData.h"
#include "CombatLevel.generated.h"

class UCombatUnitDefinition;
struct FCombatSimConfig;

USTRUCT()
struct FCombatLevelUnit
{
	GENERATED_BODY()

	/** Asset name of the UCombatUnitDefinition, for example "DA_Krijger". */
	UPROPERTY() FString Type;
	UPROPERTY() int32 Team = 0;
	UPROPERTY() FIntPoint Cell = FIntPoint::ZeroValue;
};

/** An enemy (wave team) that appears during a wave. */
USTRUCT()
struct FCombatLevelSpawn
{
	GENERATED_BODY()

	/** Asset name of the UCombatUnitDefinition. */
	UPROPERTY() FString Type;
	UPROPERTY() FIntPoint Cell = FIntPoint::ZeroValue;
	/** Seconds after the start of its wave. */
	UPROPERTY() float Time = 0.f;
};

USTRUCT()
struct FCombatLevelWave
{
	GENERATED_BODY()

	UPROPERTY() TArray<FCombatLevelSpawn> Spawns;
};

/**
 * A level built with the LevelDesigner: grid size, cells, starting units and enemy waves. Saved as readable JSON in
 * <Project>/Levels/<Name>.json and copied into replays. Cells are text rows, one character per cell.
 */
USTRUCT()
struct BATTLESYSTEM_API FCombatLevel
{
	GENERATED_BODY()

	static constexpr TCHAR Open = TEXT('.');
	/** Blocks walking and sight. */
	static constexpr TCHAR Wall = TEXT('#');
	/** Blocks sight only. */
	static constexpr TCHAR Hedge = TEXT('h');
	/** Blocks walking only. */
	static constexpr TCHAR Water = TEXT('~');

	static constexpr int32 MinSize = 5;
	static constexpr int32 MaxSize = 40;

	/** 1 = no waves; 2 = with waves (version 1 files load as levels without waves). */
	UPROPERTY() int32 FormatVersion = 2;
	UPROPERTY() FString Name;
	UPROPERTY() int32 Width = 20;
	UPROPERTY() int32 Height = 12;
	UPROPERTY() float CellSize = 100.f;
	/** Height rows of Width characters; row Y is cells (0..Width-1, Y). Unknown characters count as open. */
	UPROPERTY() TArray<FString> Rows;
	UPROPERTY() TArray<FCombatLevelUnit> Units;
	/** Enemy waves, in order; see FCombatSimConfig::Waves for when each starts. */
	UPROPERTY() TArray<FCombatLevelWave> Waves;

	/** An open level of the given size (clamped to MinSize..MaxSize). */
	static FCombatLevel MakeEmpty(const FString& InName, int32 InWidth, int32 InHeight);

	/** Clamps the size and makes every row exactly Width characters (padding with open cells). */
	void Normalize();

	/** Changes the size; cells, units and spawns outside the new size are removed. */
	void Resize(int32 NewWidth, int32 NewHeight);

	bool IsInBounds(const FIntPoint& Cell) const { return Cell.X >= 0 && Cell.Y >= 0 && Cell.X < Width && Cell.Y < Height; }
	TCHAR GetCell(const FIntPoint& Cell) const;
	void SetCell(const FIntPoint& Cell, TCHAR Kind);
	static ECombatCellFlags FlagsFor(TCHAR Kind);

	/** Index in Units of the unit on Cell, or INDEX_NONE. */
	int32 FindUnitAt(const FIntPoint& Cell) const;
	/** Index in Waves[WaveIndex].Spawns of the spawn on Cell, or INDEX_NONE. */
	int32 FindSpawnAt(int32 WaveIndex, const FIntPoint& Cell) const;
	/** Removes the spawns on Cell from every wave; returns whether there were any. */
	bool RemoveSpawnsAt(const FIntPoint& Cell);

	void ToGridData(FCombatGridData& OutGrid) const;
};

namespace CombatLevels
{
	/** <Project>/Levels/ */
	BATTLESYSTEM_API FString GetDirectory();
	/** Names (file names without .json) of all saved levels, sorted. */
	BATTLESYSTEM_API TArray<FString> FindLevelNames();
	BATTLESYSTEM_API bool Save(const FCombatLevel& Level);
	BATTLESYSTEM_API bool Load(const FString& Name, FCombatLevel& OutLevel);
	/** A level name as a file name: only letters, digits, - and _ are kept (may come out empty). */
	BATTLESYSTEM_API FString CleanName(const FString& Name);
	BATTLESYSTEM_API bool Exists(const FString& Name);
	/** Renames Levels/<OldName>.json to <NewName>.json and sets the name inside. False if the old one is missing or the new one exists. */
	BATTLESYSTEM_API bool Rename(const FString& OldName, const FString& NewName);
	BATTLESYSTEM_API bool Delete(const FString& Name);

	BATTLESYSTEM_API bool ToJson(const FCombatLevel& Level, FString& OutJson);
	BATTLESYSTEM_API bool FromJson(const FString& Json, FCombatLevel& OutLevel);

	/**
	 * Builds a simulation config (grid, units and waves) from a level. Resolve turns a unit type name into its
	 * definition; units and spawns of an unknown type or on an unwalkable cell are skipped with a warning.
	 * OutDefinitions gets the definition per FCombatUnit::SourceIndex: the units, then every wave's spawns.
	 * Settings (tick rate and the rest) are applied by the caller.
	 */
	BATTLESYSTEM_API bool BuildConfig(const FCombatLevel& Level, int32 TickRate, TFunctionRef<const UCombatUnitDefinition*(const FString&)> Resolve,
		FCombatSimConfig& OutConfig, TArray<const UCombatUnitDefinition*>* OutDefinitions = nullptr);
}
