// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SCombatHealthBar;
class SCombatStatusIcons;
class SVerticalBox;
class UCombatSubsystem;

/**
 * The player's units, one row each: name, health, status labels and current order. Clicking a row selects
 * the unit and shows its actions (Move, then a click in the arena; and its player abilities). Rebuilt per fight.
 */
class SCombatUnitList : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCombatUnitList) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UCombatSubsystem>, Subsystem)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	struct FRow
	{
		int32 UnitId = INDEX_NONE;
		TSharedPtr<SCombatHealthBar> HealthBar;
		TSharedPtr<SCombatStatusIcons> StatusIcons;
	};

	void Rebuild();
	TSharedRef<SWidget> MakeRow(int32 UnitId, const FString& Name);

	bool IsAlive(int32 UnitId) const;
	bool IsSelected(int32 UnitId) const;

	TWeakObjectPtr<UCombatSubsystem> Subsystem;
	TSharedPtr<SVerticalBox> List;
	TArray<FRow> Rows;
	int32 BuiltSerial = INDEX_NONE;
};
