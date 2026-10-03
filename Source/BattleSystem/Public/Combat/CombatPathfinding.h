// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FCombatGridData;

namespace CombatPathfinding
{
	/**
	 * A* over the grid: 8 directions without corner cutting, costs 10 (straight) and 14 (diagonal), as in
	 * FCombatDistanceMap. Deterministic: equal routes are broken by a fixed order. OutPath runs from Start
	 * to Goal, both included. Returns false (and an empty path) if Goal cannot be reached.
	 */
	BATTLESYSTEM_API bool FindPath(const FCombatGridData& Grid, const FIntPoint& Start, const FIntPoint& Goal, TArray<FIntPoint>& OutPath);

	/** Cost of a path in the same units as FindPath (10 per straight step, 14 per diagonal step). */
	BATTLESYSTEM_API int32 PathCost(TConstArrayView<FIntPoint> Path);
}
