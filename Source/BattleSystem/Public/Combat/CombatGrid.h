// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Combat/CombatGridData.h"
#include "CombatGrid.generated.h"

class UStaticMeshComponent;

/**
 * The arena grid, placed by hand (one per level). The actor location is the corner of cell (0,0);
 * the grid lies in the XY plane and ignores actor rotation and scale.
 * Cell flags come from the ACombatObstacles in the level and do not change during a fight.
 */
UCLASS()
class BATTLESYSTEM_API ACombatGrid : public AActor
{
	GENERATED_BODY()

public:
	ACombatGrid();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid", meta = (ClampMin = 10, Units = "cm"))
	float CellSize = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid", meta = (ClampMin = 1))
	int32 Width = 20;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid", meta = (ClampMin = 1))
	int32 Height = 12;

	/** Draw cell lines and blocked cells during play. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid|Debug")
	bool bDrawDebugCells = true;

	/** The grid with obstacle flags, built on first use. */
	const FCombatGridData& GetGridData();

	FIntPoint WorldToCell(const FVector& World) const;
	/** World position of the center of a cell, at the grid's height. */
	FVector CellToWorld(const FIntPoint& Cell) const;
	bool IsInBounds(const FIntPoint& Cell) const { return Cell.X >= 0 && Cell.Y >= 0 && Cell.X < Width && Cell.Y < Height; }

	FVector LocalToWorld(const FVector2D& Local) const { return GetActorLocation() + FVector(Local.X, Local.Y, 0.0); }
	FVector2D WorldToLocal(const FVector& World) const;

	/** The first ACombatGrid in the world, or null. */
	static ACombatGrid* Find(const UWorld* World);

	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	virtual void BeginPlay() override;

private:
	void BuildCells();
	void DrawDebugCells() const;

	UPROPERTY(VisibleAnywhere, Category = "Grid")
	TObjectPtr<UStaticMeshComponent> FloorMesh;

	FCombatGridData GridData;
	bool bCellsBuilt = false;
};
