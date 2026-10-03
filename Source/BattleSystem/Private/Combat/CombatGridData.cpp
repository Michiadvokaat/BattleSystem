// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatGridData.h"
#include "Misc/Crc.h"

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

uint32 FCombatGridData::ComputeChecksum() const
{
	uint32 Crc = FCrc::MemCrc32(&Width, sizeof(Width));
	Crc = FCrc::MemCrc32(&Height, sizeof(Height), Crc);
	Crc = FCrc::MemCrc32(&CellSize, sizeof(CellSize), Crc);
	return FCrc::MemCrc32(Cells.GetData(), Cells.Num() * sizeof(ECombatCellFlags), Crc);
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

const FIntPoint FCombatGridData::NeighborOffsets[8] =
{
	FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1),
	FIntPoint(1, 1), FIntPoint(1, -1), FIntPoint(-1, 1), FIntPoint(-1, -1),
};

bool FCombatGridData::CanStep(const FIntPoint& Cell, const FIntPoint& Offset) const
{
	if (!IsWalkable(Cell + Offset))
	{
		return false;
	}
	if (Offset.X != 0 && Offset.Y != 0)
	{
		return IsWalkable(FIntPoint(Cell.X + Offset.X, Cell.Y)) && IsWalkable(FIntPoint(Cell.X, Cell.Y + Offset.Y));
	}
	return true;
}

bool FCombatGridData::IsLineWalkable(const FVector2D& From, const FVector2D& To) const
{
	return IsLineClear(From, To, [this](const FIntPoint& Cell) { return IsWalkable(Cell); });
}

bool FCombatGridData::HasLineOfSight(const FVector2D& From, const FVector2D& To) const
{
	return IsLineClear(From, To, [this](const FIntPoint& Cell) { return !BlocksSight(Cell); });
}

bool FCombatGridData::IsLineClear(const FVector2D& From, const FVector2D& To, TFunctionRef<bool(const FIntPoint&)> IsCellClear) const
{
	// Grid traversal (Amanatides & Woo): visit every cell the segment crosses, in order.
	FIntPoint Cell = LocalToCell(From);
	const FIntPoint EndCell = LocalToCell(To);
	if (!IsCellClear(Cell))
	{
		return false;
	}

	const FVector2D Delta = To - From;
	const int32 StepX = Delta.X > 0.0 ? 1 : (Delta.X < 0.0 ? -1 : 0);
	const int32 StepY = Delta.Y > 0.0 ? 1 : (Delta.Y < 0.0 ? -1 : 0);

	// T runs from 0 (From) to 1 (To). TMax = T at the next cell border, TDelta = T per cell.
	constexpr double Never = TNumericLimits<double>::Max();
	const double TDeltaX = StepX != 0 ? CellSize / FMath::Abs(Delta.X) : Never;
	const double TDeltaY = StepY != 0 ? CellSize / FMath::Abs(Delta.Y) : Never;
	double TMaxX = StepX > 0 ? ((Cell.X + 1) * CellSize - From.X) / Delta.X : (StepX < 0 ? (Cell.X * CellSize - From.X) / Delta.X : Never);
	double TMaxY = StepY > 0 ? ((Cell.Y + 1) * CellSize - From.Y) / Delta.Y : (StepY < 0 ? (Cell.Y * CellSize - From.Y) / Delta.Y : Never);

	int32 StepsLeft = FMath::Abs(EndCell.X - Cell.X) + FMath::Abs(EndCell.Y - Cell.Y);
	while (Cell != EndCell && StepsLeft-- > 0)
	{
		if (TMaxX < TMaxY)
		{
			Cell.X += StepX;
			TMaxX += TDeltaX;
		}
		else if (TMaxY < TMaxX)
		{
			Cell.Y += StepY;
			TMaxY += TDeltaY;
		}
		else
		{
			// Exactly through a corner: both side cells must be clear.
			if (!IsCellClear(FIntPoint(Cell.X + StepX, Cell.Y)) || !IsCellClear(FIntPoint(Cell.X, Cell.Y + StepY)))
			{
				return false;
			}
			Cell.X += StepX;
			Cell.Y += StepY;
			TMaxX += TDeltaX;
			TMaxY += TDeltaY;
			--StepsLeft;
		}

		if (!IsCellClear(Cell))
		{
			return false;
		}
	}
	return true;
}
