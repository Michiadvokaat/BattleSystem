// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatPieces.h"

FCombatLevelPiece FCombatPieceDefinition::MakePiece(const FIntPoint& Cell, int32 Rotation) const
{
	FCombatLevelPiece Piece;
	Piece.Id = Id;
	Piece.Layer = Layer;
	Piece.Cell = Cell;
	Piece.Rotation = ((Rotation % 4) + 4) % 4;
	Piece.Size = FIntPoint(FMath::Max(Size.X, 1), FMath::Max(Size.Y, 1));
	// Floors never block.
	Piece.bBlocksWalking = Layer != ECombatPieceLayer::Floor && bBlocksWalking;
	Piece.bBlocksSight = Layer != ECombatPieceLayer::Floor && bBlocksSight;
	return Piece;
}

const FCombatPieceDefinition* UCombatPieceCatalog::Find(const FString& Id) const
{
	return Pieces.FindByPredicate([&Id](const FCombatPieceDefinition& Definition) { return Definition.Id == Id; });
}

namespace CombatPieces
{
	FTransform ComputeMeshTransform(const FCombatLevelPiece& Piece, float CellSize, const FBox& MeshBounds, float MeshYaw, const FVector& Offset, bool bScaleToFit)
	{
		// Center of the footprint, or of the border line, in grid-local cm.
		FVector2D Center;
		if (Piece.Layer == ECombatPieceLayer::Edge)
		{
			const double HalfLength = FMath::Max(Piece.Size.X, 1) * 0.5;
			Center = Piece.IsHorizontalEdge()
				? FVector2D((Piece.Cell.X + HalfLength) * CellSize, Piece.Cell.Y * CellSize)
				: FVector2D(Piece.Cell.X * CellSize, (Piece.Cell.Y + HalfLength) * CellSize);
		}
		else
		{
			const FIntPoint RotatedSize = Piece.GetRotatedSize();
			Center = FVector2D((Piece.Cell.X + FMath::Max(RotatedSize.X, 1) * 0.5) * CellSize, (Piece.Cell.Y + FMath::Max(RotatedSize.Y, 1) * 0.5) * CellSize);
		}

		// Scale in the mesh's own axes. MeshYaw turns its length onto the piece's X: with a quarter turn the mesh's Y
		// is the length. The height never scales.
		FVector Scale = FVector::OneVector;
		if (bScaleToFit)
		{
			const bool bSwapAxes = FMath::Abs(FMath::RoundToInt32(MeshYaw / 90.0)) % 2 == 1;
			const FVector MeshSize = MeshBounds.GetSize();
			const double Length = bSwapAxes ? MeshSize.Y : MeshSize.X;
			const double Depth = bSwapAxes ? MeshSize.X : MeshSize.Y;
			const double LengthScale = Length > UE_KINDA_SMALL_NUMBER ? FMath::Max(Piece.Size.X, 1) * CellSize / Length : 1.0;
			const double DepthScale = Piece.Layer != ECombatPieceLayer::Edge && Depth > UE_KINDA_SMALL_NUMBER
				? FMath::Max(Piece.Size.Y, 1) * CellSize / Depth : 1.0;
			Scale = bSwapAxes ? FVector(DepthScale, LengthScale, 1.0) : FVector(LengthScale, DepthScale, 1.0);
		}

		// Yaw only, so the height of the bounds does not change: the bottom goes to Z = 0.
		const FRotator Rotation(0.0, MeshYaw + 90.0 * Piece.Rotation, 0.0);
		const FVector BoundsCenter = MeshBounds.GetCenter() * Scale;
		const FVector Location = FVector(Center.X, Center.Y, 0.0)
			- Rotation.RotateVector(FVector(BoundsCenter.X, BoundsCenter.Y, 0.0))
			+ FVector(0.0, 0.0, -MeshBounds.Min.Z)
			+ Rotation.RotateVector(Offset);
		return FTransform(Rotation, Location, Scale);
	}
}
