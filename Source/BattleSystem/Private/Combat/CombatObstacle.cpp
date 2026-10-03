// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatObstacle.h"
#include "Combat/CombatGrid.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

ACombatObstacle::ACombatObstacle()
{
	PrimaryActorTick.bCanEverTick = false;

	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	Box->SetBoxExtent(FVector(50.0, 50.0, 100.0));
	Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RootComponent = Box;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Box);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		Mesh->SetStaticMesh(CubeMesh.Object);
	}
}

void ACombatObstacle::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// The engine cube is 100 cm and centered on its origin.
	Mesh->SetRelativeScale3D(Box->GetUnscaledBoxExtent() / 50.0);
}

void ACombatObstacle::GetFootprint(const ACombatGrid& Grid, TArray<FIntPoint>& OutCells) const
{
	const FBox Bounds = Box->CalcBounds(Box->GetComponentTransform()).GetBox();
	const FIntPoint MinCell = Grid.WorldToCell(Bounds.Min);
	const FIntPoint MaxCell = Grid.WorldToCell(Bounds.Max);

	for (int32 Y = MinCell.Y; Y <= MaxCell.Y; ++Y)
	{
		for (int32 X = MinCell.X; X <= MaxCell.X; ++X)
		{
			const FIntPoint Cell(X, Y);
			const FVector Center = Grid.CellToWorld(Cell);
			if (Grid.IsInBounds(Cell)
				&& Center.X >= Bounds.Min.X && Center.X <= Bounds.Max.X
				&& Center.Y >= Bounds.Min.Y && Center.Y <= Bounds.Max.Y)
			{
				OutCells.Add(Cell);
			}
		}
	}

	if (OutCells.IsEmpty())
	{
		const FIntPoint Cell = Grid.WorldToCell(GetActorLocation());
		if (Grid.IsInBounds(Cell))
		{
			OutCells.Add(Cell);
		}
	}
}
