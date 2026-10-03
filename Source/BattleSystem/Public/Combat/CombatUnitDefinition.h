// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "CombatUnitDefinition.generated.h"

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

	/** Seconds from the start of the attack until it hits. Rounded to simulation ticks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack", meta = (ClampMin = 0, Units = "s"))
	float Windup = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack", meta = (ClampMin = 0))
	float Damage = 10.f;
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

	/** Phase 1 uses the first Attack.Melee entry. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit")
	TArray<FCombatAttackDefinition> Attacks;

	/** Actor spawned to show this unit. Empty = ACombatUnitActor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	TSubclassOf<ACombatUnitActor> ActorClass;

	/** Converts this definition to simulation stats, with times rounded to ticks of the given rate. */
	FCombatUnitStats ToSimStats(int32 TickRate) const;
};
