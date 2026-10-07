// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatSettings.h"
#include "Engine/DataTable.h"

FLinearColor UCombatSettings::GetTeamColor(int32 Team) const
{
	if (TeamColors.IsEmpty())
	{
		return FLinearColor::White;
	}
	return TeamColors[FMath::Abs(Team) % TeamColors.Num()];
}

TArray<FName> UCombatSettings::GetSkillRowNames()
{
	TArray<FName> Names;
	if (const UDataTable* Table = GetDefault<UCombatSettings>()->SkillTable.LoadSynchronous())
	{
		Names = Table->GetRowNames();
	}
	Names.Sort(FNameLexicalLess());
	return Names;
}

TArray<FName> UCombatSettings::GetUnitRowNames()
{
	TArray<FName> Names;
	if (const UDataTable* Table = GetDefault<UCombatSettings>()->UnitTable.LoadSynchronous())
	{
		Names = Table->GetRowNames();
	}
	Names.Sort(FNameLexicalLess());
	return Names;
}
