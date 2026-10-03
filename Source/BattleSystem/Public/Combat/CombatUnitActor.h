// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CombatUnitActor.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;

/**
 * Presentation of one simulation unit. Spawned and driven by UCombatSubsystem: it only follows the
 * simulation (interpolated position, events) and contains no gameplay logic.
 * Placeholder look: a cylinder in the team color with a "nose" that shows the facing.
 */
UCLASS()
class BATTLESYSTEM_API ACombatUnitActor : public AActor
{
	GENERATED_BODY()

public:
	ACombatUnitActor();

	virtual void InitUnit(int32 InUnitId, int32 InTeam, float InRadius, const FLinearColor& InTeamColor);

	/** Called every frame with the interpolated location and the direction to face (may be zero). */
	virtual void UpdatePresentation(const FVector& InLocation, const FVector& FacingDirection);

	virtual void OnAttack(const FVector& TargetLocation);
	virtual void OnHit(float Damage);
	virtual void OnDeath();

	int32 GetUnitId() const { return UnitId; }
	int32 GetTeam() const { return Team; }

protected:
	UFUNCTION(BlueprintImplementableEvent, Category = "Combat", meta = (DisplayName = "On Unit Attack"))
	void ReceiveUnitAttack(FVector TargetLocation);

	UFUNCTION(BlueprintImplementableEvent, Category = "Combat", meta = (DisplayName = "On Unit Hit"))
	void ReceiveUnitHit(float Damage);

	UFUNCTION(BlueprintImplementableEvent, Category = "Combat", meta = (DisplayName = "On Unit Death"))
	void ReceiveUnitDeath();

	UPROPERTY(VisibleAnywhere, Category = "Combat")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, Category = "Combat")
	TObjectPtr<UStaticMeshComponent> NoseMesh;

	/** Base material for the body; tinted with the team color through BodyColorParameter. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Presentation")
	TObjectPtr<UMaterialInterface> BodyMaterialBase;

	/** Vector parameter of BodyMaterialBase that receives the team color and the hit flash. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Presentation")
	FName BodyColorParameter = TEXT("Color");

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Presentation", meta = (Units = "cm"))
	float BodyHeight = 180.f;

	/** How far the body lunges towards the target on an attack. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Presentation", meta = (Units = "cm"))
	float LungeDistance = 30.f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Presentation", meta = (Units = "s"))
	float LungeDuration = 0.15f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Presentation", meta = (Units = "s"))
	float HitFlashDuration = 0.1f;

	/** How long damage numbers stay visible. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Presentation", meta = (Units = "s"))
	float DamageTextDuration = 1.f;

private:
	void SetBodyColor(const FLinearColor& Color);

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BodyMaterial;

	int32 UnitId = INDEX_NONE;
	int32 Team = 0;
	FLinearColor TeamColor = FLinearColor::White;

	double LungeStartTime = -1.0;
	FVector LungeDirection = FVector::ZeroVector;
	double HitFlashStartTime = -1.0;
	bool bFlashing = false;
};
