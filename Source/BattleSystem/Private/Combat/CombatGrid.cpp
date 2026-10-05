// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatGrid.h"
#include "Combat/CombatObstacle.h"
#include "Combat/CombatPieces.h"
#include "Combat/CombatSettings.h"
#include "Combat/CombatSubsystem.h"
#include "Engine/StaticMesh.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
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

	for (const FCombatLevelPiece& Piece : Level.Pieces)
	{
		const FCombatPieceDefinition* Definition = Catalog->Find(Piece.Id);
		UStaticMesh* Mesh = Definition ? Definition->Mesh.Get() : nullptr;
		if (!Mesh)
		{
			UE_LOG(LogCombat, Warning, TEXT("Level %s: piece %s is not in the catalog or has no mesh."), *Level.Name, *Piece.Id);
			continue;
		}
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this);
		Component->SetupAttachment(RootComponent);
		Component->SetStaticMesh(Mesh);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetRelativeTransform(CombatPieces::ComputeMeshTransform(Piece, Level.CellSize, Mesh->GetBoundingBox(),
			Definition->MeshYaw, Definition->Offset, Definition->bScaleToFit));
		Component->RegisterComponent();
		PieceComponents.Add(Component);
	}
}

void ACombatGrid::ClearPieces()
{
	for (UStaticMeshComponent* Component : PieceComponents)
	{
		if (Component)
		{
			Component->DestroyComponent();
		}
	}
	PieceComponents.Reset();
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
