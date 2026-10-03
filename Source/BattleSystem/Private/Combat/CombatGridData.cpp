// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatGridData.h"

void FCombatGridData::Init(int32 InWidth, int32 InHeight, float InCellSize)
{
	Width = FMath::Max(InWidth, 1);
	Height = FMath::Max(InHeight, 1);
	CellSize = FMath::Max(InCellSize, 1.f);
	Cells.Init(ECombatCellFlags::None, Width * Height);
}

FIntPoint FCombatGridData::LocalToCell(const FVector2D& Local) const
{
	return FIntPoint(FMath::FloorToInt32(Local.X / CellSize), FMath::FloorToInt32(Local.Y / CellSize));
}

FVector2D FCombatGridData::CellToLocal(const FIntPoint& Cell) const
{
	return FVector2D((Cell.X + 0.5) * CellSize, (Cell.Y + 0.5) * CellSize);
}

bool FCombatGridData::HasFlags(const FIntPoint& Cell, ECombatCellFlags Flags) const
{
	return IsInBounds(Cell) && EnumHasAllFlags(Cells[CellIndex(Cell)], Flags);
}

void FCombatGridData::AddFlags(const FIntPoint& Cell, ECombatCellFlags Flags)
{
	if (IsInBounds(Cell))
	{
		Cells[CellIndex(Cell)] |= Flags;
	}
}
