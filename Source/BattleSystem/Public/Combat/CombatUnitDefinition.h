// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Combat/CombatEffects.h"
#include "CombatUnitDefinition.generated.h"

class ACombatProjectileActor;
class ACombatUnitActor;
struct FCombatUnitStats;

/** An effect an attack applies to what it hits: tags for a duration (for example Status.Taunted). */
USTRUCT(BlueprintType)
struct FCombatEffectDefinition
{
	GENERATED_BODY()

	/** Identity for stacking: a second effect with the same tag refreshes, stacks or is ignored. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect", meta = (Categories = "Effect"))
	FGameplayTag EffectTag;

	/** Seconds the effect lasts. Rounded to simulation ticks (at least 1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect", meta = (ClampMin = 0, Units = "s"))
	float Duration = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect")
	ECombatEffectStacking Stacking = ECombatEffectStacking::Refresh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect", meta = (ClampMin = 1, EditCondition = "Stacking == ECombatEffectStacking::Stack"))
	int32 MaxStacks = 1;

	/** Tags the target has while the effect is active. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect")
	FGameplayTagContainer GrantedTags;

	/** Not applied to a target that has any of these tags (innate or from another effect). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect")
	FGameplayTagContainer BlockedByTags;
};

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

	/** Threat the target gets on the attacker per point of damage (tanks: higher). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack", meta = (ClampMin = 0))
	float ThreatMultiplier = 1.f;

	/** Applied to the target when the attack lands. For Attack.Taunt: to every enemy within Range. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack")
	TArray<FCombatEffectDefinition> Effects;
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

	/**
	 * Attack.Melee and Attack.Ranged are aimed at the target: the unit picks the shortest-range one that can reach it.
	 * Attack.Taunt is an area around the unit (Range = radius, edge to edge) that applies its effects to every enemy in it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit")
	TArray<FCombatAttackDefinition> Attacks;

	/** Innate tags, for example an immunity that an effect's BlockedByTags checks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit")
	FGameplayTagContainer Tags;

	/** Actor spawned to show this unit. Empty = ACombatUnitActor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	TSubclassOf<ACombatUnitActor> ActorClass;

	/** Converts this definition to simulation stats, with times rounded to ticks of the given rate. */
	FCombatUnitStats ToSimStats(int32 TickRate) const;
};
