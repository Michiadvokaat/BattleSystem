// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Combat/CombatEffects.h"
#include "Combat/CombatTypes.h"
#include "CombatUnitDefinition.generated.h"

class ACombatProjectileActor;
class UCombatAppearance;
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

	/** Multiplies movement speed while active, per stack (0.5 = half speed). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect|Modifiers", meta = (ClampMin = 0))
	float MoveSpeedMultiplier = 1.f;

	/** Multiplies damage the unit deals while active, per stack. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect|Modifiers", meta = (ClampMin = 0))
	float DamageDealtMultiplier = 1.f;

	/** Multiplies damage the unit takes while active, per stack. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect|Modifiers", meta = (ClampMin = 0))
	float DamageTakenMultiplier = 1.f;
};

USTRUCT(BlueprintType)
struct FCombatAttackDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack", meta = (Categories = "Attack"))
	FGameplayTag Type;

	/** Name in the UI (the unit list's ability buttons). Empty = the last part of Type, for example "Taunt". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack")
	FText DisplayName;

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

	/** Applied to the target when the attack lands. For area attacks: to every affected unit in the area. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack")
	TArray<FCombatEffectDefinition> Effects;

	/** Presentation cue when it lands (hit or area going off); see UCombatCueTable. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack", meta = (Categories = "Cue"))
	FGameplayTag ImpactCue;

	/** Presentation: the montage tag in the look's UCombatAnimSet (Anim.Throw). Empty = the attack type (Attack.Melee). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack", meta = (Categories = "Anim"))
	FGameplayTag AnimationTag;

	/** Attack.AoE: the shape. Range is how far the target may be to start the attack (not used by CircleAroundSelf). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Area")
	ECombatAreaShape AreaShape = ECombatAreaShape::CircleAtTarget;

	/** Attack.AoE: size of the area in cm (circle radius, or cone length). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Area", meta = (ClampMin = 0, Units = "cm"))
	float AreaRadius = 150.f;

	/** Attack.AoE with Cone: full angle of the fan. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Area", meta = (ClampMin = 1, ClampMax = 360, Units = "deg"))
	float ConeAngle = 90.f;

	/** Attack.AoE: seconds between firing and going off; the area stays where it was placed (0 = right away). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Area", meta = (ClampMin = 0, Units = "s"))
	float TelegraphDelay = 0.f;

	/** Area attacks (AoE, taunt): hit enemies. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Area")
	bool bAffectsEnemies = true;

	/** Area attacks (AoE, taunt): hit allies, including the attacker itself (for example a buff aura). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Area")
	bool bAffectsAllies = false;
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
	 * Attack.AoE hits an area (AreaShape); with bRequiresLineOfSight it acts as ranged (needs sight), otherwise as melee.
	 * Attack.Taunt is an area around the unit (Range = radius, edge to edge) that applies its effects to every enemy in it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit")
	TArray<FCombatAttackDefinition> Attacks;

	/**
	 * Abilities only the player triggers (Ability commands); the AI never uses them. They go off right away,
	 * without cooldown or windup. For now an Attack.Taunt, or an Attack.AoE with CircleAroundSelf.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit")
	TArray<FCombatAttackDefinition> PlayerAbilities;

	/** Innate tags, for example an immunity that an effect's BlockedByTags checks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit")
	FGameplayTagContainer Tags;

	/** Actor spawned to show this unit. Empty = ACombatUnitActor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	TSubclassOf<ACombatUnitActor> ActorClass;

	/** Modular character look. Empty = the placeholder shape (cylinder or cube). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	TObjectPtr<UCombatAppearance> Appearance;

	/**
	 * Multiplies the locomotion play rate the AnimBP gets (UCombatAnimInstance::LocomotionPlayRate): above 1 the steps go
	 * faster, below 1 slower. Presentation only, not part of the fight. See it live with an ACombatAnimPreview.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation", meta = (ClampMin = 0.1, ClampMax = 4))
	float LocomotionRate = 1.f;

	/** Converts this definition to simulation stats, with times rounded to ticks of the given rate. */
	FCombatUnitStats ToSimStats(int32 TickRate) const;
};
