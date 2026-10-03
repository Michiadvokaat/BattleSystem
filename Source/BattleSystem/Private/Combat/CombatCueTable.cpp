// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatCueTable.h"

const FCombatCue* UCombatCueTable::FindCue(const FGameplayTag& CueTag) const
{
	if (!CueTag.IsValid())
	{
		return nullptr;
	}
	return Cues.FindByPredicate([&CueTag](const FCombatCue& Cue) { return Cue.CueTag == CueTag; });
}
