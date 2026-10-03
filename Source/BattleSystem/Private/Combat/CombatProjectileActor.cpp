// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatProjectileActor.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

ACombatProjectileActor::ACombatProjectileActor()
{
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		Mesh->SetStaticMesh(SphereMesh.Object);
	}

	// The engine sphere uses a material without a color parameter.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (ShapeMaterial.Succeeded())
	{
		MaterialBase = ShapeMaterial.Object;
	}
}

void ACombatProjectileActor::InitProjectile(int32 InProjectileId, const FLinearColor& TeamColor)
{
	ProjectileId = InProjectileId;

	// The engine sphere is 100 cm and centered on its origin.
	Mesh->SetRelativeScale3D(FVector(Diameter / 100.0));
	Mesh->SetRelativeLocation(FVector(0.0, 0.0, FlightHeight));

	UMaterialInstanceDynamic* Material = MaterialBase
		? Mesh->CreateAndSetMaterialInstanceDynamicFromMaterial(0, MaterialBase)
		: Mesh->CreateAndSetMaterialInstanceDynamic(0);
	if (Material)
	{
		Material->SetVectorParameterValue(ColorParameter, TeamColor);
	}
}

void ACombatProjectileActor::UpdatePresentation(const FVector& InLocation, const FVector& Direction)
{
	SetActorLocation(InLocation);
	if (!Direction.IsNearlyZero())
	{
		SetActorRotation(Direction.Rotation());
	}
}
