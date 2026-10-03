// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatUnitActor.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

ACombatUnitActor::ACombatUnitActor()
{
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(RootComponent);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	NoseMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("NoseMesh"));
	NoseMesh->SetupAttachment(RootComponent);
	NoseMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		BodyMesh->SetStaticMesh(CylinderMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		NoseMesh->SetStaticMesh(CubeMesh.Object);
	}

	// The engine cylinder uses DefaultMaterial, which has no color parameter.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (ShapeMaterial.Succeeded())
	{
		BodyMaterialBase = ShapeMaterial.Object;
	}
}

void ACombatUnitActor::InitUnit(int32 InUnitId, int32 InTeam, float InRadius, const FLinearColor& InTeamColor)
{
	UnitId = InUnitId;
	Team = InTeam;
	TeamColor = InTeamColor;

	// The engine cylinder and cube are 100 cm and centered on their origin.
	BodyMesh->SetRelativeScale3D(FVector(InRadius * 2.0 / 100.0, InRadius * 2.0 / 100.0, BodyHeight / 100.0));
	BodyMesh->SetRelativeLocation(FVector(0.0, 0.0, BodyHeight * 0.5));
	NoseMesh->SetRelativeScale3D(FVector(0.25));
	NoseMesh->SetRelativeLocation(FVector(InRadius, 0.0, BodyHeight * 0.75));

	BodyMaterial = BodyMaterialBase
		? BodyMesh->CreateAndSetMaterialInstanceDynamicFromMaterial(0, BodyMaterialBase)
		: BodyMesh->CreateAndSetMaterialInstanceDynamic(0);
	SetBodyColor(TeamColor);
}

void ACombatUnitActor::UpdatePresentation(const FVector& InLocation, const FVector& FacingDirection)
{
	const double Now = GetWorld()->GetTimeSeconds();

	FVector LungeOffset = FVector::ZeroVector;
	if (LungeStartTime >= 0.0 && LungeDuration > 0.f)
	{
		const double Alpha = (Now - LungeStartTime) / LungeDuration;
		if (Alpha < 1.0)
		{
			LungeOffset = LungeDirection * LungeDistance * FMath::Sin(Alpha * UE_DOUBLE_PI);
		}
		else
		{
			LungeStartTime = -1.0;
		}
	}

	SetActorLocation(InLocation + LungeOffset);
	if (!FacingDirection.IsNearlyZero())
	{
		SetActorRotation(FacingDirection.Rotation());
	}

	const bool bShouldFlash = HitFlashStartTime >= 0.0 && Now - HitFlashStartTime < HitFlashDuration;
	if (bShouldFlash != bFlashing)
	{
		bFlashing = bShouldFlash;
		SetBodyColor(bFlashing ? FLinearColor::White : TeamColor);
	}
}

void ACombatUnitActor::OnAttack(const FVector& TargetLocation)
{
	LungeDirection = (TargetLocation - GetActorLocation()).GetSafeNormal2D();
	LungeStartTime = GetWorld()->GetTimeSeconds();
	ReceiveUnitAttack(TargetLocation);
}

void ACombatUnitActor::OnHit(float Damage)
{
	HitFlashStartTime = GetWorld()->GetTimeSeconds();
	DrawDebugString(GetWorld(), GetActorLocation() + FVector(0.0, 0.0, BodyHeight + 30.0),
		FString::Printf(TEXT("-%.0f"), Damage), nullptr, FColor::Yellow, DamageTextDuration, true);
	ReceiveUnitHit(Damage);
}

void ACombatUnitActor::OnDeath()
{
	DrawDebugString(GetWorld(), GetActorLocation() + FVector(0.0, 0.0, BodyHeight * 0.5),
		TEXT("X"), nullptr, FColor::Red, DamageTextDuration * 2.f, true);
	SetActorHiddenInGame(true);
	ReceiveUnitDeath();
}

void ACombatUnitActor::SetBodyColor(const FLinearColor& Color)
{
	if (BodyMaterial)
	{
		BodyMaterial->SetVectorParameterValue(BodyColorParameter, Color);
	}
}
