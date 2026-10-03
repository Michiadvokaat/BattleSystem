// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatGrid.h"
#include "Combat/CombatObstacle.h"
#include "Components/StaticMeshComponent.h"
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
		DrawDebugCells();
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

void ACombatGrid::DrawDebugCells() const
{
	const UWorld* World = GetWorld();
	const FVector Origin = GetActorLocation() + FVector(0.0, 0.0, 2.0);
	const FColor LineColor(80, 80, 80);

	for (int32 X = 0; X <= Width; ++X)
	{
		DrawDebugLine(World, Origin + FVector(X * CellSize, 0.0, 0.0), Origin + FVector(X * CellSize, Height * CellSize, 0.0), LineColor, true);
	}
	for (int32 Y = 0; Y <= Height; ++Y)
	{
		DrawDebugLine(World, Origin + FVector(0.0, Y * CellSize, 0.0), Origin + FVector(Width * CellSize, Y * CellSize, 0.0), LineColor, true);
	}

	const FVector CellExtent(CellSize * 0.45, CellSize * 0.45, 2.0);
	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const FIntPoint Cell(X, Y);
			if (!GridData.IsWalkable(Cell))
			{
				DrawDebugBox(World, CellToWorld(Cell) + FVector(0.0, 0.0, 2.0), CellExtent, FColor::Red, true);
			}
		}
	}
}
