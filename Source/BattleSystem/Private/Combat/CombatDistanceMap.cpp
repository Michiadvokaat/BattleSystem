// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatDistanceMap.h"
#include "Combat/CombatGridData.h"

namespace
{
	struct FOpenEntry
	{
		int32 Distance;
		int32 UnitId;
		int32 CellIndex;
	};

	/** Strict total order, so the search is deterministic. */
	struct FOpenEntryLess
	{
		bool operator()(const FOpenEntry& A, const FOpenEntry& B) const
		{
			if (A.Distance != B.Distance)
			{
				return A.Distance < B.Distance;
			}
			if (A.UnitId != B.UnitId)
			{
				return A.UnitId < B.UnitId;
			}
			return A.CellIndex < B.CellIndex;
		}
	};
}

void FCombatDistanceMap::Build(const FCombatGridData& Grid, TConstArrayView<FSource> Sources)
{
	Width = Grid.Width;
	Height = Grid.Height;
	Distances.Init(Unreachable, Width * Height);
	NearestIds.Init(INDEX_NONE, Width * Height);

	// (Distance, UnitId) is better if shorter, or equally short with a lower unit ID.
	auto IsBetter = [this](int32 Index, int32 Distance, int32 UnitId)
	{
		return Distance < Distances[Index] || (Distance == Distances[Index] && UnitId < NearestIds[Index]);
	};

	TArray<FOpenEntry> Open;
	for (const FSource& Source : Sources)
	{
		if (!Grid.IsWalkable(Source.Cell))
		{
			continue;
		}
		const int32 Index = Grid.CellIndex(Source.Cell);
		if (IsBetter(Index, 0, Source.UnitId))
		{
			Distances[Index] = 0;
			NearestIds[Index] = Source.UnitId;
			Open.HeapPush({ 0, Source.UnitId, Index }, FOpenEntryLess());
		}
	}

	while (!Open.IsEmpty())
	{
		FOpenEntry Entry;
		Open.HeapPop(Entry, FOpenEntryLess());

		// Skip entries that were improved after they were pushed.
		if (Entry.Distance != Distances[Entry.CellIndex] || Entry.UnitId != NearestIds[Entry.CellIndex])
		{
			continue;
		}

		const FIntPoint Cell(Entry.CellIndex % Width, Entry.CellIndex / Width);
		for (int32 Dir = 0; Dir < 8; ++Dir)
		{
			const FIntPoint& Offset = FCombatGridData::NeighborOffsets[Dir];
			if (!Grid.CanStep(Cell, Offset))
			{
				continue;
			}

			const int32 NextIndex = Grid.CellIndex(Cell + Offset);
			const int32 NextDistance = Entry.Distance + (Offset.X != 0 && Offset.Y != 0 ? DiagonalCost : StraightCost);
			if (IsBetter(NextIndex, NextDistance, Entry.UnitId))
			{
				Distances[NextIndex] = NextDistance;
				NearestIds[NextIndex] = Entry.UnitId;
				Open.HeapPush({ NextDistance, Entry.UnitId, NextIndex }, FOpenEntryLess());
			}
		}
	}
}

int32 FCombatDistanceMap::GetDistance(const FIntPoint& Cell) const
{
	if (Cell.X < 0 || Cell.Y < 0 || Cell.X >= Width || Cell.Y >= Height)
	{
		return Unreachable;
	}
	return Distances[Cell.Y * Width + Cell.X];
}

int32 FCombatDistanceMap::GetNearestId(const FIntPoint& Cell) const
{
	if (Cell.X < 0 || Cell.Y < 0 || Cell.X >= Width || Cell.Y >= Height)
	{
		return INDEX_NONE;
	}
	return NearestIds[Cell.Y * Width + Cell.X];
}

bool FCombatDistanceMap::GetNextCell(const FCombatGridData& Grid, const FIntPoint& Cell, FIntPoint& OutNext) const
{
	int32 BestDistance = GetDistance(Cell);
	bool bFound = false;
	for (int32 Dir = 0; Dir < 8; ++Dir)
	{
		const FIntPoint& Offset = FCombatGridData::NeighborOffsets[Dir];
		if (!Grid.CanStep(Cell, Offset))
		{
			continue;
		}

		const int32 Distance = GetDistance(Cell + Offset);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			OutNext = Cell + Offset;
			bFound = true;
		}
	}
	return bFound;
}
