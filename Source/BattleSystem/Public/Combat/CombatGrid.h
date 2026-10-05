// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Combat/CombatGridData.h"
#include "Combat/CombatLevel.h"
#include "CombatGrid.generated.h"

class UInstancedStaticMeshComponent;

/** How border pieces (walls) are shown, so units behind them stay visible (key V, control panel). */
enum class ECombatWallMode : uint8
{
	/** Full height. */
	Up,
	/** Walls that hide a unit from the camera are lowered. */
	Cutaway,
	/** All walls lowered. */
	Down,
};
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
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

	/** The arena's own grid with obstacle flags, built on first use (also while a level is shown). */
	const FCombatGridData& GetGridData();

	/**
	 * Shows a level (LevelDesigner) instead of the arena's own size and obstacles: the floor resizes, walls,
	 * hedges and water get blocks, and the placed obstacles are hidden. Fights from a level use the level's grid.
	 */
	void ApplyLevel(const FCombatLevel& Level);
	/** Back to the arena's own size and obstacles. */
	void ClearLevel();
	bool HasLevel() const { return bHasLevel; }
	/** Grid shown right now: the level's, or the arena's own. */
	const FCombatGridData& GetShownGridData();

	/**
	 * LevelDesigner preview of a piece: its mesh at MeshTransform (when placing) and a colored plate per footprint cell
	 * or bar per border: PreviewPlaceColor, PreviewBlockedColor when it does not fit, PreviewEraseColor when erasing.
	 */
	void ShowPiecePreview(const FCombatLevelPiece& Piece, float InCellSize, UStaticMesh* Mesh, const FTransform& MeshTransform, bool bFits, bool bErase);
	void HidePiecePreview();

	/**
	 * Lowers border pieces to LowWallHeight: all of them (Down), none (Up), or those whose full-height bounds the line
	 * from CameraLocation to one of Targets crosses (Cutaway). Only changes components whose state changes.
	 */
	void UpdateWalls(ECombatWallMode Mode, const FVector& CameraLocation, TConstArrayView<FVector> Targets);

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
	/** Resizes the floor to a grid and redraws the debug cells for it. */
	void ShowGrid(const FCombatGridData& Data);
	void DrawDebugCells(const FCombatGridData& Data) const;
	void SetObstaclesHidden(bool bHideObstacles);

	UPROPERTY(VisibleAnywhere, Category = "Grid")
	TObjectPtr<UStaticMeshComponent> FloorMesh;

	/** Level blocks: one instance per wall, hedge or water cell. */
	UPROPERTY(VisibleAnywhere, Category = "Grid")
	TObjectPtr<UInstancedStaticMeshComponent> WallBlocks;

	UPROPERTY(VisibleAnywhere, Category = "Grid")
	TObjectPtr<UInstancedStaticMeshComponent> HedgeBlocks;

	UPROPERTY(VisibleAnywhere, Category = "Grid")
	TObjectPtr<UInstancedStaticMeshComponent> WaterBlocks;

	UPROPERTY(EditDefaultsOnly, Category = "Grid|Level")
	TObjectPtr<UMaterialInterface> BlockMaterialBase;

	/** Block heights in a level; low, so they read like a game board next to the 10 cm units. */
	UPROPERTY(EditAnywhere, Category = "Grid|Level", meta = (ClampMin = 1, Units = "cm"))
	float WallHeight = 30.f;

	UPROPERTY(EditAnywhere, Category = "Grid|Level", meta = (ClampMin = 1, Units = "cm"))
	float HedgeHeight = 20.f;

	UPROPERTY(EditAnywhere, Category = "Grid|Level", meta = (ClampMin = 1, Units = "cm"))
	float WaterHeight = 6.f;

	UPROPERTY(EditDefaultsOnly, Category = "Grid|Preview")
	FLinearColor PreviewPlaceColor = FLinearColor(0.2f, 0.9f, 0.3f);

	UPROPERTY(EditDefaultsOnly, Category = "Grid|Preview")
	FLinearColor PreviewBlockedColor = FLinearColor(0.95f, 0.15f, 0.1f);

	UPROPERTY(EditDefaultsOnly, Category = "Grid|Preview")
	FLinearColor PreviewEraseColor = FLinearColor(1.f, 0.5f, 0.05f);

	/** LevelDesigner piece preview: the piece's mesh, and its footprint marks (engine cubes in the preview color). */
	UPROPERTY(VisibleAnywhere, Category = "Grid")
	TObjectPtr<UStaticMeshComponent> PreviewMesh;

	UPROPERTY(VisibleAnywhere, Category = "Grid")
	TObjectPtr<UInstancedStaticMeshComponent> PreviewMarks;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PreviewMaterial;

	/** How far the grid's own floor sinks while a level has floor pieces, so the two never fight at the same height. */
	UPROPERTY(EditAnywhere, Category = "Grid|Level", meta = (ClampMin = 0, Units = "cm"))
	float PieceFloorDrop = 2.f;

	UPROPERTY(EditDefaultsOnly, Category = "Grid|Level")
	FLinearColor WallColor = FLinearColor(0.3f, 0.3f, 0.32f);

	UPROPERTY(EditDefaultsOnly, Category = "Grid|Level")
	FLinearColor HedgeColor = FLinearColor(0.1f, 0.45f, 0.12f);

	UPROPERTY(EditDefaultsOnly, Category = "Grid|Level")
	FLinearColor WaterColor = FLinearColor(0.08f, 0.3f, 0.85f);

	/**
	 * Shows the level's pieces with the meshes of the settings' PieceCatalog: one plain static mesh component per piece
	 * (no instancing, so pack materials need no instancing usage flag; UE batches equal meshes itself).
	 */
	void ShowPieces(const FCombatLevel& Level);
	void ClearPieces();

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> PieceComponents;

	/** Border pieces taller than LowWallHeight, parallel arrays: component, full and lowered transform, full world bounds. */
	struct FWallPiece
	{
		TWeakObjectPtr<UStaticMeshComponent> Component;
		FTransform FullTransform;
		FTransform LowTransform;
		FBox WorldBounds;
		bool bLowered = false;
	};
	TArray<FWallPiece> WallPieces;

	bool bHasLevel = false;
	FCombatGridData LevelGridData;

	FCombatGridData GridData;
	bool bCellsBuilt = false;
};
