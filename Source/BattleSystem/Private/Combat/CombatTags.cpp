// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatTags.h"

namespace CombatTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Attack, "Attack", "Attack types of combat units.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Attack_Melee, "Attack.Melee", "Melee attack: hits after a windup when the target is within range.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Attack_Ranged, "Attack.Ranged", "Ranged attack with a projectile (phase 3).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Attack_AoE, "Attack.AoE", "Area-of-effect attack (phase 5).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Attack_Taunt, "Attack.Taunt", "Taunt ability (phase 4).");
}
