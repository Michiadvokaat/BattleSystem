// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"

enum class ECombatDesignTool : uint8;
class UCombatSubsystem;

/**
 * LevelDesigner menu (bottom left): edit mode, grid size, tools (wall, hedge, water, unit with type and team,
 * spawn with type and time in the selected wave), waves (select, add, remove), and new/load/save/play. Painting itself happens with the mouse on the arena (ACombatPlayerController).
 */
class SCombatLevelDesigner : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCombatLevelDesigner) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UCombatSubsystem>, Subsystem)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	bool IsEditing() const;
	EVisibility GetEditVisibility() const { return IsEditing() ? EVisibility::Visible : EVisibility::Collapsed; }

	TSharedRef<SWidget> MakeButton(const FText& Label, TFunction<void()> OnClick, TFunction<bool()> IsActive = nullptr);
	TSharedRef<SWidget> MakeLabel(const FText& Label);
	TSharedRef<SWidget> MakeSizeBox(bool bWidth);

	void RefreshLevelOptions();
	void SyncNameFromLevel();

	TWeakObjectPtr<UCombatSubsystem> Subsystem;

	FText NameText;
	FString Message;

	TArray<TSharedPtr<FString>> LevelOptions;
	TSharedPtr<FString> SelectedLevel;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> LevelCombo;

	TArray<TSharedPtr<FString>> UnitTypeOptions;
	TSharedPtr<FString> SelectedUnitType;
};
