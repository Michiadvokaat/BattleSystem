// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"

enum class ECombatDesignTool : uint8;
class UCombatSubsystem;

/**
 * LevelDesigner menu (bottom left): edit mode, grid size, tools (wall, hedge, water, unit with type and team,
 * spawn with type and time in the selected wave), waves (select, add, remove), and new/load/save/play, plus
 * rename and delete (with a second click to confirm) of the level selected in the dropdown. Painting itself happens with the mouse on the arena (ACombatPlayerController).
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
	FReply OnDeleteClicked();
	/** The first Delete click arms it; a second within DeleteConfirmSeconds deletes. Any other button disarms it. */
	bool IsConfirmingDelete() const;

	TWeakObjectPtr<UCombatSubsystem> Subsystem;

	FText NameText;
	FString Message;

	TArray<TSharedPtr<FString>> LevelOptions;
	TSharedPtr<FString> SelectedLevel;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> LevelCombo;
	/** Platform seconds until which a Delete click deletes; 0 = not armed. */
	double DeleteConfirmUntil = 0.0;

	TArray<TSharedPtr<FString>> UnitTypeOptions;
	TSharedPtr<FString> SelectedUnitType;
};
