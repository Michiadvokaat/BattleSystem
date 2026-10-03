// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CombatProjectileActor.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;

/**
 * Presentation of one simulation projectile. Spawned, moved and destroyed by UCombatSubsystem; it only
 * follows the simulation. Placeholder look: a small sphere in the team color.
 */
UCLASS()
class BATTLESYSTEM_API ACombatProjectileActor : public AActor
{
	GENERATED_BODY()

public:
	ACombatProjectileActor();

	virtual void InitProjectile(int32 InProjectileId, const FLinearColor& TeamColor);

	/** Called every frame with the interpolated location (at floor height) and the flight direction. */
	virtual void UpdatePresentation(const FVector& InLocation, const FVector& Direction);

	int32 GetProjectileId() const { return ProjectileId; }

protected:
	UPROPERTY(VisibleAnywhere, Category = "Combat")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** Base material for the sphere; tinted with the team color through ColorParameter. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Presentation")
	TObjectPtr<UMaterialInterface> MaterialBase;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Presentation")
	FName ColorParameter = TEXT("Color");

	/** Height above the floor at which the projectile flies. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Presentation", meta = (Units = "cm"))
	float FlightHeight = 110.f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Presentation", meta = (Units = "cm"))
	float Diameter = 20.f;

private:
	int32 ProjectileId = INDEX_NONE;
};
