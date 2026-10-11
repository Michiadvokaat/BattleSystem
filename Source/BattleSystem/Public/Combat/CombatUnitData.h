// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "Combat/CombatEffects.h"
#include "Combat/CombatTypes.h"
#include "ModularCharacterLook.h"
#include "CombatUnitData.generated.h"

class ACombatProjectileActor;
class ACombatUnitActor;
class UCombatAnimSet;
class USkeletalMesh;
class UStaticMesh;
struct FCombatUnitStats;

/** An effect a skill applies to what it hits: tags for a duration (for example Status.Taunted). */
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

/**
 * A skill (a row of the skill table, DT_Skills): an attack the AI uses, or an ability the player triggers.
 * Units own skills by row name (FCombatUnitRow::Skills); several units can own the same skill.
 */
USTRUCT(BlueprintType)
struct FCombatSkillRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Name in the UI (the unit list's ability buttons). Empty = the Type, for example "Taunt". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	FString DisplayName;

	/**
	 * Melee and Ranged are aimed at the target: the unit picks the shortest-range one that can reach it.
	 * AoE hits an area (AreaShape); with bRequiresLineOfSight it acts as ranged (needs sight), otherwise as melee.
	 * Taunt is an area around the unit (Range = radius, from its center to the enemy's edge) that applies its effects to every enemy in it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	ECombatSkillType Type = ECombatSkillType::None;

	/**
	 * Off: the AI uses it in the fight. On: only the player triggers it (Ability commands); it goes off right away,
	 * without cooldown or windup. For now a Taunt, or an AoE with CircleAroundSelf.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	bool bPlayerActivated = false;

	/** Distance in cm from the attacker's center to the target's edge at which the skill can start. Area shapes reach the same way. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill", meta = (ClampMin = 0, Units = "cm"))
	float Range = 150.f;

	/** Seconds between the starts of two uses. Rounded to simulation ticks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill", meta = (ClampMin = 0, Units = "s"))
	float Cooldown = 1.f;

	/** Seconds from the start until it hits (melee) or fires (ranged). Rounded to simulation ticks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill", meta = (ClampMin = 0, Units = "s"))
	float Windup = 0.3f;

	/**
	 * Seconds the unit stands still after the hit (melee) or shot (the rest of its animation; tune the montage to it).
	 * Rounded to simulation ticks; the unit's SkillCooldownMultiplier counts.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill", meta = (ClampMin = 0, Units = "s"))
	float Recovery = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill", meta = (ClampMin = 0))
	float Damage = 10.f;

	/** Threat the target gets on the attacker per point of damage (tanks: higher). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill", meta = (ClampMin = 0))
	float ThreatMultiplier = 1.f;

	/** Applied to the target when the skill lands. For area skills: to every affected unit in the area. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	TArray<FCombatEffectDefinition> Effects;

	/** Cue when it lands (hit or area going off), passed on with the simulation's events; see UCombatCueTable. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill", meta = (Categories = "Cue"))
	FGameplayTag ImpactCue;

	/** Ranged: speed of the homing projectile. 0 = the hit lands directly. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill|Ranged", meta = (ClampMin = 0, Units = "cm/s"))
	float ProjectileSpeed = 1500.f;

	/** Ranged and AoE: only fire with a clear line of sight (no sight-blocking cells) to the target. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill|Ranged")
	bool bRequiresLineOfSight = true;

	/** AoE: the shape. Range is how far the target may be to start the skill (not used by CircleAroundSelf). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill|Area")
	ECombatAreaShape AreaShape = ECombatAreaShape::CircleAtTarget;

	/** AoE: size of the area in cm (circle radius, or cone length). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill|Area", meta = (ClampMin = 0, Units = "cm"))
	float AreaRadius = 150.f;

	/** AoE with Cone: full angle of the fan. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill|Area", meta = (ClampMin = 1, ClampMax = 360, Units = "deg"))
	float ConeAngle = 90.f;

	/** AoE: seconds between firing and going off; the area stays where it was placed (0 = right away). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill|Area", meta = (ClampMin = 0, Units = "s"))
	float TelegraphDelay = 0.f;

	/** Area skills (AoE, taunt): hit enemies. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill|Area")
	bool bAffectsEnemies = true;

	/** Area skills (AoE, taunt): hit allies, including the attacker itself (for example a buff aura). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill|Area")
	bool bAffectsAllies = false;

	/** Presentation: the montage tag in the look's UCombatAnimSet (Anim.Throw). Empty = no montage (the body lunges). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation", meta = (Categories = "Anim"))
	FGameplayTag AnimationTag;

	/** Presentation, ranged: actor that shows the projectile. Empty = ACombatProjectileActor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	TSoftClassPtr<ACombatProjectileActor> ProjectileActorClass;

	/** ProjectileActorClass, loaded; ACombatProjectileActor when empty or missing. */
	BATTLESYSTEM_API UClass* LoadProjectileActorClass() const;

	/** DisplayName, or the name of Type ("Taunt"). */
	BATTLESYSTEM_API FString GetDisplayName() const;
};

/**
 * The look of a unit: the modular character (FModularCharacterLook: parts, colours, props, scale, tag overrides; built by
 * UModularCharacterComponent) plus its animations. Pure presentation: the simulation never reads it, so changing a look
 * never changes a fight. The overrides switch on the unit's effect tags (Effect.Slow) and the tags they grant (Status.Taunted).
 */
