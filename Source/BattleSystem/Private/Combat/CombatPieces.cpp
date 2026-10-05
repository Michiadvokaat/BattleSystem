// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatPieces.h"
#include "Engine/StaticMesh.h"

FCombatLevelPiece FCombatPieceDefinition::MakePiece(const FIntPoint& Cell, int32 Rotation) const
{
	FCombatLevelPiece Piece;
	Piece.Id = Id;
	Piece.Layer = Layer;
	Piece.Slot = Slot;
	Piece.Cell = Cell;
	const int32 Steps = CombatPieces::GetRotationSteps(Layer);
	Piece.Rotation = ((Rotation % Steps) + Steps) % Steps;
	Piece.Size = Layer == ECombatPieceLayer::Detail ? FIntPoint(1, 1) : FIntPoint(FMath::Max(Size.X, 1), FMath::Max(Size.Y, 1));
	if (Layer == ECombatPieceLayer::Detail)
	{
		Piece.Detail = 0;
		Piece.DetailGrid = FMath::Max(DetailGrid, 1);
	}
	// Floors never block.
	Piece.bBlocksWalking = Layer != ECombatPieceLayer::Floor && bBlocksWalking;
	Piece.bBlocksSight = Layer != ECombatPieceLayer::Floor && bBlocksSight;
	return Piece;
}

FBox FCombatPieceDefinition::GetCutBox(const FBox& MeshBounds) const
{
	return bCustomCut ? FBox::BuildAABB(MeshBounds.GetCenter() + CutOffset, CutSize * 0.5) : MeshBounds;
}

const FCombatPieceDefinition* UCombatPieceCatalog::Find(const FString& Id) const
{
	return Pieces.FindByPredicate([&Id](const FCombatPieceDefinition& Definition) { return Definition.Id == Id; });
}

namespace CombatPieces
{
	FTransform ComputeMeshTransform(const FCombatLevelPiece& Piece, float CellSize, const FBox& MeshBounds, float MeshYaw, const FVector& Offset, bool bScaleToFit, double BaseHeight)
	{
		// Center of the footprint, of the border line or of the detail position, in grid-local cm.
		FVector2D Center;
		if (Piece.Layer == ECombatPieceLayer::Detail)
		{
			Center = Piece.GetDetailCenter(CellSize);
		}
		else if (Piece.Layer == ECombatPieceLayer::Edge)
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
		if (bScaleToFit && Piece.Layer != ECombatPieceLayer::Detail)
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
		const FRotator Rotation(0.0, MeshYaw + 360.0 / GetRotationSteps(Piece.Layer) * Piece.Rotation, 0.0);
		const FVector BoundsCenter = MeshBounds.GetCenter() * Scale;
		const FVector Location = FVector(Center.X, Center.Y, 0.0)
			- Rotation.RotateVector(FVector(BoundsCenter.X, BoundsCenter.Y, 0.0))
			+ FVector(0.0, 0.0, BaseHeight - MeshBounds.Min.Z)
			+ Rotation.RotateVector(Offset);
		return FTransform(Rotation, Location, Scale);
	}

	int32 GetRotationSteps(ECombatPieceLayer Layer)
	{
		return Layer == ECombatPieceLayer::Detail ? 8 : 4;
	}

	double GetSurfaceHeight(const FBox& PieceBounds, float SurfaceHeight, const FVector2D& Point)
	{
		const bool bAbove = Point.X >= PieceBounds.Min.X && Point.X <= PieceBounds.Max.X && Point.Y >= PieceBounds.Min.Y && Point.Y <= PieceBounds.Max.Y;
		if (!bAbove || SurfaceHeight == 0.f)
		{
			return 0.0;
		}
		return SurfaceHeight > 0.f ? SurfaceHeight : PieceBounds.Max.Z;
	}

	double FindDetailBaseHeight(const FCombatLevel& Level, const UCombatPieceCatalog& Catalog, const FCombatLevelPiece& Detail)
	{
		const int32 Index = Level.FindPieceAt(ECombatPieceLayer::Cell, Detail.Cell);
		if (Index == INDEX_NONE)
		{
			return 0.0;
		}
		const FCombatLevelPiece& Below = Level.Pieces[Index];
		const FCombatPieceDefinition* Definition = Catalog.Find(Below.Id);
		const UStaticMesh* Mesh = Definition ? Definition->Mesh.Get() : nullptr;
		if (!Mesh)
		{
			return 0.0;
		}
		const FBox MeshBounds = Mesh->GetBoundingBox();
		const FTransform Transform = ComputeMeshTransform(Below, Level.CellSize, MeshBounds, Definition->MeshYaw, Definition->Offset, Definition->bScaleToFit);
		return GetSurfaceHeight(MeshBounds.TransformBy(Transform), Definition->SurfaceHeight, Detail.GetDetailCenter(Level.CellSize));
	}

