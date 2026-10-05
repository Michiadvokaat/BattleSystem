// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/CombatLevel.h"
#include "Engine/DataAsset.h"
#include "CombatPieces.generated.h"

class UCombatPieceCatalog;
class UMeshComponent;
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
	 * Pieces only replace pieces of the same layer and slot. Empty for most; "Opening" for windows and door frames (they
	 * share a border with a wall and cut it), "Leaf" for door leaves (in a frame). The script sets it per category.
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

	/** Openings: cut CutSize at the mesh bounds' center + CutOffset (mesh axes) instead of the whole mesh bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cut")
	bool bCustomCut = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cut", meta = (EditCondition = "bCustomCut", Units = "cm"))
	FVector CutSize = FVector(100.0, 30.0, 200.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cut", meta = (EditCondition = "bCustomCut", Units = "cm"))
	FVector CutOffset = FVector::ZeroVector;

	/** The box an opening cuts, in the mesh's own space. */
	FBox GetCutBox(const FBox& MeshBounds) const;

	/** Moves the mesh after the automatic fit, in the mesh's own axes (for example Z for a window's sill height). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fit", meta = (Units = "cm"))
	FVector Offset = FVector::ZeroVector;

	/** Cell pieces: height (cm) that details stand on, for example a seat; -1 = the top of the mesh, 0 = nothing on it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fit", meta = (ClampMin = -1, Units = "cm"))
	float SurfaceHeight = -1.f;

	/**
	 * The placed piece's Color tints it: every material of its mesh becomes a dynamic instance with vector parameter
	 * "Color" (the engine's BasicShapeMaterial has it). For solid floors; the LevelDesigner shows a color row for it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Piece")
	bool bTintable = false;

	/** Detail layer: positions per cell side, so DetailGrid x DetailGrid positions per cell. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Piece", meta = (ClampMin = 1, ClampMax = 8))
	int32 DetailGrid = 3;

	/** The level piece this definition places at Cell with Rotation (footprint and blocking copied; Color white). */
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

	/** Gives every material slot of Mesh a dynamic instance of the settings' TintMaterial with "Color" = the piece color. */
	BATTLESYSTEM_API void ApplyTint(UMeshComponent& Mesh, const FColor& Color);

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

	/** Whether two Edge pieces cover at least one common border. */
	BATTLESYSTEM_API bool SharesBorder(const FCombatLevelPiece& A, const FCombatLevelPiece& B);

	/**
	 * The box an opening cuts out of walls, in grid-local space: its cut box placed with its transform, made Depth deep
	 * on both sides of its border line so it goes through any wall standing there.
	 */
	BATTLESYSTEM_API FBox ComputeCutBox(const FCombatLevelPiece& Opening, const FTransform& OpeningTransform, const FBox& CutBox, float CellSize, float Depth);

	/**
	 * The box that cuts a placed piece down to LowHeight (walls lowered): everything above LowHeight within its placed
	 * bounds (grid-local), with a margin around. Invalid (bIsValid false) if the piece lies wholly above LowHeight.
	 */
	BATTLESYSTEM_API FBox ComputeLowCutBox(const FBox& PlacedBounds, float LowHeight);

	/** A grid-local box seen from a mesh placed with Transform: the bounds of its corners in the mesh's own space. */
	BATTLESYSTEM_API FBox ToMeshSpace(const FBox& LocalBox, const FTransform& Transform);

	/** Cache key of a mesh with boxes cut out (mesh space, rounded to millimeters): equal keys give equal meshes. */
	BATTLESYSTEM_API FString MakeCutKey(const FString& MeshPath, TConstArrayView<FBox> Cuts);
}
