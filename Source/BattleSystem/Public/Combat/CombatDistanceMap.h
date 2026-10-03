// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FCombatGridData;

/**
 * Walking distance from every grid cell to the nearest of a set of source units (multi-source Dijkstra),
 * plus which source that is. 8 directions without corner cutting; costs are integers (10 straight,
 * 14 diagonal) so equal routes compare exactly. Ties go to the source with the lowest unit ID.
 */
class BATTLESYSTEM_API FCombatDistanceMap
{
public:
	static constexpr int32 Unreachable = MAX_int32;
	static constexpr int32 StraightCost = 10;
	static constexpr int32 DiagonalCost = 14;

	struct FSource
	{
		FIntPoint Cell;
		int32 UnitId;
	};

	void Build(const FCombatGridData& Grid, TConstArrayView<FSource> Sources);

	/** Distance in cost units (10 per cell), or Unreachable. */
	int32 GetDistance(const FIntPoint& Cell) const;
	/** ID of the nearest source, or INDEX_NONE if unreachable. */
	int32 GetNearestId(const FIntPoint& Cell) const;

	/** The walkable neighbor with the lowest distance, if it is lower than Cell's own. First in neighbor order wins ties. */
	bool GetNextCell(const FCombatGridData& Grid, const FIntPoint& Cell, FIntPoint& OutNext) const;

private:
	int32 Width = 0;
	int32 Height = 0;
	TArray<int32> Distances;
	TArray<int32> NearestIds;
};