	bool SharesBorder(const FCombatLevelPiece& A, const FCombatLevelPiece& B)
	{
		TArray<TPair<FIntPoint, FIntPoint>> EdgesA;
		TArray<TPair<FIntPoint, FIntPoint>> EdgesB;
		A.GetEdges(EdgesA);
		B.GetEdges(EdgesB);
		return EdgesA.ContainsByPredicate([&EdgesB](const TPair<FIntPoint, FIntPoint>& Edge) { return EdgesB.Contains(Edge); });
	}

	FBox ComputeCutBox(const FCombatLevelPiece& Opening, const FTransform& OpeningTransform, const FBox& CutBox, float CellSize, float Depth)
	{
		FBox Box = CutBox.TransformBy(OpeningTransform);
		// Across the border line: deep enough on both sides for any wall.
		if (Opening.IsHorizontalEdge())
		{
			const double Line = Opening.Cell.Y * CellSize;
			Box.Min.Y = FMath::Min(Box.Min.Y, Line - Depth);
			Box.Max.Y = FMath::Max(Box.Max.Y, Line + Depth);
		}
		else
		{
			const double Line = Opening.Cell.X * CellSize;
			Box.Min.X = FMath::Min(Box.Min.X, Line - Depth);
			Box.Max.X = FMath::Max(Box.Max.X, Line + Depth);
		}
		return Box;
	}

	FBox ComputeLowCutBox(const FBox& PlacedBounds, float LowHeight)
	{
		if (PlacedBounds.Min.Z >= LowHeight)
		{
			return FBox(ForceInit);
		}
		constexpr double Margin = 10.0;
		return FBox(FVector(PlacedBounds.Min.X - Margin, PlacedBounds.Min.Y - Margin, LowHeight),
			FVector(PlacedBounds.Max.X + Margin, PlacedBounds.Max.Y + Margin, PlacedBounds.Max.Z + Margin));
	}

	FBox ToMeshSpace(const FBox& LocalBox, const FTransform& Transform)
	{
		FBox Result(ForceInit);
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector Point((Corner & 1) ? LocalBox.Max.X : LocalBox.Min.X, (Corner & 2) ? LocalBox.Max.Y : LocalBox.Min.Y, (Corner & 4) ? LocalBox.Max.Z : LocalBox.Min.Z);
			Result += Transform.InverseTransformPosition(Point);
		}
		return Result;
	}

	FString MakeCutKey(const FString& MeshPath, TConstArrayView<FBox> Cuts)
	{
		FString Key = MeshPath;
		for (const FBox& Cut : Cuts)
		{
			Key += FString::Printf(TEXT("|%.1f,%.1f,%.1f,%.1f,%.1f,%.1f"), Cut.Min.X, Cut.Min.Y, Cut.Min.Z, Cut.Max.X, Cut.Max.Y, Cut.Max.Z);
		}
		return Key;
	}

	FCombatLevelPiece PlaceAt(const FCombatPieceDefinition& Definition, const FVector2D& Local, int32 Rotation, float CellSize)
	{
		FCombatLevelPiece Piece = Definition.MakePiece(FIntPoint::ZeroValue, Rotation);
		const FVector2D InCells = Local / FMath::Max(CellSize, 1.f);
		if (Piece.Layer == ECombatPieceLayer::Detail)
		{
			// The cell under the point, and the detail position under it inside that cell.
			Piece.Cell = FIntPoint(FMath::FloorToInt32(InCells.X), FMath::FloorToInt32(InCells.Y));
			const int32 Grid = Piece.DetailGrid;
			const int32 X = FMath::Clamp(FMath::FloorToInt32((InCells.X - Piece.Cell.X) * Grid), 0, Grid - 1);
			const int32 Y = FMath::Clamp(FMath::FloorToInt32((InCells.Y - Piece.Cell.Y) * Grid), 0, Grid - 1);
			Piece.Detail = X + Y * Grid;
		}
		else if (Piece.Layer == ECombatPieceLayer::Edge)
		{
			// The nearest border line across, and the start so that the piece's middle is at the point along it.
			const double HalfLength = Piece.Size.X * 0.5;
			Piece.Cell = Piece.IsHorizontalEdge()
				? FIntPoint(FMath::RoundToInt32(InCells.X - HalfLength), FMath::RoundToInt32(InCells.Y))
				: FIntPoint(FMath::RoundToInt32(InCells.X), FMath::RoundToInt32(InCells.Y - HalfLength));
		}
		else
		{
			const FIntPoint RotatedSize = Piece.GetRotatedSize();
			const FIntPoint Under(FMath::FloorToInt32(InCells.X), FMath::FloorToInt32(InCells.Y));
			Piece.Cell = Under - FIntPoint((RotatedSize.X - 1) / 2, (RotatedSize.Y - 1) / 2);
		}
		return Piece;
	}
}
