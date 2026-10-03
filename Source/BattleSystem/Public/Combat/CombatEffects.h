// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "CombatEffects.generated.h"

/** What happens when an effect is applied to a unit that already has an effect with the same EffectTag. */
UENUM(BlueprintType)
enum class ECombatEffectStacking : uint8
{
	/** Restart the duration; the new source replaces the old one. */
	Refresh,
	/** Add a stack (up to MaxStacks) and restart the duration; the new source replaces the old one. */
	Stack,
	/** Keep the existing effect unchanged. */
	Ignore,
};

/** An effect as the simulation sees it: copied from a definition at fight start, duration in ticks. */
struct FCombatEffectStats
{
	/** Identity for stacking. */
	FGameplayTag EffectTag;
	int32 DurationTicks = 1;
	ECombatEffectStacking Stacking = ECombatEffectStacking::Refresh;
	int32 MaxStacks = 1;
	/** Tags the unit has while the effect is active. */
	FGameplayTagContainer GrantedTags;
	/** The effect is not applied to a unit that has any of these tags (innate or granted). */
	FGameplayTagContainer BlockedByTags;

	/** Multipliers while active, applied once per stack (0.5 with 2 stacks = 0.25). */
	float MoveSpeedMultiplier = 1.f;
	float DamageDealtMultiplier = 1.f;
	float DamageTakenMultiplier = 1.f;
};

struct FCombatActiveEffect
{
	FCombatEffectStats Effect;
	int32 SourceId = INDEX_NONE;
	/** Where the effect came from in the source unit's stats; used by the checksum instead of tag names. */
	int32 AttackIndex = INDEX_NONE;
	int32 EffectIndex = INDEX_NONE;
	int32 RemainingTicks = 0;
	int32 Stacks = 1;
};

/** The active effects of one unit, in the order they were first applied. */
class BATTLESYSTEM_API FCombatEffectList
{
public:
	/** Applies an effect following its stacking rule. Returns false if it was blocked or ignored. */
	bool Apply(const FCombatEffectStats& Effect, int32 SourceId, int32 AttackIndex, int32 EffectIndex, const FGameplayTagContainer& InnateTags);

	/** Counts all durations down by one tick and removes expired effects. */
	void Tick();

	void Reset() { Effects.Reset(); }

	/** Whether an active effect grants the tag (or a child of it). */
	bool HasGrantedTag(const FGameplayTag& Tag) const;

	/** Source of the active effect that grants Tag with the most ticks left (lowest source ID on ties), or INDEX_NONE. */
	int32 FindSourceOfTag(const FGameplayTag& Tag) const;

	/** Whether an effect with this EffectTag from this source is active. */
	bool HasEffectFromSource(const FGameplayTag& EffectTag, int32 SourceId) const;

	const TArray<FCombatActiveEffect>& GetEffects() const { return Effects; }

	/** Product of all active effects' multipliers, each to the power of its stacks. */
	float GetMoveSpeedMultiplier() const { return GetMultiplier(&FCombatEffectStats::MoveSpeedMultiplier); }
	float GetDamageDealtMultiplier() const { return GetMultiplier(&FCombatEffectStats::DamageDealtMultiplier); }
	float GetDamageTakenMultiplier() const { return GetMultiplier(&FCombatEffectStats::DamageTakenMultiplier); }

	uint32 AppendChecksum(uint32 Crc) const;

private:
	float GetMultiplier(float FCombatEffectStats::* Member) const;

	TArray<FCombatActiveEffect> Effects;
};
