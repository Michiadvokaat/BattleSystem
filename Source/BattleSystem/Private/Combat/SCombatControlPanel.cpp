// Copyright Epic Games, Inc. All Rights Reserved.

#include "SCombatControlPanel.h"
#include "Combat/CombatSettings.h"
#include "Combat/CombatSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace CombatControlPanel
{
	static const FLinearColor ActiveColor(0.2f, 0.8f, 0.3f);
	static const float Speeds[] = { 0.05f, 0.1f, 0.25f, 0.5f, 1.f, 2.f, 4.f };

	/** Taunt range slider: cm, in steps of TauntRangeStep. */
	static const float MinTauntRange = 100.f;
	static const float MaxTauntRange = 1000.f;
	static const float TauntRangeStep = 50.f;
}

void SCombatControlPanel::Construct(const FArguments& InArgs)
{
	Subsystem = InArgs._Subsystem;

	const UCombatSettings* Settings = GetDefault<UCombatSettings>();
	if (UCombatSubsystem* CombatSubsystem = Subsystem.Get())
	{
		CombatSubsystem->EnsureDesignLevel();
	}

	SeedText = FText::AsNumber(Settings->DefaultSeed, &FNumberFormattingOptions::DefaultNoGrouping());
	RefreshReplayOptions();

	TSharedRef<SHorizontalBox> SpeedRow = SNew(SHorizontalBox);
	for (const float Speed : CombatControlPanel::Speeds)
	{
		SpeedRow->AddSlot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
		[
			MakeButton(FText::FromString(FString::Printf(TEXT("%gx"), Speed)),
				FOnClicked::CreateSP(this, &SCombatControlPanel::OnSpeedClicked, Speed),
				TAttribute<FSlateColor>::CreateSP(this, &SCombatControlPanel::GetSpeedColor, Speed))
		];
	}

	const FText DebugLabels[] = { INVTEXT("Off"), INVTEXT("Targets"), INVTEXT("+ Distance map") };
	TSharedRef<SHorizontalBox> DebugRow = SNew(SHorizontalBox);
	for (int32 Level = 0; Level < UE_ARRAY_COUNT(DebugLabels); ++Level)
	{
		DebugRow->AddSlot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
		[
			MakeButton(DebugLabels[Level],
				FOnClicked::CreateSP(this, &SCombatControlPanel::OnDebugClicked, Level),
				TAttribute<FSlateColor>::CreateSP(this, &SCombatControlPanel::GetDebugColor, Level))
		];
	}

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(10.f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[
				SNew(STextBlock)
				.Text(INVTEXT("Combat"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ MakeLabel(INVTEXT("Level")) ]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					// Chosen in the LevelDesigner; Start, Batch and so on play it as it is (saved or not).
					SNew(STextBlock).Text_Lambda([this]()
					{
						const UCombatSubsystem* CombatSubsystem = Subsystem.Get();
						return CombatSubsystem ? FText::FromString(CombatSubsystem->GetDesignLevel().Name) : FText::GetEmpty();
					})
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ MakeLabel(INVTEXT("Seed")) ]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 4.f, 0.f)
				[
					SNew(SEditableTextBox)
					.Text_Lambda([this]() { return SeedText; })
					.OnTextChanged_Lambda([this](const FText& Text) { SeedText = Text; })
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					MakeButton(INVTEXT("Random"), FOnClicked::CreateSP(this, &SCombatControlPanel::OnRandomSeedClicked))
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
				[
					MakeButton(INVTEXT("Start / Restart"), FOnClicked::CreateSP(this, &SCombatControlPanel::OnStartClicked))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
				[
					MakeButton(INVTEXT("Stop"), FOnClicked::CreateSP(this, &SCombatControlPanel::OnStopClicked))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
				[
					SNew(SButton)
					.OnClicked(this, &SCombatControlPanel::OnPauseClicked)
					[
						SNew(STextBlock).Text(this, &SCombatControlPanel::GetPauseText)
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
				[
					SNew(SButton)
					.OnClicked_Lambda([this]()
					{
						if (UCombatSubsystem* CombatSubsystem = Subsystem.Get())
						{
							CombatSubsystem->CycleWallMode();
						}
						return FReply::Handled();
					})
					[
						SNew(STextBlock).Text_Lambda([this]()
						{
							const UCombatSubsystem* CombatSubsystem = Subsystem.Get();
							return CombatSubsystem ? CombatSubsystem->GetWallModeText() : FText::GetEmpty();
						})
					]
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					MakeButton(INVTEXT("Reset camera (F)"), FOnClicked::CreateLambda([this]()
					{
						if (UCombatSubsystem* CombatSubsystem = Subsystem.Get())
						{
							CombatSubsystem->ResetCameraToOverview();
						}
						return FReply::Handled();
					}))
				]
			]

			// Only for fights with waves (levels).
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([this]()
				{
					const UCombatSubsystem* CombatSubsystem = Subsystem.Get();
					return CombatSubsystem && !CombatSubsystem->GetWaveText().IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed;
				})
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ MakeLabel(INVTEXT("Waves")) ]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
				[
					SNew(STextBlock).Text_Lambda([this]()
					{
						const UCombatSubsystem* CombatSubsystem = Subsystem.Get();
						return CombatSubsystem ? FText::FromString(CombatSubsystem->GetWaveText()) : FText::GetEmpty();
					})
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.IsEnabled_Lambda([this]()
					{
						const UCombatSubsystem* CombatSubsystem = Subsystem.Get();
						return CombatSubsystem && CombatSubsystem->CanCallWave();
					})
					.OnClicked_Lambda([this]()
					{
						if (UCombatSubsystem* CombatSubsystem = Subsystem.Get())
						{
							CombatSubsystem->CallWave();
						}
						return FReply::Handled();
					})
					[
						SNew(STextBlock).Text(INVTEXT("Call wave"))
					]
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ MakeLabel(INVTEXT("Speed")) ]
				+ SHorizontalBox::Slot().AutoWidth()[ SpeedRow ]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ MakeLabel(INVTEXT("Debug")) ]
				+ SHorizontalBox::Slot().AutoWidth()[ DebugRow ]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ MakeLabel(INVTEXT("Taunt")) ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
				[
					MakeButton(INVTEXT("Asset"),
						FOnClicked::CreateSP(this, &SCombatControlPanel::OnTauntRangeAssetClicked),
						TAttribute<FSlateColor>::CreateSP(this, &SCombatControlPanel::GetTauntRangeAssetColor))
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
				[
					SNew(SBox)
					.MinDesiredWidth(120.f)
					[
						SNew(SSlider)
						.MinValue(CombatControlPanel::MinTauntRange)
						.MaxValue(CombatControlPanel::MaxTauntRange)
						.StepSize(CombatControlPanel::TauntRangeStep)
						.Value(this, &SCombatControlPanel::GetTauntRangeSliderValue)
						.OnValueChanged(this, &SCombatControlPanel::OnTauntRangeChanged)
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(this, &SCombatControlPanel::GetTauntRangeText)
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ MakeLabel(INVTEXT("Show")) ]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					MakeButton(INVTEXT("Taunt range"),
						FOnClicked::CreateSP(this, &SCombatControlPanel::OnShowRangesClicked),
						TAttribute<FSlateColor>::CreateSP(this, &SCombatControlPanel::GetShowRangesColor))
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ MakeLabel(INVTEXT("Replay")) ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
				[
					MakeButton(INVTEXT("Save"), FOnClicked::CreateSP(this, &SCombatControlPanel::OnSaveReplayClicked))
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 4.f, 0.f)
				[
					SAssignNew(ReplayCombo, SComboBox<TSharedPtr<FString>>)
					.OptionsSource(&ReplayOptions)
					.OnComboBoxOpening_Lambda([this]() { RefreshReplayOptions(); })
					.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
					{
						return SNew(STextBlock).Text(FText::FromString(*Item));
					})
					.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type)
					{
						SelectedReplay = Item;
					})
					[
						SNew(STextBlock).Text_Lambda([this]()
						{
							return SelectedReplay ? FText::FromString(*SelectedReplay) : INVTEXT("(no replays)");
						})
					]
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					MakeButton(INVTEXT("Play"), FOnClicked::CreateSP(this, &SCombatControlPanel::OnPlayReplayClicked))
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ MakeLabel(INVTEXT("Batch")) ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
				[
					MakeButton(INVTEXT("100"), FOnClicked::CreateSP(this, &SCombatControlPanel::OnBatchClicked, 100))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
				[
					MakeButton(INVTEXT("1000"), FOnClicked::CreateSP(this, &SCombatControlPanel::OnBatchClicked, 1000))
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					MakeButton(INVTEXT("CSV"), FOnClicked::CreateSP(this, &SCombatControlPanel::OnBatchCsvClicked),
						TAttribute<FSlateColor>::CreateSP(this, &SCombatControlPanel::GetBatchCsvColor))
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
			[
				SNew(STextBlock).Text(this, &SCombatControlPanel::GetStatusText)
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
			[
				SNew(SBox)
				.MaxDesiredWidth(420.f)
				[
					SNew(STextBlock)
					.Text(this, &SCombatControlPanel::GetMessageText)
					.AutoWrapText(true)
					.Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
				]
			]
		]
	];
}

TSharedRef<SWidget> SCombatControlPanel::MakeButton(const FText& Label, FOnClicked OnClicked, TAttribute<FSlateColor> Color)
{
	return SNew(SButton)
		.OnClicked(OnClicked)
		.ButtonColorAndOpacity(Color)
		[
			SNew(STextBlock).Text(Label)
		];
}

TSharedRef<SWidget> SCombatControlPanel::MakeLabel(const FText& Label)
{
	return SNew(SBox)
		.WidthOverride(60.f)
		[
			SNew(STextBlock).Text(Label)
		];
}

FReply SCombatControlPanel::OnStartClicked()
{
	UCombatSubsystem* CombatSubsystem = Subsystem.Get();
	if (CombatSubsystem)
	{
		CombatSubsystem->PlayDesignLevel(FCString::Atoi(*SeedText.ToString()));
	}
	return FReply::Handled();
}

FReply SCombatControlPanel::OnStopClicked()
{
	if (UCombatSubsystem* CombatSubsystem = Subsystem.Get())
	{
		CombatSubsystem->StopFight();
	}
	return FReply::Handled();
}

FReply SCombatControlPanel::OnPauseClicked()
{
	if (UCombatSubsystem* CombatSubsystem = Subsystem.Get())
	{
		CombatSubsystem->SetPaused(!CombatSubsystem->IsPaused());
	}
	return FReply::Handled();
}

FReply SCombatControlPanel::OnRandomSeedClicked()
{
	// UI only: picking a seed is not part of the simulation.
	SeedText = FText::AsNumber(FMath::RandRange(1, 99999), &FNumberFormattingOptions::DefaultNoGrouping());
	return FReply::Handled();
}

FReply SCombatControlPanel::OnSpeedClicked(float Speed)
{
	if (UCombatSubsystem* CombatSubsystem = Subsystem.Get())
	{
		CombatSubsystem->SetTimeScale(Speed);
	}
	return FReply::Handled();
}

FReply SCombatControlPanel::OnDebugClicked(int32 Level)
{
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Combat.Debug")))
	{
		CVar->Set(Level, ECVF_SetByConsole);
	}
	return FReply::Handled();
}

FReply SCombatControlPanel::OnShowRangesClicked()
{
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Combat.ShowRanges")))
	{
		CVar->Set(AreRangesShown() ? 0 : 1, ECVF_SetByConsole);
	}
	return FReply::Handled();
}

void SCombatControlPanel::RefreshReplayOptions()
{
	const FString Previous = SelectedReplay ? *SelectedReplay : FString();
	ReplayOptions.Reset();
	SelectedReplay.Reset();
	for (const FString& File : CombatReplay::FindReplayFiles())
	{
		ReplayOptions.Add(MakeShared<FString>(File));
		if (File == Previous || !SelectedReplay)
		{
			SelectedReplay = ReplayOptions.Last();
		}
	}
	if (ReplayCombo)
	{
		ReplayCombo->RefreshOptions();
		ReplayCombo->SetSelectedItem(SelectedReplay);
	}
}

FReply SCombatControlPanel::OnSaveReplayClicked()
{
	if (const UCombatSubsystem* CombatSubsystem = Subsystem.Get())
	{
		CombatSubsystem->SaveReplay(Message);
		RefreshReplayOptions();
	}
	return FReply::Handled();
}

FReply SCombatControlPanel::OnPlayReplayClicked()
{
	UCombatSubsystem* CombatSubsystem = Subsystem.Get();
	if (CombatSubsystem && SelectedReplay)
	{
		CombatSubsystem->PlayReplay(*SelectedReplay, Message);
	}
	return FReply::Handled();
}

FReply SCombatControlPanel::OnBatchClicked(int32 Count)
{
	UCombatSubsystem* CombatSubsystem = Subsystem.Get();
	if (CombatSubsystem)
	{
		FCombatFightSource Source;
		Source.Level = CombatSubsystem->GetDesignLevel();
		// Seeds start at the seed field; the screen freezes while the batch runs.
		CombatSubsystem->RunBatch(Source, Count, FCString::Atoi(*SeedText.ToString()), bBatchCsv, Message);
	}
	return FReply::Handled();
}

FReply SCombatControlPanel::OnBatchCsvClicked()
{
	bBatchCsv = !bBatchCsv;
	return FReply::Handled();
}

FText SCombatControlPanel::GetMessageText() const
{
	return FText::FromString(Message);
}

FSlateColor SCombatControlPanel::GetBatchCsvColor() const
{
	return bBatchCsv ? CombatControlPanel::ActiveColor : FLinearColor::White;
}

FReply SCombatControlPanel::OnTauntRangeAssetClicked()
{
	if (UCombatSubsystem* CombatSubsystem = Subsystem.Get())
	{
		CombatSubsystem->SetTauntRangeOverride(0.f);
	}
	return FReply::Handled();
}

void SCombatControlPanel::OnTauntRangeChanged(float Value)
{
	if (UCombatSubsystem* CombatSubsystem = Subsystem.Get())
	{
		CombatSubsystem->SetTauntRangeOverride(FMath::GridSnap(Value, CombatControlPanel::TauntRangeStep));
	}
}

FSlateColor SCombatControlPanel::GetTauntRangeAssetColor() const
{
	const UCombatSubsystem* CombatSubsystem = Subsystem.Get();
	return CombatSubsystem && CombatSubsystem->GetTauntRangeOverride() <= 0.f ? CombatControlPanel::ActiveColor : FLinearColor::White;
}

float SCombatControlPanel::GetTauntRangeSliderValue() const
{
	const UCombatSubsystem* CombatSubsystem = Subsystem.Get();
	const float Override = CombatSubsystem ? CombatSubsystem->GetTauntRangeOverride() : 0.f;
	return Override > 0.f ? Override : CombatControlPanel::MinTauntRange;
}

FText SCombatControlPanel::GetTauntRangeText() const
{
	const UCombatSubsystem* CombatSubsystem = Subsystem.Get();
	const float Override = CombatSubsystem ? CombatSubsystem->GetTauntRangeOverride() : 0.f;
	return Override > 0.f
		? FText::FromString(FString::Printf(TEXT("%.1f m (on Start)"), Override / 100.f))
		: INVTEXT("from asset");
}

FText SCombatControlPanel::GetStatusText() const
{
	const UCombatSubsystem* CombatSubsystem = Subsystem.Get();
	const FCombatSimulation* Simulation = CombatSubsystem ? CombatSubsystem->GetSimulation() : nullptr;
	if (!Simulation)
	{
		return INVTEXT("No fight. Press Start.");
	}

	const FString Fight = FString::Printf(TEXT("%s%s, seed %d"), CombatSubsystem->IsPlayingReplay() ? TEXT("Replay: ") : TEXT(""),
		*CombatSubsystem->GetCurrentSetupName(), CombatSubsystem->GetCurrentSeed());
	if (!Simulation->IsFinished())
	{
		return FText::FromString(FString::Printf(TEXT("%s\nTick %d - %s"), *Fight, Simulation->GetTick(),
			CombatSubsystem->IsPaused() ? TEXT("paused") : TEXT("running")));
	}

	FString Result = FCombatSimulation::OutcomeToString(Simulation->GetOutcome());
	if (Simulation->GetOutcome() == ECombatOutcome::TeamWon)
	{
		Result += FString::Printf(TEXT(" (team %d)"), Simulation->GetWinningTeam());
	}
	FString Over = FString::Printf(TEXT("%s\nOver: %s after %d ticks\nChecksum 0x%08X"),
		*Fight, *Result, Simulation->GetTick(), Simulation->GetChecksum());
	if (!CombatSubsystem->GetReplayVerdict().IsEmpty())
	{
		Over += TEXT("\n") + CombatSubsystem->GetReplayVerdict();
	}
	return FText::FromString(Over);
}

FText SCombatControlPanel::GetPauseText() const
{
	const UCombatSubsystem* CombatSubsystem = Subsystem.Get();
	return CombatSubsystem && CombatSubsystem->IsPaused() ? INVTEXT("Resume") : INVTEXT("Pause");
}

FSlateColor SCombatControlPanel::GetSpeedColor(float Speed) const
{
	const UCombatSubsystem* CombatSubsystem = Subsystem.Get();
	const bool bActive = CombatSubsystem && FMath::IsNearlyEqual(CombatSubsystem->GetTimeScale(), Speed);
	return bActive ? CombatControlPanel::ActiveColor : FLinearColor::White;
}

FSlateColor SCombatControlPanel::GetDebugColor(int32 Level) const
{
	return GetDebugLevel() == Level ? CombatControlPanel::ActiveColor : FLinearColor::White;
}

FSlateColor SCombatControlPanel::GetShowRangesColor() const
{
	return AreRangesShown() ? CombatControlPanel::ActiveColor : FLinearColor::White;
}

bool SCombatControlPanel::AreRangesShown()
{
	const IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Combat.ShowRanges"));
	return CVar && CVar->GetInt() > 0;
}

int32 SCombatControlPanel::GetDebugLevel()
{
	const IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Combat.Debug"));
	return CVar ? CVar->GetInt() : 0;
}
