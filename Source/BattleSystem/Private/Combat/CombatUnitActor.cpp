// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatUnitActor.h"
#include "Combat/CombatAnimation.h"
#include "Combat/CombatSubsystem.h"
#include "Combat/CombatTags.h"
#include "Animation/AnimMontage.h"
#include "Components/WidgetComponent.h"
#include "SCombatUnitWidgets.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ModularCharacterComponent.h"
#include "TimerManager.h"
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

	CharacterMesh = CreateDefaultSubobject<UModularCharacterComponent>(TEXT("CharacterMesh"));
	CharacterMesh->SetupAttachment(RootComponent);
	CharacterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CharacterMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	CharacterMesh->SetVisibility(false);

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
	VisualHeight = BodyHeight;

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
	// The nose scales with the body (at most 25 cm), so flat pieces get a small one.
	const double NoseSize = FMath::Min(25.0, BodyHeight * 0.8);
	NoseMesh->SetRelativeScale3D(FVector(NoseSize / 100.0));
	NoseMesh->SetRelativeLocation(FVector(InRadius, 0.0, FMath::Max(BodyHeight * 0.75, NoseSize * 0.5)));

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

void ACombatUnitActor::InitLook(const FCombatLook& InLook, int32 Seed)
{
	if (!InLook.HasParts())
	{
		return;
	}
	CharacterMesh->bBuildingInConstructionScript = bBuildingInConstruction;
	const bool bBuilt = CharacterMesh->BuildLook(InLook, Seed);
	CharacterMesh->bBuildingInConstructionScript = false;
	if (!bBuilt)
	{
		UE_LOG(LogCombat, Warning, TEXT("Unit %d keeps the placeholder (its look has no fixed part)."), UnitId);
		return;
	}
	LookMeshScale = InLook.GetMeshScale();
	AnimSet = InLook.AnimSet.LoadSynchronous();
	if (AnimSet && AnimSet->AnimClass)
	{
		// In the editor world (ACombatAnimPreview) the pose only updates when asked to.
		CharacterMesh->SetUpdateAnimationInEditor(!GetWorld()->IsGameWorld());
		CharacterMesh->SetAnimInstanceClass(AnimSet->AnimClass);
		AnimInstance = Cast<UCombatAnimInstance>(CharacterMesh->GetAnimInstance());
		if (AnimInstance)
		{
			AnimInstance->SetLocomotion(AnimSet->Locomotion);
		}
		IdleStream.Initialize(Seed);
		NextIdleBreak = IdleStream.FRandRange(AnimSet->MinIdleBreakInterval, AnimSet->MaxIdleBreakInterval);
	}
	CharacterMesh->SetVisibility(true);
	BodyMesh->SetVisibility(false);
	NoseMesh->SetVisibility(false);

	// Widgets and texts by the figure's height (feet at the mesh origin) instead of BodyHeight.
	VisualHeight = FMath::Max(CharacterMesh->GetSkeletalMeshAsset()->GetBounds().GetBox().Max.Z * LookMeshScale.Z, 10.0);
	HealthBarWidget->SetRelativeLocation(FVector(0.0, 0.0, VisualHeight + HealthBarOffset));
	StatusWidget->SetRelativeLocation(FVector(0.0, 0.0, VisualHeight * 0.5));
}

bool ACombatUnitActor::HasLookOverrides() const
{
	return CharacterMesh->HasOverrides();
}

void ACombatUnitActor::SetActiveTags(const FGameplayTagContainer& InTags)
{
	CharacterMesh->SetActiveTags(InTags);
}

