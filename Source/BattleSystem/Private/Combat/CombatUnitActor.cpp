// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatUnitActor.h"
#include "Components/WidgetComponent.h"
#include "SCombatUnitWidgets.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
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

	// Screen-space widgets always face the camera; they follow the actor's location only.
	HealthBarWidget = CreateDefaultSubobject<UWidgetComponent>(TEXT("HealthBarWidget"));
	HealthBarWidget->SetupAttachment(RootComponent);
	HealthBarWidget->SetWidgetSpace(EWidgetSpace::Screen);
	HealthBarWidget->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	StatusWidget = CreateDefaultSubobject<UWidgetComponent>(TEXT("StatusWidget"));
	StatusWidget->SetupAttachment(RootComponent);
	StatusWidget->SetWidgetSpace(EWidgetSpace::Screen);
	StatusWidget->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	StatusWidget->SetDrawSize(FVector2D(120.0, 24.0));

	MoveTargetMarker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MoveTargetMarker"));
	MoveTargetMarker->SetupAttachment(RootComponent);
	MoveTargetMarker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MoveTargetMarker->SetCastShadow(false);
	MoveTargetMarker->SetUsingAbsoluteLocation(true);
	MoveTargetMarker->SetUsingAbsoluteRotation(true);
	MoveTargetMarker->SetUsingAbsoluteScale(true);
	MoveTargetMarker->SetVisibility(false);

	MoveTargetLine = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MoveTargetLine"));
	MoveTargetLine->SetupAttachment(RootComponent);
	MoveTargetLine->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MoveTargetLine->SetCastShadow(false);
	MoveTargetLine->SetUsingAbsoluteLocation(true);
	MoveTargetLine->SetUsingAbsoluteRotation(true);
	MoveTargetLine->SetUsingAbsoluteScale(true);
	MoveTargetLine->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		MeleeBodyMesh = CylinderMesh.Object;
		BodyMesh->SetStaticMesh(CylinderMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		RangedBodyMesh = CubeMesh.Object;
		NoseMesh->SetStaticMesh(CubeMesh.Object);
	}

	// The engine cylinder uses DefaultMaterial, which has no color parameter.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (ShapeMaterial.Succeeded())
	{
		BodyMaterialBase = ShapeMaterial.Object;
	}
}

