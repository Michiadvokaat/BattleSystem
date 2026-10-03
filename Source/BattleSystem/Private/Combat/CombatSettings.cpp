// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatSettings.h"

FLinearColor UCombatSettings::GetTeamColor(int32 Team) const
{
	if (TeamColors.IsEmpty())
	{
		return FLinearColor::White;
	}
	return TeamColors[FMath::Abs(Team) % TeamColors.Num()];
}
