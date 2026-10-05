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
	/** Start pose (presentation only): eighth turns, 0..7, 0 = facing +X; see CombatLevels::GetUnitYaw. */
	UPROPERTY() int32 Rotation = 0;
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
	/** Pose when it appears (presentation only): eighth turns, 0..7, 0 = facing +X. */
	UPROPERTY() int32 Rotation = 0;
};

USTRUCT()
struct FCombatLevelWave
{
	GENERATED_BODY()

	UPROPERTY() TArray<FCombatLevelSpawn> Spawns;
};

/** Where a piece goes: each cell has one piece per layer (floor, cell, border; detail later). */
UENUM(BlueprintType)
enum class ECombatPieceLayer : uint8
{
	/** Under everything; never blocks. */
	Floor,
	/** Fills its footprint cells: furniture, pillars. Can block walking and/or sight. */
	Cell,
	/** On the borders between cells: walls, windows (block walking and sight), door frames (visual only). */
	Edge,
	/** Small objects on the detail grid inside a cell, one per position; they stand on the Cell piece under them. */
	Detail,
};

/**
 * A piece placed in a level (LevelDesigner). Its footprint and blocking are copied from the catalog when it is
 * placed, so the level, fights and replays never need the catalog: UCombatPieceCatalog only gives the look (by Id).
 */
USTRUCT()
struct BATTLESYSTEM_API FCombatLevelPiece
{
	GENERATED_BODY()

	/** Catalog id, "Category/MeshName". */
	UPROPERTY() FString Id;
	UPROPERTY() ECombatPieceLayer Layer = ECombatPieceLayer::Cell;
	/**
	 * Pieces only replace pieces of the same layer and slot: a wall (""), an opening ("Opening": window, door frame;
	 * cuts the wall visually) and a door leaf ("Leaf") share a border. A non-blocking opening (a door frame) makes the
	 * wall's border passable.
	 */
	UPROPERTY() FString Slot;

	static constexpr const TCHAR* OpeningSlot = TEXT("Opening");
	static constexpr const TCHAR* LeafSlot = TEXT("Leaf");
	/**
	 * Floor and Cell: the footprint's first cell (smallest X and Y). Edge: the cell after the first border, so a
	 * horizontal piece (even rotation) runs along the border between rows Cell.Y - 1 and Cell.Y from column Cell.X on,
	 * a vertical one (odd rotation) along the border between columns Cell.X - 1 and Cell.X from row Cell.Y on.
	 */
	UPROPERTY() FIntPoint Cell = FIntPoint::ZeroValue;
	/** Quarter turns, 0..3; Detail pieces: eighth turns (45 degrees), 0..7. */
	UPROPERTY() int32 Rotation = 0;
	/** Footprint in cells before rotation; Edge: X = the number of borders it covers. */
	UPROPERTY() FIntPoint Size = FIntPoint(1, 1);
	/** Cell pieces: their cells block walking / sight. Edge pieces block both when either is set. */
	UPROPERTY() bool bBlocksWalking = false;
	UPROPERTY() bool bBlocksSight = false;
	/** Detail layer: position on the detail grid of its cell, X + Y x DetailGrid; INDEX_NONE for the other layers. */
	UPROPERTY() int32 Detail = INDEX_NONE;
	/** Detail layer: positions per cell side (copied from the catalog, so Detail keeps its meaning); 0 for the other layers. */
	UPROPERTY() int32 DetailGrid = 0;
	/** Tint of a tintable piece (solid floors; FCombatPieceDefinition::bTintable), sRGB; presentation only. */
	UPROPERTY() FColor Color = FColor::White;

	/** Size after rotation (X and Y swap on odd rotations). */
	FIntPoint GetRotatedSize() const;
	bool IsHorizontalEdge() const { return Rotation % 2 == 0; }
	/** Detail layer: the center of its position on the detail grid, in grid-local cm. */
	FVector2D GetDetailCenter(float CellSize) const;
	/** Floor and Cell: the footprint cells; Detail: its cell; nothing for Edge. */
	void GetCells(TArray<FIntPoint>& OutCells) const;
	/** Edge: the cell pairs on both sides of each border it covers; nothing for the other layers. */
	void GetEdges(TArray<TPair<FIntPoint, FIntPoint>>& OutEdges) const;
	/** Whether it shares a cell (Floor, Cell), a border (Edge) or a detail position (Detail) with another piece of the same layer and slot. */
	bool Overlaps(const FCombatLevelPiece& Other) const;
};