bool ACombatUnitActor::SetSlotMesh(EModularLookSlot Slot, USkeletalMesh* Mesh)
{
	return CharacterMesh->SetSlotMesh(Slot, Mesh);
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

void ACombatUnitActor::SetLocomotionTuning(float InDefinitionMoveSpeed, float InLocomotionRate)
{
	DefinitionMoveSpeed = InDefinitionMoveSpeed;
	LocomotionRate = InLocomotionRate;
}

void ACombatUnitActor::SetAnimationState(const FVector& Velocity, float RateScale)
{
	const float MoveSpeed = Velocity.Size2D();
	AnimRateScale = RateScale;
	if (!AnimSet)
	{
		return;
	}
	// A recompiled AnimBP (editor) replaces the anim instance, and the AnimSet's blend space can be swapped: pick both up again.
	UCombatAnimInstance* Current = Cast<UCombatAnimInstance>(CharacterMesh->GetAnimInstance());
	if (Current != AnimInstance || (Current && Current->Locomotion != AnimSet->Locomotion))
	{
		AnimInstance = Current;
		if (AnimInstance)
		{
			AnimInstance->SetLocomotion(AnimSet->Locomotion);
		}
	}
	if (!AnimInstance)
	{
		return;
	}
	AnimInstance->SetTuning(DefinitionMoveSpeed, LocomotionRate);
	AnimInstance->SetMeshScale(LookMeshScale);
	AnimInstance->SetLocalVelocity(UCombatAnimInstance::ToLocalVelocity(FVector2D(Velocity), GetActorRotation().Yaw));
	// The parts follow the body's pose, so the body's rate is enough.
	CharacterMesh->GlobalAnimRateScale = RateScale;

	if (AnimSet->IdleBreaks.IsEmpty())
	{
		return;
	}
	if (MoveSpeed > 1.f || AnimInstance->IsAnyMontagePlaying())
	{
		IdleTime = 0.f;
		return;
	}
	IdleTime += GetWorld()->GetDeltaSeconds() * RateScale;
	if (IdleTime >= NextIdleBreak)
	{
		if (UAnimMontage* IdleBreak = AnimSet->IdleBreaks[IdleStream.RandHelper(AnimSet->IdleBreaks.Num())])
		{
			AnimInstance->Montage_Play(IdleBreak);
		}
		IdleTime = 0.f;
		NextIdleBreak = IdleStream.FRandRange(AnimSet->MinIdleBreakInterval, AnimSet->MaxIdleBreakInterval);
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
		// An animated figure turns at TurnRate (in animation time); the placeholder turns at once.
		const FRotator Target = FacingDirection.Rotation();
		const float TurnRate = AnimSet && bHasFacing ? AnimSet->TurnRate : 0.f;
		bHasFacing = true;
		SetActorRotation(TurnRate > 0.f
			? FMath::RInterpConstantTo(GetActorRotation(), Target, GetWorld()->GetDeltaSeconds() * AnimRateScale, TurnRate)
			: Target);
	}

	const bool bShouldFlash = HitFlashStartTime >= 0.0 && Now - HitFlashStartTime < HitFlashDuration;
	if (bShouldFlash != bFlashing)
	{
		bFlashing = bShouldFlash;
		SetBodyColor(bFlashing ? FLinearColor::White : TeamColor);
	}
}

void ACombatUnitActor::OnAttack(const FVector& TargetLocation, FGameplayTag AnimationTag, float WindupSeconds)
{
	UAnimMontage* Montage = AnimInstance ? AnimSet->FindMontage(AnimationTag) : nullptr;
	if (Montage)
	{
		// Faster or slower, so the montage's Impact notify comes when the simulation hits.
		AnimInstance->Montage_Play(Montage, AnimSet->GetPlayRateForImpact(UCombatAnimSet::FindImpactTime(Montage), WindupSeconds));
		AttackMontage = Montage;
		IdleTime = 0.f;
	}
	else
	{
		LungeDirection = (TargetLocation - GetActorLocation()).GetSafeNormal2D();
		LungeStartTime = GetWorld()->GetTimeSeconds();
	}
	ReceiveUnitAttack(TargetLocation);
}

void ACombatUnitActor::OnHit(float Damage)
{
	HitFlashStartTime = GetWorld()->GetTimeSeconds();
	// A hit reaction never cuts off an attack or the death.
	if (AnimInstance && !AnimInstance->bDead && !(AttackMontage && AnimInstance->Montage_IsPlaying(AttackMontage)))
	{
		if (UAnimMontage* HitMontage = AnimSet->FindMontage(CombatTags::Anim_Hit))
		{
			AnimInstance->Montage_Play(HitMontage);
		}
	}
	DrawDebugString(GetWorld(), GetActorLocation() + FVector(0.0, 0.0, VisualHeight + 30.0),
		FString::Printf(TEXT("-%.0f"), Damage), nullptr, FColor::Yellow, DamageTextDuration, true);
	ReceiveUnitHit(Damage);
}

void ACombatUnitActor::OnDeath()
{
	UAnimMontage* DeathMontage = AnimInstance ? AnimSet->FindMontage(CombatTags::Anim_Death) : nullptr;
	if (DeathMontage)
	{
		// The figure falls and stays lying for CorpseDuration; its widgets and markers go at once.
		HealthBarWidget->SetVisibility(false);
		StatusWidget->SetVisibility(false);
		SetSelected(false);
		SetMoveTarget(false, FVector::ZeroVector);
		AnimInstance->PlayDeath(DeathMontage);
		const float HideDelay = DeathMontage->GetPlayLength() + AnimSet->CorpseDuration;
		FTimerHandle HideTimer;
		GetWorldTimerManager().SetTimer(HideTimer, FTimerDelegate::CreateWeakLambda(this, [this]() { SetActorHiddenInGame(true); }), HideDelay, false);
		ReceiveUnitDeath();
		return;
	}

	DrawDebugString(GetWorld(), GetActorLocation() + FVector(0.0, 0.0, VisualHeight * 0.5),
		TEXT("X"), nullptr, FColor::Red, DamageTextDuration * 2.f, true);
	SetActorHiddenInGame(true);
	ReceiveUnitDeath();
}

void ACombatUnitActor::OnAreaAttack(float Radius)
{
	ReceiveUnitAreaAttack(Radius);
}

void ACombatUnitActor::MakeGhost(UMaterialInterface* Material, float Opacity)
{
	HealthBarWidget->SetVisibility(false);
	StatusWidget->SetVisibility(false);
	UMaterialInstanceDynamic* GhostMaterial = Material ? UMaterialInstanceDynamic::Create(Material, this) : nullptr;
	if (GhostMaterial)
	{
		GhostMaterial->SetVectorParameterValue(TEXT("Color"), TeamColor);
		GhostMaterial->SetScalarParameterValue(TEXT("Opacity"), Opacity);
	}
	TArray<UMeshComponent*> Meshes;
	GetComponents<UMeshComponent>(Meshes);
	for (UMeshComponent* Mesh : Meshes)
	{
		Mesh->SetCastShadow(false);
		for (int32 Index = 0; GhostMaterial && Index < Mesh->GetNumMaterials(); ++Index)
		{
			Mesh->SetMaterial(Index, GhostMaterial);
		}
	}
	// The body flash would set the old body material's color; a ghost is never hit.
	BodyMaterial = nullptr;
}

void ACombatUnitActor::SetBodyColor(const FLinearColor& Color)
{
	if (BodyMaterial)
	{
		BodyMaterial->SetVectorParameterValue(BodyColorParameter, Color);
	}
}
