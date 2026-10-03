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

	const FCombatAttackDefinition* Melee = Attacks.FindByPredicate([](const FCombatAttackDefinition& Attack)
	{
		return Attack.Type.MatchesTagExact(CombatTags::Attack_Melee);
	});

	if (Melee)
	{
		Stats.bHasAttack = true;
		Stats.AttackType = Melee->Type;
		Stats.AttackRange = Melee->Range;
		Stats.AttackDamage = Melee->Damage;
		Stats.AttackCooldownTicks = FMath::Max(FMath::RoundToInt32(Melee->Cooldown * TickRate), 1);
		Stats.AttackWindupTicks = FMath::Max(FMath::RoundToInt32(Melee->Windup * TickRate), 0);
	}
	return Stats;
}
