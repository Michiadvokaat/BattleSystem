// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"

class UCombatSubsystem;

/**
 * In-game control panel for the combat arena: choose a setup and seed, start/stop, pause and speed,
 * and the Combat.Debug level. Built in code; shown by ACombatHUD.
 */
class SCombatControlPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCombatControlPanel) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UCombatSubsystem>, Subsystem)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FReply OnStartClicked();
	FReply OnStopClicked();
	FReply OnPauseClicked();
	FReply OnRandomSeedClicked();
	FReply OnSpeedClicked(float Speed);
	FReply OnDebugClicked(int32 Level);
	FReply OnShowRangesClicked();
	FReply OnTauntRangeAssetClicked();
	FReply OnSaveReplayClicked();
	FReply OnPlayReplayClicked();
	FReply OnBatchClicked(int32 Count);
	FReply OnBatchCsvClicked();
	void RefreshReplayOptions();
	void OnTauntRangeChanged(float Value);

	FText GetStatusText() const;
	FText GetPauseText() const;
	FSlateColor GetSpeedColor(float Speed) const;
	FSlateColor GetDebugColor(int32 Level) const;
	FSlateColor GetShowRangesColor() const;
	FSlateColor GetTauntRangeAssetColor() const;
	float GetTauntRangeSliderValue() const;
	FText GetTauntRangeText() const;
	FText GetMessageText() const;
	FSlateColor GetBatchCsvColor() const;

	static int32 GetDebugLevel();
	static bool AreRangesShown();

	TSharedRef<SWidget> MakeButton(const FText& Label, FOnClicked OnClicked, TAttribute<FSlateColor> Color = FSlateColor(FLinearColor::White));
	TSharedRef<SWidget> MakeLabel(const FText& Label);

	TWeakObjectPtr<UCombatSubsystem> Subsystem;

	TArray<TSharedPtr<FString>> ReplayOptions;
	TSharedPtr<FString> SelectedReplay;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> ReplayCombo;

	/** Result of the last save/load/batch action. */
	FString Message;
	bool bBatchCsv = false;
	FText SeedText;
};
