// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatGridData.h"
#include "Misc/Crc.h"

void FCombatGridData::Init(int32 InWidth, int32 InHeight, float InCellSize)
{
	Width = FMath::Max(InWidth, 1);
	Height = FMath::Max(InHeight, 1);
	CellSize = FMath::Max(InCellSize, 1.f);
	Cells.Init(ECombatCellFlags::None, Width * Height);
	Edges.Reset();
}

namespace CombatGridDataPrivate
{
	/** The cell that stores the border between two orthogonal neighbors, and its bit; false for other pairs. */
	static bool FindEdge(const FCombatGridData& Grid, const FIntPoint& A, const FIntPoint& B, FIntPoint& OutOwner, uint8& OutBit)
	{
		const FIntPoint Delta = B - A;
		if (FMath::Abs(Delta.X) + FMath::Abs(Delta.Y) != 1)
		{
			return false;
		}
		// The border is stored on the cell with the larger coordinate: west for X, north for Y.
		OutOwner = (Delta.X > 0 || Delta.Y > 0) ? B : A;
		OutBit = Delta.X != 0 ? FCombatGridData::EdgeWest : FCombatGridData::EdgeNorth;
		// The outer border of the grid already blocks: only borders between two cells of the grid count.
		return Grid.IsInBounds(A) && Grid.IsInBounds(B);
	}
}

void FCombatGridData::AddEdgeWall(const FIntPoint& A, const FIntPoint& B)
{
	FIntPoint Owner;
	uint8 Bit = 0;
	if (CombatGridDataPrivate::FindEdge(*this, A, B, Owner, Bit))
	{
		if (Edges.IsEmpty())
		{
			Edges.Init(0, Width * Height);
		}
		Edges[CellIndex(Owner)] |= Bit;
	}
}

bool FCombatGridData::HasEdgeWall(const FIntPoint& A, const FIntPoint& B) const
{
	FIntPoint Owner;
	uint8 Bit = 0;
	return !Edges.IsEmpty() && CombatGridDataPrivate::FindEdge(*this, A, B, Owner, Bit) && (Edges[CellIndex(Owner)] & Bit) != 0;
}

bool FCombatGridData::CrossesNoEdgeWall(const FVector2D& From, const FVector2D& To) const
{
	return !HasEdgeWalls() || IsLineClear(From, To, [](const FIntPoint&) { return true; });
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
	Crc = FCrc::MemCrc32(Cells.GetData(), Cells.Num() * sizeof(ECombatCellFlags), Crc);
	return HasEdgeWalls() ? FCrc::MemCrc32(Edges.GetData(), Edges.Num(), Crc) : Crc;
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
	const FIntPoint Target = Cell + Offset;
	if (!IsWalkable(Target))
	{
		return false;
	}
	if (Offset.X != 0 && Offset.Y != 0)
	{
		const FIntPoint SideX(Cell.X + Offset.X, Cell.Y);
		const FIntPoint SideY(Cell.X, Cell.Y + Offset.Y);
		return IsWalkable(SideX) && IsWalkable(SideY)
			&& !HasEdgeWall(Cell, SideX) && !HasEdgeWall(SideX, Target)
			&& !HasEdgeWall(Cell, SideY) && !HasEdgeWall(SideY, Target);
	}
	return !HasEdgeWall(Cell, Target);
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
			if (HasEdgeWall(Cell, FIntPoint(Cell.X + StepX, Cell.Y)))
			{
				return false;
			}
			Cell.X += StepX;
			TMaxX += TDeltaX;
		}
		else if (TMaxY < TMaxX)
		{
			if (HasEdgeWall(Cell, FIntPoint(Cell.X, Cell.Y + StepY)))
			{
				return false;
			}
			Cell.Y += StepY;
			TMaxY += TDeltaY;
		}
		else
		{
			// Exactly through a corner: both side cells must be clear, and no edge wall may touch the corner on either route.
			const FIntPoint SideX(Cell.X + StepX, Cell.Y);
			const FIntPoint SideY(Cell.X, Cell.Y + StepY);
			const FIntPoint Diagonal(Cell.X + StepX, Cell.Y + StepY);
			if (!IsCellClear(SideX) || !IsCellClear(SideY)
				|| HasEdgeWall(Cell, SideX) || HasEdgeWall(SideX, Diagonal) || HasEdgeWall(Cell, SideY) || HasEdgeWall(SideY, Diagonal))
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