/**
 * A level built with the LevelDesigner: grid size, starting units, enemy waves and placed pieces. Saved as readable
 * JSON in <Project>/Levels/<Name>.json and copied into replays. Every cell is open; only pieces block.
 */
USTRUCT()
struct BATTLESYSTEM_API FCombatLevel
{
	GENERATED_BODY()

	static constexpr int32 MinSize = 5;
	static constexpr int32 MaxSize = 40;

	/**
	 * 1 = no waves; 2 = with waves; 3 = with pieces; 4 = without cell rows; 5 = with unit and spawn rotations; 6 = with
	 * piece colors (older files load without the missing parts; their rows of walls, hedges and water are ignored,
	 * their units face the other side: team 0 +X, the rest and spawns -X, and their pieces are white).
	 */
	UPROPERTY() int32 FormatVersion = 6;
	UPROPERTY() FString Name;
	UPROPERTY() int32 Width = 20;
	UPROPERTY() int32 Height = 12;
	UPROPERTY() float CellSize = 100.f;
	UPROPERTY() TArray<FCombatLevelUnit> Units;
	/** Enemy waves, in order; see FCombatSimConfig::Waves for when each starts. */
	UPROPERTY() TArray<FCombatLevelWave> Waves;
	/** Placed pieces (walls, floors, furniture); only their blocking makes cells and borders unwalkable. */
	UPROPERTY() TArray<FCombatLevelPiece> Pieces;

	/** An open level of the given size (clamped to MinSize..MaxSize). */
	static FCombatLevel MakeEmpty(const FString& InName, int32 InWidth, int32 InHeight);

	/** Clamps the size to MinSize..MaxSize. */
	void Normalize();

	/** Changes the size; units, spawns and pieces outside the new size are removed. */
	void Resize(int32 NewWidth, int32 NewHeight);

	bool IsInBounds(const FIntPoint& Cell) const { return Cell.X >= 0 && Cell.Y >= 0 && Cell.X < Width && Cell.Y < Height; }

	/** Index in Units of the unit on Cell, or INDEX_NONE. */
	int32 FindUnitAt(const FIntPoint& Cell) const;
	/** Index in Waves[WaveIndex].Spawns of the spawn on Cell, or INDEX_NONE. */
	int32 FindSpawnAt(int32 WaveIndex, const FIntPoint& Cell) const;
	/** Removes the spawns on Cell from every wave; returns whether there were any. */
	bool RemoveSpawnsAt(const FIntPoint& Cell);

	/** Whether a piece fits in the grid. Edge pieces may lie on the outer border (visual only there). */
	bool IsPieceInBounds(const FCombatLevelPiece& Piece) const;

	/** Adds a piece and removes the pieces of its layer that it overlaps; false (nothing changes) if it does not fit. */
	bool PlacePiece(const FCombatLevelPiece& Piece);

	/** Index of the piece of a Floor or Cell layer that covers Cell, or INDEX_NONE. */
	int32 FindPieceAt(ECombatPieceLayer Layer, const FIntPoint& Cell) const;

	/**
	 * Index of the topmost piece under a grid-local point (move, erase), or INDEX_NONE: the detail on the detail position
	 * under it, else the Cell piece of its cell, else the Edge piece on the nearest border within EdgeReach cells, else
	 * the Floor piece. Within a layer a piece with a slot (on top, like a door leaf) comes before one without.
	 */
	int32 FindPieceUnder(const FVector2D& Local, float EdgeReach = 0.25f) const;

	void ToGridData(FCombatGridData& OutGrid) const;
};

namespace CombatLevels
{
	/** <Project>/Levels/ */
	BATTLESYSTEM_API FString GetDirectory();
	/** Rotation steps of a unit or spawn (eighth turns) and the yaw in degrees of one. */
	static constexpr int32 UnitRotationSteps = 8;
	inline float GetUnitYaw(int32 Rotation) { return Rotation * 360.f / UnitRotationSteps; }

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
	 * OutDefinitions gets the definition per FCombatUnit::SourceIndex: the units, then every wave's spawns;
	 * OutRotations their start rotation in the same order (presentation only, not in the config).
	 * Settings (tick rate and the rest) are applied by the caller.
	 */
	BATTLESYSTEM_API bool BuildConfig(const FCombatLevel& Level, int32 TickRate, TFunctionRef<const UCombatUnitDefinition*(const FString&)> Resolve,
		FCombatSimConfig& OutConfig, TArray<const UCombatUnitDefinition*>* OutDefinitions = nullptr, TArray<int32>* OutRotations = nullptr);
}
