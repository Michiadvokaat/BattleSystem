// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Combat/CombatGridData.h"
#include "Combat/CombatLevel.h"
#include "CombatGrid.generated.h"

class UDynamicMesh;
class UInstancedStaticMeshComponent;
class UPrimitiveComponent;

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
	void ShowPiecePreview(const FCombatLevelPiece& Piece, float InCellSize, UStaticMesh* Mesh, const FTransform& MeshTransform, bool bFits, bool bErase,
		bool bTinted = false);

	/**
	 * While an opening is previewed, the shown walls on its borders are swapped for versions with its cut box (mesh
	 * space of the opening) cut out as well; recomputed only when the previewed opening moves. Null Opening restores them.
	 */
	void UpdatePreviewCuts(const FCombatLevelPiece* Opening, const FTransform& OpeningTransform, const FBox& CutBox, float InCellSize);
	void HidePiecePreview();

	/**
	 * Lowers border pieces to LowWallHeight: all of them (Down), none (Up), or those whose full-height bounds the line
	 * from CameraLocation to one of Targets crosses (Cutaway). Lowering shows a version cut off at LowWallHeight instead
	 * of the full piece (nothing for a piece wholly above it). Only changes components whose state changes.
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

	/** Base material of the preview marks (engine shape material with a Color parameter). */
	UPROPERTY(EditDefaultsOnly, Category = "Grid|Level")
	TObjectPtr<UMaterialInterface> BlockMaterialBase;

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

	/**
	 * Shows the level's pieces with the meshes of the settings' PieceCatalog: one plain static mesh component per piece
	 * (no instancing, so pack materials need no instancing usage flag; UE batches equal meshes itself). A wall with
	 * openings (windows, door frames) on its borders becomes a dynamic mesh with their cut boxes cut out.
	 */
	void ShowPieces(const FCombatLevel& Level);
	void ClearPieces();

	/** A dynamic mesh component showing Mesh with the boxes (mesh space) cut out; equal cuts come from CutWallCache. */
	UPrimitiveComponent* MakeCutWall(UStaticMesh* Mesh, TConstArrayView<FBox> MeshSpaceCuts, bool& bOutFromCache);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UPrimitiveComponent>> PieceComponents;

	/** Cut walls by MakeCutKey, kept while the grid lives, so rebuilding a level only cuts what changed. */
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UDynamicMesh>> CutWallCache;

	/** Border pieces taller than LowWallHeight, parallel arrays: component, full and lowered transform, full world bounds. */
	struct FWallPiece
	{
		TWeakObjectPtr<UPrimitiveComponent> Full;
		/** The piece cut off at LowWallHeight (a cut wall); null if it lies wholly above it. */
		TWeakObjectPtr<UPrimitiveComponent> Low;
		/** Swapped for a preview cut: UpdateWalls leaves its visibility alone. */
		bool bPreviewHidden = false;
		FBox WorldBounds;
		bool bLowered = false;
	};
	TArray<FWallPiece> WallPieces;

	/** Shown walls (Edge, no slot) with what a preview cut needs: piece, mesh, transform, its own cuts, WallPieces index. */
	struct FShownWall
	{
		FCombatLevelPiece Piece;
		TWeakObjectPtr<UStaticMesh> Mesh;
		FTransform Transform;
		TArray<FBox> Cuts;
		int32 WallPiece = INDEX_NONE;
		TWeakObjectPtr<UPrimitiveComponent> Full;
	};
	TArray<FShownWall> ShownWalls;

	/** Preview cuts: the walls (ShownWalls indices) swapped out, their cut stand-ins, and the opening they are for. */
	TArray<int32> PreviewCutWalls;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UPrimitiveComponent>> PreviewCutComponents;
	FString PreviewCutKey;
	/** Shows a shown wall again as the wall mode wants it (full or low). */
	void RestoreShownWall(int32 Index);

	bool bHasLevel = false;
	FCombatGridData LevelGridData;

	FCombatGridData GridData;
	bool bCellsBuilt = false;
};
