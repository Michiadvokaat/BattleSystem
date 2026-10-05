// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatGrid.h"
#include "Combat/CombatObstacle.h"
#include "Combat/CombatPieces.h"
#include "Combat/CombatSettings.h"
#include "Combat/CombatSubsystem.h"
#include "Engine/StaticMesh.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GeometryScript/MeshAssetFunctions.h"
#include "GeometryScript/MeshBooleanFunctions.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "HAL/PlatformTime.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "UDynamicMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"

ACombatGrid::ACombatGrid()
{
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	FloorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FloorMesh"));
	FloorMesh->SetupAttachment(RootComponent);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneMesh.Succeeded())
	{
		FloorMesh->SetStaticMesh(PlaneMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	BlockMaterialBase = ShapeMaterial.Succeeded() ? ShapeMaterial.Object : nullptr;

	auto MakeBlocks = [this](const TCHAR* Name)
	{
		UInstancedStaticMeshComponent* Blocks = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
		Blocks->SetupAttachment(RootComponent);
		Blocks->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (CubeMesh.Succeeded())
		{
			Blocks->SetStaticMesh(CubeMesh.Object);
		}
		return Blocks;
	};
	WallBlocks = MakeBlocks(TEXT("WallBlocks"));
	HedgeBlocks = MakeBlocks(TEXT("HedgeBlocks"));
	WaterBlocks = MakeBlocks(TEXT("WaterBlocks"));

	PreviewMarks = MakeBlocks(TEXT("PreviewMarks"));
	PreviewMarks->SetCastShadow(false);
	PreviewMarks->SetVisibility(false);
	PreviewMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PreviewMesh"));
	PreviewMesh->SetupAttachment(RootComponent);
	PreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewMesh->SetVisibility(false);
}

void ACombatGrid::ShowPiecePreview(const FCombatLevelPiece& Piece, float InCellSize, UStaticMesh* Mesh, const FTransform& MeshTransform, bool bFits, bool bErase)
{
	PreviewMesh->SetStaticMesh(Mesh);
	PreviewMesh->SetRelativeTransform(MeshTransform);
	PreviewMesh->SetVisibility(!bErase && Mesh != nullptr);

	if (!PreviewMaterial && BlockMaterialBase)
	{
		PreviewMaterial = PreviewMarks->CreateAndSetMaterialInstanceDynamicFromMaterial(0, BlockMaterialBase);
	}
	if (PreviewMaterial)
	{
		PreviewMaterial->SetVectorParameterValue(TEXT("Color"), bErase ? PreviewEraseColor : (bFits ? PreviewPlaceColor : PreviewBlockedColor));
	}

	// Flat plates just above the floor pieces; bars along borders. The engine cube is 100 cm.
	PreviewMarks->ClearInstances();
	const double Size = InCellSize;
	TArray<FIntPoint> Cells;
	Piece.GetCells(Cells);
	if (Piece.Layer == ECombatPieceLayer::Detail)
	{
		// One small plate on its detail position instead of the whole cell.
		const FVector2D Center = Piece.GetDetailCenter(InCellSize);
		const double PlateSize = Size / FMath::Max(Piece.DetailGrid, 1) * 0.8;
		PreviewMarks->AddInstance(FTransform(FRotator::ZeroRotator, FVector(Center.X, Center.Y, 3.0), FVector(PlateSize / 100.0, PlateSize / 100.0, 0.04)));
		Cells.Reset();
	}
	for (const FIntPoint& Cell : Cells)
	{
		PreviewMarks->AddInstance(FTransform(FRotator::ZeroRotator, FVector((Cell.X + 0.5) * Size, (Cell.Y + 0.5) * Size, 3.0),
			FVector(Size * 0.9 / 100.0, Size * 0.9 / 100.0, 0.04)));
	}
	TArray<TPair<FIntPoint, FIntPoint>> Edges;
	Piece.GetEdges(Edges);
	for (const TPair<FIntPoint, FIntPoint>& Edge : Edges)
	{
		const FIntPoint& After = Edge.Value;
		const bool bHorizontal = Edge.Key.Y != After.Y;
		const FVector Center = bHorizontal ? FVector((After.X + 0.5) * Size, After.Y * Size, 4.0) : FVector(After.X * Size, (After.Y + 0.5) * Size, 4.0);
		const FVector Scale = bHorizontal ? FVector(Size * 0.9 / 100.0, 0.1, 0.06) : FVector(0.1, Size * 0.9 / 100.0, 0.06);
		PreviewMarks->AddInstance(FTransform(FRotator::ZeroRotator, Center, Scale));
	}
	PreviewMarks->SetVisibility(true);
}

void ACombatGrid::HidePiecePreview()
{
	if (PreviewMesh->IsVisible() || PreviewMarks->IsVisible())
	{
		PreviewMesh->SetVisibility(false);
		PreviewMarks->SetVisibility(false);
	}
	UpdatePreviewCuts(nullptr, FTransform::Identity, FBox(ForceInit), 100.f);
}

void ACombatGrid::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// The engine plane is 100x100 cm and centered on its origin.
	const FVector2D Size(Width * CellSize, Height * CellSize);
	FloorMesh->SetRelativeLocation(FVector(Size.X * 0.5, Size.Y * 0.5, 0.0));
	FloorMesh->SetRelativeScale3D(FVector(Size.X / 100.0, Size.Y / 100.0, 1.0));

	bCellsBuilt = false;
}

