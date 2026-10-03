// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatUnitDefinition.h"
#include "Combat/CombatSimulation.h"
#include "Combat/CombatTags.h"

FCombatUnitStats UCombatUnitDefinition::ToSimStats(int32 TickRate) const
{
	FCombatUnitStats Stats;
	Stats.MaxHP = MaxHP;
	Stats.MoveSpeed = MoveSpeed;
	Stats.Radius = Radius;

	for (int32 Index = 0; Index < Attacks.Num(); ++Index)
	{
		const FCombatAttackDefinition& Definition = Attacks[Index];
		const bool bMelee = Definition.Type.MatchesTagExact(CombatTags::Attack_Melee);
		const bool bRanged = Definition.Type.MatchesTagExact(CombatTags::Attack_Ranged);
		if (!bMelee && !bRanged)
		{
			continue;
		}

		FCombatAttackStats& Attack = Stats.Attacks.AddDefaulted_GetRef();
		Attack.Type = Definition.Type;
		Attack.Range = Definition.Range;
		Attack.Damage = Definition.Damage;
		Attack.CooldownTicks = FMath::Max(FMath::RoundToInt32(Definition.Cooldown * TickRate), 1);
		Attack.WindupTicks = FMath::Max(FMath::RoundToInt32(Definition.Windup * TickRate), 0);
		Attack.bNeedsWalkableLine = bMelee;
		Attack.bNeedsLineOfSight = bRanged && Definition.bRequiresLineOfSight;
		Attack.ProjectileSpeed = bRanged ? Definition.ProjectileSpeed : 0.f;
		Attack.SourceIndex = Index;
	}
	return Stats;
}
