// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatTags.h"

namespace CombatTags
{
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

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Anim, "Anim", "Animation actions: UCombatAnimSet maps them to montages (presentation only).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Anim_Hit, "Anim.Hit", "Hit reaction.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Anim_Death, "Anim.Death", "Death; the last pose is held.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Anim_Throw, "Anim.Throw", "Throwing.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Anim_Push, "Anim.Push", "Pushing or shoving.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Anim_Punch, "Anim.Punch", "Punching or striking.");
}