USTRUCT(BlueprintType)
struct BATTLESYSTEM_API FCombatLook : public FModularCharacterLook
{
	GENERATED_BODY()

	/** Animations for this look's skeleton (AnimBP, locomotion, montages). Empty = the reference pose. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look")
	TSoftObjectPtr<UCombatAnimSet> AnimSet;
};

/**
 * A unit type (a row of the unit table, DT_Units): heroes and enemies. Levels name it by row name ("Tank").
 * The functional part goes into the fight; the presentation part only into the unit actor.
 */
USTRUCT(BlueprintType)
struct FCombatUnitRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Name in the UI. Empty = the row name. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit")
	FString DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit", meta = (ClampMin = 1))
	float MaxHP = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit", meta = (ClampMin = 0, Units = "cm/s"))
	float MoveSpeed = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit", meta = (ClampMin = 1, Units = "cm"))
	float Radius = 40.f;

	/** Innate tags, for example an immunity that an effect's BlockedByTags checks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit")
	FGameplayTagContainer Tags;

	/**
	 * Row names in the skill table. The AI's skills keep their order among themselves, and so do the player's
	 * (Combat.Ability <unit> <index> counts the player's skills).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skills", meta = (GetOptions = "BattleSystem.CombatSettings.GetSkillRowNames"))
	TArray<FName> Skills;

	/** Multiplies the damage of all its skills. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skills", meta = (ClampMin = 0))
	float SkillDamageMultiplier = 1.f;

	/** Multiplies the range and area radius of all its skills. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skills", meta = (ClampMin = 0))
	float SkillRangeMultiplier = 1.f;

	/** Multiplies the cooldown, windup and recovery of all its skills (below 1 = faster). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skills", meta = (ClampMin = 0))
	float SkillCooldownMultiplier = 1.f;

	/** Multiplies the duration of the effects its skills apply. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skills", meta = (ClampMin = 0))
	float EffectDurationMultiplier = 1.f;

	/** Actor spawned to show this unit. Empty = ACombatUnitActor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	TSoftClassPtr<ACombatUnitActor> ActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	FCombatLook Look;

	/**
	 * Multiplies the locomotion play rate the AnimBP gets (UCombatAnimInstance::LocomotionPlayRate): above 1 the steps go
	 * faster, below 1 slower. Presentation only, not part of the fight. See it live with an ACombatAnimPreview.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation", meta = (ClampMin = 0.1, ClampMax = 4))
	float LocomotionRate = 1.f;

	/** ActorClass, loaded; ACombatUnitActor when empty or missing. */
	BATTLESYSTEM_API UClass* LoadActorClass() const;
};

/**
 * A unit type resolved for use: its row and the rows of its skills, split like the simulation's stats
 * (FCombatAttackStats::SourceIndex indexes Attacks or PlayerAbilities). Immutable once made.
 */
struct BATTLESYSTEM_API FCombatUnitType
{
	FName Name;
	FCombatUnitRow Unit;
	/** The skills the AI uses, in the order of FCombatUnitRow::Skills. */
	TArray<FCombatSkillRow> Attacks;
	/** The skills only the player triggers, in the order of FCombatUnitRow::Skills. */
	TArray<FCombatSkillRow> PlayerAbilities;

	/** Simulation stats with the unit's multipliers applied and times rounded to ticks of the given rate. */
	FCombatUnitStats ToSimStats(int32 TickRate) const;

	/** DisplayName, or the row name. */
	FString GetDisplayName() const;
};

/**
 * The unit and skill rows a fight uses, by row name. Taken from the project's tables for a new fight and saved in
 * replays, so a replay plays (and looks) the same after the tables are tuned. Only looked up by key, never iterated
 * by the simulation.
 */
USTRUCT()
struct BATTLESYSTEM_API FCombatUnitCatalog
{
	GENERATED_BODY()

	UPROPERTY() TMap<FName, FCombatUnitRow> Units;
	UPROPERTY() TMap<FName, FCombatSkillRow> Skills;

	/**
	 * Copies a unit's row and the rows of its skills from the tables (a unit already in the catalog is kept).
	 * False, with the reason in OutError, if the unit or one of its skills is not in the tables.
	 */
	bool AddFromTables(FName UnitName, const UDataTable& UnitTable, const UDataTable& SkillTable, FString& OutError);

	/** The unit type, or null (with the reason in OutError) if the unit or one of its skills is missing. */
	TSharedPtr<const FCombatUnitType> Resolve(FName UnitName, FString* OutError = nullptr) const;
};

namespace CombatUnits
{
	/** The unit and skill tables of Project Settings > Game > Combat, loaded; null if not set or missing. */
	BATTLESYSTEM_API const UDataTable* GetUnitTable();
	BATTLESYSTEM_API const UDataTable* GetSkillTable();

	/** A unit type from the project's tables, or null (with the reason in OutError) if it or one of its skills is missing. */
	BATTLESYSTEM_API TSharedPtr<const FCombatUnitType> FindType(const FString& Name, FString* OutError = nullptr);

	/** Row names of the unit table, sorted. */
	BATTLESYSTEM_API TArray<FString> GetAllTypeNames();
}
