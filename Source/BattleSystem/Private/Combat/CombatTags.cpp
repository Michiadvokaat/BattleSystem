// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatTags.h"

namespace CombatTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Attack, "Attack", "Attack types of combat units.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Attack_Melee, "Attack.Melee", "Melee attack: hits after a windup when the target is within range.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Attack_Ranged, "Attack.Ranged", "Ranged attack with a projectile (phase 3).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Attack_AoE, "Attack.AoE", "Area attack: a circle at the target, a circle around the attacker, or a cone; optionally telegraphed.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Attack_Taunt, "Attack.Taunt", "Area ability around the unit that applies its effects (taunt) to enemies in range.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Status, "Status", "States a unit can be in, granted by effects or innate.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Status_Taunted, "Status.Taunted", "The unit must target the source of the effect that grants this tag.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Effect, "Effect", "Identity of an effect, used for stacking.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Effect_Taunt, "Effect.Taunt", "The taunt effect.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Effect_Slow, "Effect.Slow", "Slows movement.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Effect_Rally, "Effect.Rally", "Raises damage dealt.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cue, "Cue", "Presentation cues: UCombatCueTable maps them to VFX, sound and a debug color.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cue_Fire, "Cue.Fire", "Fire impact.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cue_Cleave, "Cue.Cleave", "Cleave swing.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cue_Rally, "Cue.Rally", "Rally aura.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cue_Taunt, "Cue.Taunt", "Taunt.");
}