void ACombatGrid::BeginPlay()
{
	Super::BeginPlay();

	BuildCells();
	if (bDrawDebugCells)
	{
		DrawDebugCells(GridData);
	}
}

const FCombatGridData& ACombatGrid::GetShownGridData()
{
	return bHasLevel ? LevelGridData : GetGridData();
}

void ACombatGrid::ApplyLevel(const FCombatLevel& Level)
{
	bHasLevel = true;
	Level.ToGridData(LevelGridData);
	SetObstaclesHidden(true);
	ShowGrid(LevelGridData);
	// Floor pieces lie at Z = 0: the grid's floor goes just below them (cells without a floor piece still show it).
	if (Level.Pieces.ContainsByPredicate([](const FCombatLevelPiece& Piece) { return Piece.Layer == ECombatPieceLayer::Floor; }))
	{
		FloorMesh->SetRelativeLocation(FloorMesh->GetRelativeLocation() - FVector(0.0, 0.0, PieceFloorDrop));
	}

	// One block per wall, hedge and water cell (WallHeight, HedgeHeight, WaterHeight); the engine cube is 100 cm.
	struct FBlockKind
	{
		UInstancedStaticMeshComponent* Blocks;
		TCHAR Kind;
		float BlockHeight;
		FLinearColor Color;
	};
	const FBlockKind Kinds[] =
	{
		{ WallBlocks, FCombatLevel::Wall, WallHeight, WallColor },
		{ HedgeBlocks, FCombatLevel::Hedge, HedgeHeight, HedgeColor },
		{ WaterBlocks, FCombatLevel::Water, WaterHeight, WaterColor },
	};
	const float Size = Level.CellSize;
	for (const FBlockKind& Kind : Kinds)
	{
		Kind.Blocks->ClearInstances();
		if (BlockMaterialBase)
		{
			UMaterialInstanceDynamic* Material = Kind.Blocks->CreateAndSetMaterialInstanceDynamicFromMaterial(0, BlockMaterialBase);
			Material->SetVectorParameterValue(TEXT("Color"), Kind.Color);
		}
		for (int32 Y = 0; Y < Level.Height; ++Y)
		{
			for (int32 X = 0; X < Level.Width; ++X)
			{
				if (Level.GetCell(FIntPoint(X, Y)) == Kind.Kind)
				{
					const FVector Location((X + 0.5) * Size, (Y + 0.5) * Size, Kind.BlockHeight * 0.5);
					const FVector Scale(Size * 0.98 / 100.0, Size * 0.98 / 100.0, Kind.BlockHeight / 100.0);
					Kind.Blocks->AddInstance(FTransform(FRotator::ZeroRotator, Location, Scale));
				}
			}
		}
	}
	ShowPieces(Level);
}

