// Copyright Epic Games, Inc. All Rights Reserved.

#include "SCombatControlPanel.h"
#include "Combat/CombatSettings.h"
#include "Combat/CombatSetup.h"
#include "Combat/CombatSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace CombatControlPanel
{
	static const FLinearColor ActiveColor(0.2f, 0.8f, 0.3f);
	static const float Speeds[] = { 0.5f, 1.f, 2.f, 4.f };
}

void SCombatControlPanel::Construct(const FArguments& InArgs)
{
	Subsystem = InArgs._Subsystem;

	const UCombatSettings* Settings = GetDefault<UCombatSettings>();
	const FString DefaultSetupName = Settings->DefaultSetup.ToSoftObjectPath().GetAssetName();
	for (const FString& Name : UCombatSubsystem::GetAllSetupNames())
	{
		SetupOptions.Add(MakeShared<FString>(Name));
		if (Name == DefaultSetupName || !SelectedSetup)
		{
			SelectedSetup = SetupOptions.Last();
		}
	}
	SeedText = FText::AsNumber(Settings->DefaultSeed, &FNumberFormattingOptions::DefaultNoGrouping());

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
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ MakeLabel(INVTEXT("Setup")) ]
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					SNew(SComboBox<TSharedPtr<FString>>)
					.OptionsSource(&SetupOptions)
					.InitiallySelectedItem(SelectedSetup)
					.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
					{
						return SNew(STextBlock).Text(FText::FromString(*Item));
					})
					.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type)
					{
						SelectedSetup = Item;
					})
					[
						SNew(STextBlock).Text_Lambda([this]()
						{
							return SelectedSetup ? FText::FromString(*SelectedSetup) : INVTEXT("(no setups)");
						})
					]
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
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.OnClicked(this, &SCombatControlPanel::OnPauseClicked)
					[
						SNew(STextBlock).Text(this, &SCombatControlPanel::GetPauseText)
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

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
			[
				SNew(STextBlock).Text(this, &SCombatControlPanel::GetStatusText)
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
	UCombatSetup* Setup = SelectedSetup ? UCombatSubsystem::FindSetup(*SelectedSetup) : nullptr;
	if (CombatSubsystem && Setup)
	{
		CombatSubsystem->StartFight(FCString::Atoi(*SeedText.ToString()), Setup);
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

FText SCombatControlPanel::GetStatusText() const
{
	const UCombatSubsystem* CombatSubsystem = Subsystem.Get();
	const FCombatSimulation* Simulation = CombatSubsystem ? CombatSubsystem->GetSimulation() : nullptr;
	if (!Simulation)
	{
		return INVTEXT("No fight. Press Start.");
	}

	const FString Fight = FString::Printf(TEXT("%s, seed %d"), *CombatSubsystem->GetCurrentSetupName(), CombatSubsystem->GetCurrentSeed());
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
	return FText::FromString(FString::Printf(TEXT("%s\nOver: %s after %d ticks\nChecksum 0x%08X"),
		*Fight, *Result, Simulation->GetTick(), Simulation->GetChecksum()));
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

int32 SCombatControlPanel::GetDebugLevel()
{
	const IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Combat.Debug"));
	return CVar ? CVar->GetInt() : 0;
}
