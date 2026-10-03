// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "CombatUnitDefinition.generated.h"

class ACombatProjectileActor;
class ACombatUnitActor;
struct FCombatUnitStats;

USTRUCT(BlueprintType)
struct FCombatAttackDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack", meta = (Categories = "Attack"))
	FGameplayTag Type;

	/** Edge-to-edge distance in cm at which the attack can start. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack", meta = (ClampMin = 0, Units = "cm"))
	float Range = 150.f;

	/** Seconds between the starts of two attacks. Rounded to simulation ticks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack", meta = (ClampMin = 0, Units = "s"))
	float Cooldown = 1.f;

	/** Seconds from the start of the attack until it hits (melee) or fires (ranged). Rounded to simulation ticks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack", meta = (ClampMin = 0, Units = "s"))
	float Windup = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack", meta = (ClampMin = 0))
	float Damage = 10.f;

	/** Ranged: speed of the homing projectile. 0 = the hit lands directly. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Ranged", meta = (ClampMin = 0, Units = "cm/s"))
	float ProjectileSpeed = 1500.f;

	/** Ranged: only fire with a clear line of sight (no sight-blocking cells) to the target. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Ranged")
	bool bRequiresLineOfSight = true;

	/** Ranged: actor that shows the projectile. Empty = ACombatProjectileActor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Ranged")
	TSubclassOf<ACombatProjectileActor> ProjectileActorClass;
};

/** A unit type: stats, attacks and the actor class that shows it. Read-only during a fight. */
UCLASS(BlueprintType)
class BATTLESYSTEM_API UCombatUnitDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit", meta = (ClampMin = 1))
	float MaxHP = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit", meta = (ClampMin = 0, Units = "cm/s"))
	float MoveSpeed = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit", meta = (ClampMin = 1, Units = "cm"))
	float Radius = 40.f;

	/** Attack.Melee and Attack.Ranged entries are used; the unit picks the shortest-range one that can reach its target. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit")
	TArray<FCombatAttackDefinition> Attacks;

	/** Actor spawned to show this unit. Empty = ACombatUnitActor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	TSubclassOf<ACombatUnitActor> ActorClass;

	/** Converts this definition to simulation stats, with times rounded to ticks of the given rate. */
	FCombatUnitStats ToSimStats(int32 TickRate) const;
};