void ACombatGrid::ShowPieces(const FCombatLevel& Level)
{
	ClearPieces();
	if (Level.Pieces.IsEmpty())
	{
		return;
	}
	const UCombatPieceCatalog* Catalog = GetDefault<UCombatSettings>()->PieceCatalog.LoadSynchronous();
	if (!Catalog)
	{
		UE_LOG(LogCombat, Warning, TEXT("Level %s has %d pieces but no PieceCatalog is set (Project Settings > Combat)."), *Level.Name, Level.Pieces.Num());
		return;
	}

	// First where every piece goes, then the cut boxes of the openings, then the components.
	struct FPlaced
	{
		const FCombatLevelPiece* Piece = nullptr;
		const FCombatPieceDefinition* Definition = nullptr;
		UStaticMesh* Mesh = nullptr;
		FBox MeshBounds;
		FTransform Transform;
	};
	TArray<FPlaced> Placed;
	for (const FCombatLevelPiece& Piece : Level.Pieces)
	{
		const FCombatPieceDefinition* Definition = Catalog->Find(Piece.Id);
		UStaticMesh* Mesh = Definition ? Definition->Mesh.Get() : nullptr;
		if (!Mesh)
		{
			UE_LOG(LogCombat, Warning, TEXT("Level %s: piece %s is not in the catalog or has no mesh."), *Level.Name, *Piece.Id);
			continue;
		}
		// Details stand on the Cell piece under them.
		const double BaseHeight = Piece.Layer == ECombatPieceLayer::Detail ? CombatPieces::FindDetailBaseHeight(Level, *Catalog, Piece) : 0.0;
		FPlaced& Entry = Placed.AddDefaulted_GetRef();
		Entry.Piece = &Piece;
		Entry.Definition = Definition;
		Entry.Mesh = Mesh;
		Entry.MeshBounds = Mesh->GetBoundingBox();
		Entry.Transform = CombatPieces::ComputeMeshTransform(Piece, Level.CellSize, Entry.MeshBounds,
			Definition->MeshYaw, Definition->Offset, Definition->bScaleToFit, BaseHeight);
	}

	// One cell deep on both sides of the border: through any wall.
	const float CutDepth = Level.CellSize;
	const double CutStart = FPlatformTime::Seconds();
	int32 CutWalls = 0;
	int32 CutFromCache = 0;
	int32 LowVersions = 0;
	int32 LowFromCache = 0;
	for (const FPlaced& Entry : Placed)
	{
		const FCombatLevelPiece& Piece = *Entry.Piece;
		const FBox MeshBounds = Entry.MeshBounds;
		const FTransform& Transform = Entry.Transform;

		// A wall (Edge, no slot) with openings on its borders gets their boxes cut out.
		TArray<FBox> Cuts;
		if (Piece.Layer == ECombatPieceLayer::Edge && Piece.Slot.IsEmpty())
		{
			for (const FPlaced& Other : Placed)
			{
				if (Other.Piece->Layer == ECombatPieceLayer::Edge && Other.Piece->Slot == FCombatLevelPiece::OpeningSlot && CombatPieces::SharesBorder(Piece, *Other.Piece))
				{
					const FBox CutBox = CombatPieces::ComputeCutBox(*Other.Piece, Other.Transform, Other.Definition->GetCutBox(Other.MeshBounds), Level.CellSize, CutDepth);
					Cuts.Add(CombatPieces::ToMeshSpace(CutBox, Transform));
				}
			}
		}

		UPrimitiveComponent* Component = nullptr;
		if (!Cuts.IsEmpty())
		{
			bool bFromCache = false;
			Component = MakeCutWall(Entry.Mesh, Cuts, bFromCache);
			++CutWalls;
			CutFromCache += bFromCache ? 1 : 0;
		}
		else
		{
			UStaticMeshComponent* MeshComponent = NewObject<UStaticMeshComponent>(this);
			MeshComponent->SetStaticMesh(Entry.Mesh);
			Component = MeshComponent;
		}
		Component->SetupAttachment(RootComponent);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetRelativeTransform(Transform);
		Component->RegisterComponent();
		PieceComponents.Add(Component);
		if (Piece.Layer == ECombatPieceLayer::Edge && Piece.Slot.IsEmpty())
		{
			FShownWall& Shown = ShownWalls.AddDefaulted_GetRef();
			Shown.Piece = Piece;
			Shown.Mesh = Entry.Mesh;
			Shown.Transform = Transform;
			Shown.Cuts = Cuts;
			Shown.Full = Component;
		}

		// Border pieces reaching above the low height can be lowered: a version cut off there (with the same openings),
		// hidden until the wall mode lowers the piece. Pieces wholly above it have no low version.
		const float LowHeight = GetDefault<UCombatSettings>()->LowWallHeight;
		const FBox PlacedBounds = MeshBounds.TransformBy(Transform);
		if (Piece.Layer == ECombatPieceLayer::Edge && PlacedBounds.Max.Z > LowHeight)
		{
			if (Piece.Layer == ECombatPieceLayer::Edge && Piece.Slot.IsEmpty() && !ShownWalls.IsEmpty() && ShownWalls.Last().Full == Component)
			{
				ShownWalls.Last().WallPiece = WallPieces.Num();
			}
			FWallPiece& Wall = WallPieces.AddDefaulted_GetRef();
			Wall.Full = Component;
			Wall.WorldBounds = MeshBounds.TransformBy(Transform * GetActorTransform());
			const FBox LowCut = CombatPieces::ComputeLowCutBox(PlacedBounds, LowHeight);
			if (LowCut.IsValid)
			{
				TArray<FBox> LowCuts = Cuts;
				LowCuts.Add(CombatPieces::ToMeshSpace(LowCut, Transform));
				bool bFromCache = false;
				UPrimitiveComponent* Low = MakeCutWall(Entry.Mesh, LowCuts, bFromCache);
				++LowVersions;
				LowFromCache += bFromCache ? 1 : 0;
				Low->SetupAttachment(RootComponent);
				Low->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Low->SetRelativeTransform(Transform);
				Low->SetVisibility(false);
				Low->RegisterComponent();
				PieceComponents.Add(Low);
				Wall.Low = Low;
			}
		}
	}
	if (CutWalls > 0 || LowVersions > 0)
	{
		UE_LOG(LogCombat, Display, TEXT("Level %s: %d walls with openings (%d from the cache), %d low versions (%d from the cache) in %.2f ms."),
			*Level.Name, CutWalls, CutFromCache, LowVersions, LowFromCache, (FPlatformTime::Seconds() - CutStart) * 1000.0);
	}
}

