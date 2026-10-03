// Copyright Epic Games, Inc. All Rights Reserved.

#include "SCombatLevelDesigner.h"
#include "Combat/CombatLevel.h"
#include "Combat/CombatSettings.h"
#include "Combat/CombatSubsystem.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace CombatLevelDesigner
{
	static const FLinearColor ActiveColor(0.2f, 0.8f, 0.3f);
}

void SCombatLevelDesigner::Construct(const FArguments& InArgs)
{
	Subsystem = InArgs._Subsystem;
	RefreshLevelOptions();
	for (const FString& Type : UCombatSubsystem::GetAllUnitDefinitionNames())
	{
		UnitTypeOptions.Add(MakeShared<FString>(Type));
	}
	SelectedUnitType = UnitTypeOptions.IsEmpty() ? nullptr : UnitTypeOptions[0];
	if (UCombatSubsystem* CombatSubsystem = Subsystem.Get(); CombatSubsystem && SelectedUnitType)
	{
		CombatSubsystem->SetDesignUnitType(*SelectedUnitType);
	}

	auto ToolButton = [this](const FText& Label, ECombatDesignTool Tool)
	{
		return MakeButton(Label,
			[this, Tool]() { if (UCombatSubsystem* Current = Subsystem.Get()) { Current->SetDesignTool(Tool); } },
			[this, Tool]() { const UCombatSubsystem* Current = Subsystem.Get(); return Current && Current->GetDesignTool() == Tool; });
	};
	auto TeamButton = [this](int32 Team)
	{
		return MakeButton(FText::FromString(FString::Printf(TEXT("Team %d"), Team)),
			[this, Team]() { if (UCombatSubsystem* Current = Subsystem.Get()) { Current->SetDesignUnitTeam(Team); } },
			[this, Team]() { const UCombatSubsystem* Current = Subsystem.Get(); return Current && Current->GetDesignUnitTeam() == Team; });
	};

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(10.f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(INVTEXT("Level Designer")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f, 4.f, 0.f)
				[
					MakeButton(INVTEXT("Edit"),
						[this]()
						{
							if (UCombatSubsystem* Current = Subsystem.Get())
							{
								if (Current->IsDesignMode())
								{
									Current->ExitDesignMode();
								}
								else
								{
									Current->EnterDesignMode();
									SyncNameFromLevel();
								}
							}
						},
						[this]() { return IsEditing(); })
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					MakeButton(INVTEXT("Play"), [this]()
					{
						if (UCombatSubsystem* Current = Subsystem.Get(); Current && Current->IsDesignMode())
						{
							Current->PlayDesignLevel(GetDefault<UCombatSettings>()->DefaultSeed);
						}
					})
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
			[
				SNew(SHorizontalBox)
				.Visibility(this, &SCombatLevelDesigner::GetEditVisibility)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ MakeLabel(INVTEXT("Name")) ]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 4.f, 0.f)
				[
					SNew(SEditableTextBox)
					.Text_Lambda([this]() { return NameText; })
					.OnTextChanged_Lambda([this](const FText& Text) { NameText = Text; })
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					MakeButton(INVTEXT("Save"), [this]()
					{
						if (UCombatSubsystem* Current = Subsystem.Get())
						{
							Current->SaveDesignLevel(NameText.ToString(), Message);
							SyncNameFromLevel();
						}
					})
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
			[
				SNew(SHorizontalBox)
				.Visibility(this, &SCombatLevelDesigner::GetEditVisibility)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ MakeLabel(INVTEXT("Level")) ]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 4.f, 0.f)
				[
					SAssignNew(LevelCombo, SComboBox<TSharedPtr<FString>>)
					.OptionsSource(&LevelOptions)
					.InitiallySelectedItem(SelectedLevel)
					.OnComboBoxOpening_Lambda([this]() { RefreshLevelOptions(); })
					.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) { return SNew(STextBlock).Text(FText::FromString(*Item)); })
					.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type) { SelectedLevel = Item; })
					[
						SNew(STextBlock).Text_Lambda([this]()
						{
							return SelectedLevel ? FText::FromString(*SelectedLevel) : INVTEXT("(no levels)");
						})
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
				[
					MakeButton(INVTEXT("Load"), [this]()
					{
						if (UCombatSubsystem* Current = Subsystem.Get(); Current && SelectedLevel)
						{
							Current->LoadDesignLevel(*SelectedLevel, Message);
							SyncNameFromLevel();
						}
					})
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					MakeButton(INVTEXT("New"), [this]()
					{
						if (UCombatSubsystem* Current = Subsystem.Get())
						{
							Current->NewDesignLevel();
							SyncNameFromLevel();
							Message = TEXT("New empty level.");
						}
					})
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
			[
				SNew(SHorizontalBox)
				.Visibility(this, &SCombatLevelDesigner::GetEditVisibility)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ MakeLabel(INVTEXT("Size")) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)[ SNew(STextBlock).Text(INVTEXT("W")) ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 10.f, 0.f)[ MakeSizeBox(true) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)[ SNew(STextBlock).Text(INVTEXT("H")) ]
				+ SHorizontalBox::Slot().AutoWidth()[ MakeSizeBox(false) ]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)
			[
				SNew(SHorizontalBox)
				.Visibility(this, &SCombatLevelDesigner::GetEditVisibility)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ MakeLabel(INVTEXT("Tool")) ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)[ ToolButton(INVTEXT("Wall"), ECombatDesignTool::Wall) ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)[ ToolButton(INVTEXT("Hedge"), ECombatDesignTool::Hedge) ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)[ ToolButton(INVTEXT("Water"), ECombatDesignTool::Water) ]
				+ SHorizontalBox::Slot().AutoWidth()[ ToolButton(INVTEXT("Unit"), ECombatDesignTool::Unit) ]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([this]()
				{
					const UCombatSubsystem* Current = Subsystem.Get();
					return IsEditing() && Current && Current->GetDesignTool() == ECombatDesignTool::Unit ? EVisibility::Visible : EVisibility::Collapsed;
				})
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ MakeLabel(INVTEXT("Unit")) ]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 4.f, 0.f)
				[
					SNew(SComboBox<TSharedPtr<FString>>)
					.OptionsSource(&UnitTypeOptions)
					.InitiallySelectedItem(SelectedUnitType)
					.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) { return SNew(STextBlock).Text(FText::FromString(*Item)); })
					.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type)
					{
						SelectedUnitType = Item;
						if (UCombatSubsystem* Current = Subsystem.Get(); Current && Item)
						{
							Current->SetDesignUnitType(*Item);
						}
					})
					[
						SNew(STextBlock).Text_Lambda([this]()
						{
							return SelectedUnitType ? FText::FromString(*SelectedUnitType) : INVTEXT("(no unit types)");
						})
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)[ TeamButton(0) ]
				+ SHorizontalBox::Slot().AutoWidth()[ TeamButton(1) ]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Visibility(this, &SCombatLevelDesigner::GetEditVisibility)
				.Text(INVTEXT("Left mouse: place (hold to paint)   Right mouse: erase"))
				.ColorAndOpacity(FLinearColor(0.75f, 0.75f, 0.75f))
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return FText::FromString(Message); })
				.Visibility_Lambda([this]() { return Message.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
			]
		]
	];
}

