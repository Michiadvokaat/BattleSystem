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

	FText GetStatusText() const;
	FText GetPauseText() const;
	FSlateColor GetSpeedColor(float Speed) const;
	FSlateColor GetDebugColor(int32 Level) const;

	static int32 GetDebugLevel();

	TSharedRef<SWidget> MakeButton(const FText& Label, FOnClicked OnClicked, TAttribute<FSlateColor> Color = FSlateColor(FLinearColor::White));
	TSharedRef<SWidget> MakeLabel(const FText& Label);

	TWeakObjectPtr<UCombatSubsystem> Subsystem;

	TArray<TSharedPtr<FString>> SetupOptions;
	TSharedPtr<FString> SelectedSetup;
	FText SeedText;
};