UPrimitiveComponent* ACombatGrid::MakeCutWall(UStaticMesh* Mesh, TConstArrayView<FBox> MeshSpaceCuts, bool& bOutFromCache)
{
	const FString Key = CombatPieces::MakeCutKey(Mesh->GetPathName(), MeshSpaceCuts);
	TObjectPtr<UDynamicMesh>& Cached = CutWallCache.FindOrAdd(Key);
	bOutFromCache = Cached != nullptr;
	if (!Cached)
	{
		Cached = NewObject<UDynamicMesh>(this);
		EGeometryScriptOutcomePins Outcome;
		UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshFromStaticMeshV2(Mesh, Cached, FGeometryScriptCopyMeshFromAssetOptions(),
			FGeometryScriptMeshReadLOD(), Outcome, true);
		if (Outcome != EGeometryScriptOutcomePins::Success)
		{
			UE_LOG(LogCombat, Warning, TEXT("Could not read %s to cut openings (packaged builds need Allow CPU Access on it)."), *Mesh->GetPathName());
		}
		// Each cut is a box subtracted from the wall; its faces take the wall's first material. All their UVs go to one
		// point: the UV of the wall triangle nearest the cut, so packs that color by a texture atlas get the wall's color
		// there instead of a stretch over the whole atlas.
		UDynamicMesh* Cutter = NewObject<UDynamicMesh>(this);
		for (const FBox& Cut : MeshSpaceCuts)
		{
			Cutter->Reset();
			const FVector Size = Cut.GetSize();
			UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Cutter, FGeometryScriptPrimitiveOptions(), FTransform(Cut.GetCenter()),
				Size.X, Size.Y, Size.Z, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Center);
			FVector2f WallUV = FVector2f::ZeroVector;
			Cached->ProcessMesh([&Cut, &WallUV](const UE::Geometry::FDynamicMesh3& Wall)
			{
				const UE::Geometry::FDynamicMeshUVOverlay* UVs = Wall.HasAttributes() ? Wall.Attributes()->PrimaryUV() : nullptr;
				double Nearest = TNumericLimits<double>::Max();
				for (const int32 Triangle : Wall.TriangleIndicesItr())
				{
					const double Distance = FVector::DistSquared(Wall.GetTriCentroid(Triangle), Cut.GetCenter());
					if (UVs && UVs->IsSetTriangle(Triangle) && Distance < Nearest)
					{
						Nearest = Distance;
						WallUV = UVs->GetElement(UVs->GetTriangle(Triangle).A);
					}
				}
			});
			Cutter->EditMesh([&WallUV](UE::Geometry::FDynamicMesh3& Box)
			{
				if (UE::Geometry::FDynamicMeshUVOverlay* UVs = Box.HasAttributes() ? Box.Attributes()->PrimaryUV() : nullptr)
				{
					for (const int32 Element : UVs->ElementIndicesItr())
					{
						UVs->SetElement(Element, WallUV);
					}
				}
			});
			UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(Cached, FTransform::Identity, Cutter, FTransform::Identity,
				EGeometryScriptBooleanOperation::Subtract, FGeometryScriptMeshBooleanOptions());
		}
	}

	UDynamicMeshComponent* Component = NewObject<UDynamicMeshComponent>(this);
	Component->SetMesh(UE::Geometry::FDynamicMesh3(Cached->GetMeshRef()));
	TArray<UMaterialInterface*> Materials;
	TArray<int32> MaterialIndices;
	TArray<FName> SlotNames;
	EGeometryScriptOutcomePins Outcome;
	UGeometryScriptLibrary_StaticMeshFunctions::GetSectionMaterialListFromStaticMesh(Mesh, FGeometryScriptMeshReadLOD(), Materials, MaterialIndices, SlotNames, Outcome);
	Component->ConfigureMaterialSet(Materials);
	return Component;
}

