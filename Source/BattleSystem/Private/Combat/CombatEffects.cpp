// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatEffects.h"
#include "Misc/Crc.h"

bool FCombatEffectList::Apply(const FCombatEffectStats& Effect, int32 SourceId, int32 AttackIndex, int32 EffectIndex, const FGameplayTagContainer& InnateTags)
{
	if (InnateTags.HasAny(Effect.BlockedByTags))
	{
		return false;
	}
	for (const FCombatActiveEffect& Active : Effects)
	{
		if (Active.Effect.GrantedTags.HasAny(Effect.BlockedByTags))
		{
			return false;
		}
	}

	FCombatActiveEffect* Existing = Effects.FindByPredicate([&Effect](const FCombatActiveEffect& Active)
	{
		return Active.Effect.EffectTag == Effect.EffectTag;
	});

	if (!Existing)
	{
		FCombatActiveEffect& Added = Effects.AddDefaulted_GetRef();
		Added.Effect = Effect;
		Added.SourceId = SourceId;
		Added.AttackIndex = AttackIndex;
		Added.EffectIndex = EffectIndex;
		Added.RemainingTicks = FMath::Max(Effect.DurationTicks, 1);
		Added.Stacks = 1;
		return true;
	}

	switch (Effect.Stacking)
	{
	case ECombatEffectStacking::Ignore:
		return false;
	case ECombatEffectStacking::Stack:
		Existing->Stacks = FMath::Min(Existing->Stacks + 1, FMath::Max(Effect.MaxStacks, 1));
		break;
	case ECombatEffectStacking::Refresh:
		break;
	}

	Existing->Effect = Effect;
	Existing->SourceId = SourceId;
	Existing->AttackIndex = AttackIndex;
	Existing->EffectIndex = EffectIndex;
	Existing->RemainingTicks = FMath::Max(Effect.DurationTicks, 1);
	return true;
}

void FCombatEffectList::Tick()
{
	for (FCombatActiveEffect& Active : Effects)
	{
		--Active.RemainingTicks;
	}
	Effects.RemoveAll([](const FCombatActiveEffect& Active) { return Active.RemainingTicks <= 0; });
}

bool FCombatEffectList::HasGrantedTag(const FGameplayTag& Tag) const
{
	return Effects.ContainsByPredicate([&Tag](const FCombatActiveEffect& Active)
	{
		return Active.Effect.GrantedTags.HasTag(Tag);
	});
}

int32 FCombatEffectList::FindSourceOfTag(const FGameplayTag& Tag) const
{
	const FCombatActiveEffect* Best = nullptr;
	for (const FCombatActiveEffect& Active : Effects)
	{
		if (Active.Effect.GrantedTags.HasTag(Tag)
			&& (!Best || Active.RemainingTicks > Best->RemainingTicks
				|| (Active.RemainingTicks == Best->RemainingTicks && Active.SourceId < Best->SourceId)))
		{
			Best = &Active;
		}
	}
	return Best ? Best->SourceId : INDEX_NONE;
}

bool FCombatEffectList::HasEffectFromSource(const FGameplayTag& EffectTag, int32 SourceId) const
{
	return Effects.ContainsByPredicate([&EffectTag, SourceId](const FCombatActiveEffect& Active)
	{
		return Active.Effect.EffectTag == EffectTag && Active.SourceId == SourceId;
	});
}

uint32 FCombatEffectList::AppendChecksum(uint32 Crc) const
{
	for (const FCombatActiveEffect& Active : Effects)
	{
		Crc = FCrc::MemCrc32(&Active.SourceId, sizeof(Active.SourceId), Crc);
		Crc = FCrc::MemCrc32(&Active.AttackIndex, sizeof(Active.AttackIndex), Crc);
		Crc = FCrc::MemCrc32(&Active.EffectIndex, sizeof(Active.EffectIndex), Crc);
		Crc = FCrc::MemCrc32(&Active.RemainingTicks, sizeof(Active.RemainingTicks), Crc);
		Crc = FCrc::MemCrc32(&Active.Stacks, sizeof(Active.Stacks), Crc);
	}
	return Crc;
}
