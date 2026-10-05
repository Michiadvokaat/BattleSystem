// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/CombatLevel.h"
#include "Engine/DataAsset.h"
#include "CombatPieces.generated.h"

class UCombatPieceCatalog;
class UStaticMesh;

/** One placeable piece of the catalog: its mesh, layer, footprint and blocking. */
USTRUCT(BlueprintType)
struct BATTLESYSTEM_API FCombatPieceDefinition
{
	GENERATED_BODY()

	/** "Category/MeshName"; levels refer to the piece by it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Piece")
	FString Id;

	/** Palette group in the LevelDesigner (the catalog subfolder). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Piece")
	FString Category;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Piece")
	TObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Piece")
	ECombatPieceLayer Layer = ECombatPieceLayer::Cell;

	/**
	 * Pieces only replace pieces of the same layer and slot. Empty for most; "Leaf" for door leaves, so a leaf and a
	 * frame share a border (the script sets it per category).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Piece")
	FString Slot;

	/** Footprint in cells before rotation; Edge: X = the number of borders it covers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Piece", meta = (ClampMin = 1))
	FIntPoint Size = FIntPoint(1, 1);

	/** Cell pieces: their cells block walking / sight. Edge pieces block both when either is set (off for door frames). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Piece")
	bool bBlocksWalking = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Piece")
	bool bBlocksSight = false;

	/** Turns the mesh so that its length lies along X (unrotated piece); the script sets 90 for meshes long along Y. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fit", meta = (Units = "deg"))
	float MeshYaw = 0.f;

	/**
	 * Stretches or squeezes the mesh to exactly its footprint: along its length (Edge), or in X and Y (Floor, Cell).
	 * The height stays. For shorter variants of a long mesh: another entry with the same mesh, its own Id and a smaller Size.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fit")
	bool bScaleToFit = false;

	/** Moves the mesh after the automatic fit, in the mesh's own axes (for example Z for a window's sill height). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fit", meta = (Units = "cm"))
	FVector Offset = FVector::ZeroVector;

	/** Cell pieces: height (cm) that details stand on, for example a seat; -1 = the top of the mesh, 0 = nothing on it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fit", meta = (ClampMin = -1, Units = "cm"))
	float SurfaceHeight = -1.f;

	/** Detail layer: positions per cell side, so DetailGrid x DetailGrid positions per cell. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Piece", meta = (ClampMin = 1, ClampMax = 8))
	int32 DetailGrid = 3;

	/** The level piece this definition places at Cell with Rotation (footprint and blocking copied). */
	FCombatLevelPiece MakePiece(const FIntPoint& Cell, int32 Rotation) const;
};

/**
 * The pieces the LevelDesigner can place, filled by Scripts/CreatePieceCatalog.py from the catalog folder
 * (subfolder = category). Presentation and editing only: placed pieces carry their own footprint and blocking.
 */
UCLASS(BlueprintType)
class BATTLESYSTEM_API UCombatPieceCatalog : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog", meta = (TitleProperty = "Id"))
	TArray<FCombatPieceDefinition> Pieces;

	const FCombatPieceDefinition* Find(const FString& Id) const;
};

namespace CombatPieces
{
	/**
	 * Where a piece's mesh goes, relative to the grid origin: centered on its footprint (Floor, Cell) or on its border
	 * line (Edge), with the bottom of MeshBounds on the floor, turned by MeshYaw + 90 x Rotation, then moved by Offset
	 * (in the mesh's axes). With bScaleToFit the mesh is scaled to the footprint's length (Edge) or X and Y (Floor,
	 * Cell) first. Detail pieces are centered on their detail position, turn in 45 degree steps, never scale, and
	 * stand at BaseHeight. MeshBounds is the mesh's local bounding box.
	 */
	BATTLESYSTEM_API FTransform ComputeMeshTransform(const FCombatLevelPiece& Piece, float CellSize, const FBox& MeshBounds,
		float MeshYaw, const FVector& Offset, bool bScaleToFit = false, double BaseHeight = 0.0);

	/** Rotation steps per full turn of a layer: 8 for details (45 degrees), 4 for the others. */
	BATTLESYSTEM_API int32 GetRotationSteps(ECombatPieceLayer Layer);

	/**
	 * Height a detail at Point stands on, given the bounds of the Cell piece in its cell (grid-local): SurfaceHeight
	 * (-1 = the bounds' top, 0 = nothing on it) when Point lies inside the bounds seen from above, else the floor (0).
	 */
	BATTLESYSTEM_API double GetSurfaceHeight(const FBox& PieceBounds, float SurfaceHeight, const FVector2D& Point);

	/** Height a detail piece stands on in a level: on the Cell piece in its cell (catalog meshes), else 0. */
	BATTLESYSTEM_API double FindDetailBaseHeight(const FCombatLevel& Level, const UCombatPieceCatalog& Catalog, const FCombatLevelPiece& Detail);

	/**
	 * The piece a definition makes under a grid-local point (LevelDesigner): Floor and Cell pieces are centered on the
	 * cell under the point (rounded down for even sizes), Edge pieces lie on the nearest border of their direction and
	 * are centered along it.
	 */
	BATTLESYSTEM_API FCombatLevelPiece PlaceAt(const FCombatPieceDefinition& Definition, const FVector2D& Local, int32 Rotation, float CellSize);
}