void ACombatGrid::ClearPieces()
{
	for (UPrimitiveComponent* Component : PieceComponents)
	{
		if (Component)
		{
			Component->DestroyComponent();
		}
	}
	PieceComponents.Reset();
	WallPieces.Reset();
	for (UPrimitiveComponent* Component : PreviewCutComponents)
	{
		if (Component)
		{
			Component->DestroyComponent();
		}
	}
	PreviewCutComponents.Reset();
	PreviewCutWalls.Reset();
	PreviewCutKey.Reset();
	ShownWalls.Reset();
}

void ACombatGrid::RestoreShownWall(int32 Index)
{
	const FShownWall& Shown = ShownWalls[Index];
	if (WallPieces.IsValidIndex(Shown.WallPiece))
	{
		FWallPiece& Wall = WallPieces[Shown.WallPiece];
		Wall.bPreviewHidden = false;
		if (UPrimitiveComponent* Full = Wall.Full.Get())
		{
			Full->SetVisibility(!Wall.bLowered);
		}
		if (UPrimitiveComponent* Low = Wall.Low.Get())
		{
			Low->SetVisibility(Wall.bLowered);
		}
	}
	else if (UPrimitiveComponent* Full = Shown.Full.Get())
	{
		Full->SetVisibility(true);
	}
}