void ACombatUnitActor::InitUnit(int32 InUnitId, int32 InTeam, float InRadius, const FLinearColor& InTeamColor, bool bRanged)
{
	UnitId = InUnitId;
	Team = InTeam;
	TeamColor = InTeamColor;

	if (UStaticMesh* Shape = bRanged ? RangedBodyMesh : MeleeBodyMesh)
	{
		BodyMesh->SetStaticMesh(Shape);
	}

	// Selection ring, move disc and line: the engine shapes in a slightly brighter team color.
	if (BodyMaterialBase)
	{
		MarkerMaterial = UMaterialInstanceDynamic::Create(BodyMaterialBase, this);
		MarkerMaterial->SetVectorParameterValue(BodyColorParameter, TeamColor * 1.5f);
	}
	MoveTargetMarker->SetStaticMesh(MeleeBodyMesh);
	MoveTargetMarker->SetMaterial(0, MarkerMaterial);
	MoveTargetLine->SetStaticMesh(RangedBodyMesh);
	MoveTargetLine->SetMaterial(0, MarkerMaterial);

	const float RingRadius = InRadius + SelectionRingOffset;
	const float SegmentLength = 2.f * UE_PI * RingRadius / SelectionRingSegments * 0.6f;
	for (int32 Index = 0; Index < SelectionRingSegments; ++Index)
	{
		const float Angle = 2.f * UE_PI * Index / SelectionRingSegments;
		UStaticMeshComponent* Segment = NewObject<UStaticMeshComponent>(this);
		Segment->SetupAttachment(RootComponent);
		Segment->SetStaticMesh(RangedBodyMesh);
		Segment->SetMaterial(0, MarkerMaterial);
		Segment->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Segment->SetCastShadow(false);
		// The engine cube is 100 cm: a flat block along the circle's tangent.
		Segment->SetRelativeLocation(FVector(FMath::Cos(Angle) * RingRadius, FMath::Sin(Angle) * RingRadius, 3.0));
		Segment->SetRelativeRotation(FRotator(0.0, FMath::RadiansToDegrees(Angle) + 90.0, 0.0));
		Segment->SetRelativeScale3D(FVector(SegmentLength / 100.0, 0.08, 0.03));
		Segment->SetVisibility(false);
		Segment->RegisterComponent();
		SelectionRing.Add(Segment);
	}

	// The engine cylinder and cube are 100 cm and centered on their origin.
	BodyMesh->SetRelativeScale3D(FVector(InRadius * 2.0 / 100.0, InRadius * 2.0 / 100.0, BodyHeight / 100.0));
	BodyMesh->SetRelativeLocation(FVector(0.0, 0.0, BodyHeight * 0.5));
	NoseMesh->SetRelativeScale3D(FVector(0.25));
	NoseMesh->SetRelativeLocation(FVector(InRadius, 0.0, BodyHeight * 0.75));

	BodyMaterial = BodyMaterialBase
		? BodyMesh->CreateAndSetMaterialInstanceDynamicFromMaterial(0, BodyMaterialBase)
		: BodyMesh->CreateAndSetMaterialInstanceDynamic(0);
	SetBodyColor(TeamColor);

	HealthBar = SNew(SCombatHealthBar).TeamColor(TeamColor);
	HealthBarWidget->SetSlateWidget(HealthBar);
	HealthBarWidget->SetDrawSize(HealthBarSize);
	HealthBarWidget->SetRelativeLocation(FVector(0.0, 0.0, BodyHeight + HealthBarOffset));

	StatusIcons = SNew(SCombatStatusIcons);
	StatusWidget->SetSlateWidget(StatusIcons);
	StatusWidget->SetRelativeLocation(FVector(0.0, 0.0, BodyHeight * 0.5));
}

void ACombatUnitActor::SetHealth(float Fraction)
{
	if (HealthBar)
	{
		HealthBar->SetFraction(Fraction);
	}
}

void ACombatUnitActor::SetSelected(bool bSelected)
{
	for (UStaticMeshComponent* Segment : SelectionRing)
	{
		if (Segment && Segment->IsVisible() != bSelected)
		{
			Segment->SetVisibility(bSelected);
		}
	}
}

void ACombatUnitActor::SetMoveTarget(bool bActive, const FVector& Target)
{
	MoveTargetMarker->SetVisibility(bActive);
	MoveTargetLine->SetVisibility(bActive);
	if (!bActive)
	{
		return;
	}

	// The engine cylinder and cube are 100 cm: a flat disc on the target, a thin flat bar from the unit to it.
	MoveTargetMarker->SetWorldLocationAndRotation(Target + FVector(0.0, 0.0, 3.0), FRotator::ZeroRotator);
	MoveTargetMarker->SetWorldScale3D(FVector(0.5, 0.5, 0.02));

	const FVector From = GetActorLocation() + FVector(0.0, 0.0, 3.0);
	const FVector To = Target + FVector(0.0, 0.0, 3.0);
	const FVector Delta = To - From;
	MoveTargetLine->SetWorldLocationAndRotation((From + To) * 0.5, Delta.Rotation());
	MoveTargetLine->SetWorldScale3D(FVector(Delta.Size() / 100.0, 0.04, 0.02));
}

void ACombatUnitActor::SetStatusEffects(const TArray<FCombatStatusDisplay>& Icons)
{
	if (StatusIcons)
	{
		StatusIcons->SetIcons(Icons);
	}
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

void ACombatUnitActor::OnAreaAttack(float Radius)
{
	ReceiveUnitAreaAttack(Radius);
}

void ACombatUnitActor::SetBodyColor(const FLinearColor& Color)
{
	if (BodyMaterial)
	{
		BodyMaterial->SetVectorParameterValue(BodyColorParameter, Color);
	}
}