bool SCombatLevelDesigner::IsEditing() const
{
	const UCombatSubsystem* CombatSubsystem = Subsystem.Get();
	return CombatSubsystem && CombatSubsystem->IsDesignMode();
}

TSharedRef<SWidget> SCombatLevelDesigner::MakeButton(const FText& Label, TFunction<void()> OnClick, TFunction<bool()> IsActive)
{
	return SNew(SButton)
		.OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
		.ButtonColorAndOpacity_Lambda([IsActive]()
		{
			return FSlateColor(IsActive && IsActive() ? CombatLevelDesigner::ActiveColor : FLinearColor::White);
		})
		[
			SNew(STextBlock).Text(Label)
		];
}

TSharedRef<SWidget> SCombatLevelDesigner::MakeLabel(const FText& Label)
{
	return SNew(SBox).WidthOverride(45.f)[ SNew(STextBlock).Text(Label) ];
}

TSharedRef<SWidget> SCombatLevelDesigner::MakeSizeBox(bool bWidth)
{
	return SNew(SBox)
		.WidthOverride(60.f)
		[
			SNew(SSpinBox<int32>)
			.MinValue(FCombatLevel::MinSize)
			.MaxValue(FCombatLevel::MaxSize)
			.Delta(1)
			.Value_Lambda([this, bWidth]()
			{
				const UCombatSubsystem* Current = Subsystem.Get();
				return Current ? (bWidth ? Current->GetDesignLevel().Width : Current->GetDesignLevel().Height) : 0;
			})
			.OnValueChanged_Lambda([this, bWidth](int32 Value)
			{
				if (UCombatSubsystem* Current = Subsystem.Get())
				{
					const FCombatLevel& Level = Current->GetDesignLevel();
					Current->SetDesignSize(bWidth ? Value : Level.Width, bWidth ? Level.Height : Value);
				}
			})
		];
}

void SCombatLevelDesigner::RefreshLevelOptions()
{
	const FString Previous = SelectedLevel ? *SelectedLevel : FString();
	LevelOptions.Reset();
	SelectedLevel.Reset();
	for (const FString& Name : CombatLevels::FindLevelNames())
	{
		LevelOptions.Add(MakeShared<FString>(Name));
		if (Name == Previous || !SelectedLevel)
		{
			SelectedLevel = LevelOptions.Last();
		}
	}
	if (LevelCombo)
	{
		LevelCombo->RefreshOptions();
		LevelCombo->SetSelectedItem(SelectedLevel);
	}
}

void SCombatLevelDesigner::SyncNameFromLevel()
{
	if (const UCombatSubsystem* CombatSubsystem = Subsystem.Get())
	{
		NameText = FText::FromString(CombatSubsystem->GetDesignLevel().Name);
	}
}