void ACombatGrid::UpdatePreviewCuts(const FCombatLevelPiece* Opening, const FTransform& OpeningTransform, const FBox& CutBox, float InCellSize)
{
	const FString Key = Opening ? FString::Printf(TEXT("%s|%d,%d|%d"), *Opening->Id, Opening->Cell.X, Opening->Cell.Y, Opening->Rotation) : FString();
	if (Key == PreviewCutKey)
	{
		return;
	}
	PreviewCutKey = Key;
	for (UPrimitiveComponent* Component : PreviewCutComponents)
	{
		if (Component)
		{
			Component->DestroyComponent();
		}
	}
	PreviewCutComponents.Reset();
	for (const int32 Index : PreviewCutWalls)
	{
		RestoreShownWall(Index);
	}
	PreviewCutWalls.Reset();
	if (!Opening)
	{
		return;
	}

	const FBox Cut = CombatPieces::ComputeCutBox(*Opening, OpeningTransform, CutBox, InCellSize, InCellSize);
	for (int32 Index = 0; Index < ShownWalls.Num(); ++Index)
	{
		const FShownWall& Shown = ShownWalls[Index];
		UStaticMesh* WallMesh = Shown.Mesh.Get();
		if (!WallMesh || !CombatPieces::SharesBorder(Shown.Piece, *Opening))
		{
			continue;
		}
		TArray<FBox> Cuts = Shown.Cuts;
		Cuts.Add(CombatPieces::ToMeshSpace(Cut, Shown.Transform));
		bool bFromCache = false;
		UPrimitiveComponent* Component = MakeCutWall(WallMesh, Cuts, bFromCache);
		Component->SetupAttachment(RootComponent);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetRelativeTransform(Shown.Transform);
		Component->RegisterComponent();
		PreviewCutComponents.Add(Component);

		// The wall itself (full and low) is hidden while the preview stands in for it.
		if (UPrimitiveComponent* Full = Shown.Full.Get())
		{
			Full->SetVisibility(false);
		}
		if (WallPieces.IsValidIndex(Shown.WallPiece))
		{
			WallPieces[Shown.WallPiece].bPreviewHidden = true;
			if (UPrimitiveComponent* Low = WallPieces[Shown.WallPiece].Low.Get())
			{
				Low->SetVisibility(false);
			}
		}
		PreviewCutWalls.Add(Index);
	}
}

void ACombatGrid::UpdateWalls(ECombatWallMode Mode, const FVector& CameraLocation, TConstArrayView<FVector> Targets)
{
	for (FWallPiece& Wall : WallPieces)
	{
		bool bLower = Mode == ECombatWallMode::Down;
		if (Mode == ECombatWallMode::Cutaway)
		{
			for (const FVector& Target : Targets)
			{
				if (FMath::LineBoxIntersection(Wall.WorldBounds, CameraLocation, Target, Target - CameraLocation))
				{
					bLower = true;
					break;
				}
			}
		}
		if (bLower != Wall.bLowered && !Wall.bPreviewHidden)
		{
			Wall.bLowered = bLower;
			if (UPrimitiveComponent* Full = Wall.Full.Get())
			{
				Full->SetVisibility(!bLower);
			}
			if (UPrimitiveComponent* Low = Wall.Low.Get())
			{
				Low->SetVisibility(bLower);
			}
		}
	}
}

void ACombatGrid::ClearLevel()
{
	if (!bHasLevel)
	{
		return;
	}
	bHasLevel = false;
	WallBlocks->ClearInstances();
	HedgeBlocks->ClearInstances();
	WaterBlocks->ClearInstances();
	ClearPieces();
	SetObstaclesHidden(false);
	ShowGrid(GetGridData());
}

void ACombatGrid::ShowGrid(const FCombatGridData& Data)
{
	// The engine plane is 100x100 cm and centered on its origin.
	const FVector2D Size = Data.GetLocalSize();
	FloorMesh->SetRelativeLocation(FVector(Size.X * 0.5, Size.Y * 0.5, 0.0));
	FloorMesh->SetRelativeScale3D(FVector(Size.X / 100.0, Size.Y / 100.0, 1.0));

	if (bDrawDebugCells)
	{
		FlushPersistentDebugLines(GetWorld());
		DrawDebugCells(Data);
	}
}

