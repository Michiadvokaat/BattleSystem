// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Per-cell flags of the combat grid. A cell without flags is walkable and see-through. */
enum class ECombatCellFlags : uint8
{
	None = 0,
	Blocked = 1 << 0,
	BlocksSight = 1 << 1,
};
ENUM_CLASS_FLAGS(ECombatCellFlags);

/**
 * Plain snapshot of the combat grid, shared by ACombatGrid and FCombatSimulation.
 * Positions are local to the grid: (0,0) is the corner of cell (0,0), in cm, in the XY plane.
 */
struct BATTLESYSTEM_API FCombatGridData
{
	int32 Width = 0;
	int32 Height = 0;
	float CellSize = 100.f;

	/** Width * Height entries, row-major (index = Y * Width + X). */
	TArray<ECombatCellFlags> Cells;

	void Init(int32 InWidth, int32 InHeight, float InCellSize);

	bool IsInBounds(const FIntPoint& Cell) const
	{
		return Cell.X >= 0 && Cell.Y >= 0 && Cell.X < Width && Cell.Y < Height;
	}

	int32 CellIndex(const FIntPoint& Cell) const { return Cell.Y * Width + Cell.X; }

	/** Cell containing a local position. May be out of bounds. */
	FIntPoint LocalToCell(const FVector2D& Local) const;

	/** Local position of the center of a cell. */
	FVector2D CellToLocal(const FIntPoint& Cell) const;

	/** Size of the whole grid in cm. */
	FVector2D GetLocalSize() const { return FVector2D(Width * CellSize, Height * CellSize); }

	bool HasFlags(const FIntPoint& Cell, ECombatCellFlags Flags) const;
	void AddFlags(const FIntPoint& Cell, ECombatCellFlags Flags);

	/** Out-of-bounds cells are not walkable. */
	bool IsWalkable(const FIntPoint& Cell) const { return IsInBounds(Cell) && !HasFlags(Cell, ECombatCellFlags::Blocked); }
	bool BlocksSight(const FIntPoint& Cell) const { return !IsInBounds(Cell) || HasFlags(Cell, ECombatCellFlags::BlocksSight); }
};
