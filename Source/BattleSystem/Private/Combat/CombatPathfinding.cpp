// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatPathfinding.h"
#include "Algo/Reverse.h"
#include "Combat/CombatDistanceMap.h"
#include "Combat/CombatGridData.h"

namespace
{
	struct FOpenNode
	{
		int32 F;
		int32 H;
		int32 CellIndex;
	};

	/** Strict total order (lowest F, then lowest H, then lowest cell index), so the search is deterministic. */
	struct FOpenNodeLess
	{
		bool operator()(const FOpenNode& A, const FOpenNode& B) const
		{
			if (A.F != B.F)
			{
				return A.F < B.F;
			}
			if (A.H != B.H)
			{
				return A.H < B.H;
			}
			return A.CellIndex < B.CellIndex;
		}
	};

	/** Octile distance in cost units; admissible for 8-way moves with costs 10/14. */
	int32 Heuristic(const FIntPoint& A, const FIntPoint& B)
	{
		const int32 DX = FMath::Abs(A.X - B.X);
		const int32 DY = FMath::Abs(A.Y - B.Y);
		return FCombatDistanceMap::StraightCost * FMath::Max(DX, DY)
			+ (FCombatDistanceMap::DiagonalCost - FCombatDistanceMap::StraightCost) * FMath::Min(DX, DY);
	}
}

bool CombatPathfinding::FindPath(const FCombatGridData& Grid, const FIntPoint& Start, const FIntPoint& Goal, TArray<FIntPoint>& OutPath)
{
	OutPath.Reset();
	if (!Grid.IsWalkable(Start) || !Grid.IsWalkable(Goal))
	{
		return false;
	}

	const int32 NumCells = Grid.Width * Grid.Height;
	TArray<int32> Costs;
	TArray<int32> Parents;
	Costs.Init(MAX_int32, NumCells);
	Parents.Init(INDEX_NONE, NumCells);

	const int32 StartIndex = Grid.CellIndex(Start);
	const int32 GoalIndex = Grid.CellIndex(Goal);
	Costs[StartIndex] = 0;

	TArray<FOpenNode> Open;
	Open.HeapPush({ Heuristic(Start, Goal), Heuristic(Start, Goal), StartIndex }, FOpenNodeLess());

	while (!Open.IsEmpty())
	{
		FOpenNode Node;
		Open.HeapPop(Node, FOpenNodeLess());
		if (Node.CellIndex == GoalIndex)
		{
			break;
		}

		const FIntPoint Cell(Node.CellIndex % Grid.Width, Node.CellIndex / Grid.Width);
		const int32 Cost = Costs[Node.CellIndex];

		// Skip entries that were improved after they were pushed.
		if (Node.F != Cost + Node.H)
		{
			continue;
		}

		for (int32 Dir = 0; Dir < 8; ++Dir)
		{
			const FIntPoint& Offset = FCombatGridData::NeighborOffsets[Dir];
			if (!Grid.CanStep(Cell, Offset))
			{
				continue;
			}

			const FIntPoint Next = Cell + Offset;
			const int32 NextIndex = Grid.CellIndex(Next);
			const int32 NextCost = Cost + (Offset.X != 0 && Offset.Y != 0 ? FCombatDistanceMap::DiagonalCost : FCombatDistanceMap::StraightCost);
			if (NextCost < Costs[NextIndex])
			{
				Costs[NextIndex] = NextCost;
				Parents[NextIndex] = Node.CellIndex;
				const int32 H = Heuristic(Next, Goal);
				Open.HeapPush({ NextCost + H, H, NextIndex }, FOpenNodeLess());
			}
		}
	}

	if (Costs[GoalIndex] == MAX_int32)
	{
		return false;
	}

	for (int32 Index = GoalIndex; Index != INDEX_NONE; Index = Parents[Index])
	{
		OutPath.Add(FIntPoint(Index % Grid.Width, Index / Grid.Width));
	}
	Algo::Reverse(OutPath);
	return true;
}

int32 CombatPathfinding::PathCost(TConstArrayView<FIntPoint> Path)
{
	int32 Cost = 0;
	for (int32 Index = 1; Index < Path.Num(); ++Index)
	{
		const FIntPoint Step = Path[Index] - Path[Index - 1];
		Cost += Step.X != 0 && Step.Y != 0 ? FCombatDistanceMap::DiagonalCost : FCombatDistanceMap::StraightCost;
	}
	return Cost;
}
