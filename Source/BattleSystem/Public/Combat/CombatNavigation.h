// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/CombatGridData.h"

/**
 * The navigation layer (phase 6): the combat grid split into Subdivision x Subdivision sub-cells, one nav grid per
 * clearance class. A sub-cell is open when its center keeps the class's clearance from every unwalkable cell (also
 * outside the grid) and every edge wall; the edge walls are copied onto the sub-cell borders, so routes never cross
 * them. Distance maps, A* and the route lookahead run on the nav grid of a unit's class; movement and line checks
 * stay on the combat grid.
 */
namespace CombatNavigation
{
	/** Sub-cells per cell side (fixed). */
	inline constexpr int32 Subdivision = 3;

	/**
	 * The highest clearance class: the one whose clearance still fits through a one-cell door (its middle sub-cell),
	 * so every unit can pass one: bigger units use this class too.
	 */
	inline constexpr int32 MaxClass = (Subdivision - 1) / 2;

	/**
	 * The clearance class of a unit radius: the radius rounded up to the distances at which sub-cell centers lie from
	 * a cell border, (Class + 0.5) sub-cells, at most MaxClass. With 100 cm cells: up to 16.7 cm class 0, above that
	 * class 1 (50 cm, through a one-cell door).
	 */
	BATTLESYSTEM_API int32 GetClearanceClass(float Radius, float CellSize);

	/** The clearance (cm) the sub-cells of a class keep from walls: (Class + 0.5) sub-cells. */
	BATTLESYSTEM_API float GetClassClearance(int32 Class, float CellSize);

	/** Builds the nav grid of a clearance class from a combat grid. */
	BATTLESYSTEM_API void BuildNavGrid(const FCombatGridData& Grid, int32 Class, FCombatGridData& OutNav);

	/**
	 * The open sub-cell for a grid-local position: the one it lies in, else the nearest open one within MaxRings rings
	 * around it (by distance to the position; the first in row order on ties). False if there is none.
	 */
	BATTLESYSTEM_API bool FindOpenCell(const FCombatGridData& Nav, const FVector2D& Local, FIntPoint& OutCell, int32 MaxRings = 3);
}
