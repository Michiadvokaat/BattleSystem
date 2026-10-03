// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatGrid.h"
#include "Combat/CombatObstacle.h"
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