void ACombatGrid::SetObstaclesHidden(bool bHideObstacles)
{
	for (TActorIterator<ACombatObstacle> It(GetWorld()); It; ++It)
	{
		It->SetActorHiddenInGame(bHideObstacles);
	}
}

const FCombatGridData& ACombatGrid::GetGridData()
{
	if (!bCellsBuilt)
	{
		BuildCells();
	}
	return GridData;
}

FIntPoint ACombatGrid::WorldToCell(const FVector& World) const
{
	const FVector2D Local = WorldToLocal(World);
	return FIntPoint(FMath::FloorToInt32(Local.X / CellSize), FMath::FloorToInt32(Local.Y / CellSize));
}

FVector ACombatGrid::CellToWorld(const FIntPoint& Cell) const
{
	return LocalToWorld(FVector2D((Cell.X + 0.5) * CellSize, (Cell.Y + 0.5) * CellSize));
}

FVector2D ACombatGrid::WorldToLocal(const FVector& World) const
{
	const FVector Delta = World - GetActorLocation();
	return FVector2D(Delta.X, Delta.Y);
}

ACombatGrid* ACombatGrid::Find(const UWorld* World)
{
	if (World)
	{
		for (TActorIterator<ACombatGrid> It(World); It; ++It)
		{
			return *It;
		}
	}
	return nullptr;
}

void ACombatGrid::BuildCells()
{
	GridData.Init(Width, Height, CellSize);

	// Flags are OR-ed, so the iteration order of obstacles does not matter.
	TArray<FIntPoint> Footprint;
	for (TActorIterator<ACombatObstacle> It(GetWorld()); It; ++It)
	{
		const ACombatObstacle* Obstacle = *It;
		ECombatCellFlags Flags = ECombatCellFlags::None;
		if (Obstacle->bBlocksWalkability)
		{
			Flags |= ECombatCellFlags::Blocked;
		}
		if (Obstacle->bBlocksSight)
		{
			Flags |= ECombatCellFlags::BlocksSight;
		}

		Footprint.Reset();
		Obstacle->GetFootprint(*this, Footprint);
		for (const FIntPoint& Cell : Footprint)
		{
			GridData.AddFlags(Cell, Flags);
		}
	}

	bCellsBuilt = true;
}

void ACombatGrid::DrawDebugCells(const FCombatGridData& Data) const
{
	const UWorld* World = GetWorld();
	const FVector Origin = GetActorLocation() + FVector(0.0, 0.0, 2.0);
	const FColor LineColor(80, 80, 80);
	const double Size = Data.CellSize;

	for (int32 X = 0; X <= Data.Width; ++X)
	{
		DrawDebugLine(World, Origin + FVector(X * Size, 0.0, 0.0), Origin + FVector(X * Size, Data.Height * Size, 0.0), LineColor, true);
	}
	for (int32 Y = 0; Y <= Data.Height; ++Y)
	{
		DrawDebugLine(World, Origin + FVector(0.0, Y * Size, 0.0), Origin + FVector(Data.Width * Size, Y * Size, 0.0), LineColor, true);
	}

	// Blocked cells of the arena's own obstacles; a level shows its blocks instead.
	if (bHasLevel)
	{
		return;
	}
	const FVector CellExtent(Size * 0.45, Size * 0.45, 2.0);
	for (int32 Y = 0; Y < Data.Height; ++Y)
	{
		for (int32 X = 0; X < Data.Width; ++X)
		{
			const FIntPoint Cell(X, Y);
			if (!Data.IsWalkable(Cell))
			{
				DrawDebugBox(World, Origin + FVector((X + 0.5) * Size, (Y + 0.5) * Size, 0.0), CellExtent, FColor::Red, true);
			}
		}
	}
}
