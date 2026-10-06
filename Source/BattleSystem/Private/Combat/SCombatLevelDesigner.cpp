// Copyright Epic Games, Inc. All Rights Reserved.

#include "SCombatLevelDesigner.h"
#include "Algo/StableSort.h"
#include "Combat/CombatLevel.h"
#include "Combat/CombatPieces.h"
#include "Combat/CombatSettings.h"
#include "Combat/CombatSubsystem.h"
#include "HAL/PlatformTime.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#if WITH_EDITOR
#include "AssetThumbnail.h"
#endif

namespace CombatLevelDesigner
{
	static const FLinearColor ActiveColor(0.2f, 0.8f, 0.3f);
	static const FLinearColor ConfirmColor(0.9f, 0.15f, 0.1f);
	static constexpr double DeleteConfirmSeconds = 3.0;
	static constexpr float ThumbnailSize = 64.f;
	static constexpr float PaletteWidth = 440.f;

	/** The main category of a catalog category: the part before its first "/" (the whole name without one). */
	static FString GetGroup(const FString& Category)
	{
		FString Group;
		return Category.Split(TEXT("/"), &Group, nullptr) ? Group : Category;
	}

	/** The subcategory: the part after the first "/" (empty without one). */
	static FString GetSubCategory(const FString& Category)
	{
		FString Sub;
		return Category.Split(TEXT("/"), nullptr, &Sub) ? Sub : FString();
	}
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
				// Undo / redo of the edited level (Ctrl+Z, Ctrl+Y).
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
				[
					SNew(SButton)
					.Visibility(this, &SCombatLevelDesigner::GetEditVisibility)
					.ToolTipText(INVTEXT("Undo (Ctrl+Z)"))
					.IsEnabled_Lambda([this]() { const UCombatSubsystem* Current = Subsystem.Get(); return Current && Current->CanUndoDesign(); })
					.OnClicked_Lambda([this]() { if (UCombatSubsystem* Current = Subsystem.Get()) { Current->UndoDesign(); } return FReply::Handled(); })
					[
						SNew(STextBlock).Text(INVTEXT("Undo"))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
				[
					SNew(SButton)
					.Visibility(this, &SCombatLevelDesigner::GetEditVisibility)
					.ToolTipText(INVTEXT("Redo (Ctrl+Y or Ctrl+Shift+Z)"))
					.IsEnabled_Lambda([this]() { const UCombatSubsystem* Current = Subsystem.Get(); return Current && Current->CanRedoDesign(); })
					.OnClicked_Lambda([this]() { if (UCombatSubsystem* Current = Subsystem.Get()) { Current->RedoDesign(); } return FReply::Handled(); })
					[
						SNew(STextBlock).Text(INVTEXT("Redo"))
					]
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
					.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type)
					{
						SelectedLevel = Item;
						DeleteConfirmUntil = 0.0;
					})
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
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
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
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
				[
					// The selected level gets the name from the Name field.
					MakeButton(INVTEXT("Rename"), [this]()
					{
						if (UCombatSubsystem* Current = Subsystem.Get(); Current && SelectedLevel)
						{
							if (Current->RenameLevelFile(*SelectedLevel, NameText.ToString(), Message))
							{
								SelectedLevel = MakeShared<FString>(CombatLevels::CleanName(NameText.ToString()));
								RefreshLevelOptions();
								SyncNameFromLevel();
							}
						}
					})
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.OnClicked(this, &SCombatLevelDesigner::OnDeleteClicked)
					.ButtonColorAndOpacity_Lambda([this]()
					{
						return FSlateColor(IsConfirmingDelete() ? CombatLevelDesigner::ConfirmColor : FLinearColor::White);
					})
					[
						SNew(STextBlock).Text_Lambda([this]() { return IsConfirmingDelete() ? INVTEXT("Confirm?") : INVTEXT("Delete"); })
					]
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
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ MakeLabel(INVTEXT("Mode")) ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)[ ToolButton(INVTEXT("Build Mode"), ECombatDesignTool::Build) ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)[ ToolButton(INVTEXT("Unit Mode"), ECombatDesignTool::Unit) ]
				+ SHorizontalBox::Slot().AutoWidth()[ ToolButton(INVTEXT("Spawn Mode"), ECombatDesignTool::Spawn) ]
			]

			// Spawn Mode, waves: select, add after the selected one, remove the selected one. The arena shows the selected wave's spawns.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
			[
				SNew(SHorizontalBox)
				.Visibility(this, &SCombatLevelDesigner::GetToolVisibility, ECombatDesignTool::Spawn)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ MakeLabel(INVTEXT("Wave")) ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
				[
					MakeButton(INVTEXT("<"), [this]()
					{
						if (UCombatSubsystem* Current = Subsystem.Get()) { Current->SetDesignWave(Current->GetDesignWave() - 1); }
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
				[
					SNew(SBox)
					.WidthOverride(50.f)
					.HAlign(HAlign_Center)
					[
						SNew(STextBlock).Text_Lambda([this]()
						{
							const UCombatSubsystem* Current = Subsystem.Get();
							if (!Current || Current->GetDesignWave() == INDEX_NONE)
							{
								return INVTEXT("none");
							}
							return FText::FromString(FString::Printf(TEXT("%d / %d"), Current->GetDesignWave() + 1, Current->GetDesignLevel().Waves.Num()));
						})
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 10.f, 0.f)
				[
					MakeButton(INVTEXT(">"), [this]()
					{
						if (UCombatSubsystem* Current = Subsystem.Get()) { Current->SetDesignWave(Current->GetDesignWave() + 1); }
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
				[
					MakeButton(INVTEXT("+"), [this]()
					{
						if (UCombatSubsystem* Current = Subsystem.Get()) { Current->AddDesignWave(); }
					})
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					MakeButton(INVTEXT("-"), [this]()
					{
						if (UCombatSubsystem* Current = Subsystem.Get()) { Current->RemoveDesignWave(); }
					})
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([this]()
				{
					return IsEditing() && (IsTool(ECombatDesignTool::Unit) || IsTool(ECombatDesignTool::Spawn)) ? EVisibility::Visible : EVisibility::Collapsed;
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
							// The subsystem's type (a picked-up unit changes it).
							const UCombatSubsystem* Current = Subsystem.Get();
							if (Current && !Current->GetDesignUnitType().IsEmpty())
							{
								return FText::FromString(Current->GetDesignUnitType());
							}
							return SelectedUnitType ? FText::FromString(*SelectedUnitType) : INVTEXT("(no unit types)");
						})
					]
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					// Unit: the team. Spawn: the time after the wave start (spawns are always the wave team).
					SNew(SHorizontalBox)
					.Visibility(this, &SCombatLevelDesigner::GetToolVisibility, ECombatDesignTool::Unit)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)[ TeamButton(0) ]
					+ SHorizontalBox::Slot().AutoWidth()[ TeamButton(1) ]
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SHorizontalBox)
					.Visibility(this, &SCombatLevelDesigner::GetToolVisibility, ECombatDesignTool::Spawn)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)[ SNew(STextBlock).Text(INVTEXT("Time (s)")) ]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SBox)
						.WidthOverride(60.f)
						[
							SNew(SSpinBox<float>)
							.MinValue(0.f)
							.MaxValue(600.f)
							.Delta(0.5f)
							.Value_Lambda([this]()
							{
								const UCombatSubsystem* Current = Subsystem.Get();
								return Current ? Current->GetDesignSpawnTime() : 0.f;
							})
							.OnValueChanged_Lambda([this](float Value)
							{
								if (UCombatSubsystem* Current = Subsystem.Get())
								{
									Current->SetDesignSpawnTime(Value);
								}
							})
						]
					]
				]
			]

			// Unit and Spawn Mode: the start rotation of the next unit or spawn (45 degree steps, kept between placements).
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([this]()
				{
					return IsEditing() && (IsTool(ECombatDesignTool::Unit) || IsTool(ECombatDesignTool::Spawn)) ? EVisibility::Visible : EVisibility::Collapsed;
				})
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ MakeLabel(INVTEXT("Rotation")) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
				[
					SNew(STextBlock).Text_Lambda([this]()
					{
						const UCombatSubsystem* Current = Subsystem.Get();
						return FText::FromString(FString::Printf(TEXT("%g°"), Current ? Current->GetDesignUnitDegrees() : 0.f));
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
				[
					MakeButton(INVTEXT("Rotate (R)"), [this]() { if (UCombatSubsystem* Current = Subsystem.Get()) { Current->RotateDesignUnit(1); } })
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					MakeButton(INVTEXT("Back (Shift+R)"), [this]() { if (UCombatSubsystem* Current = Subsystem.Get()) { Current->RotateDesignUnit(-1); } })
				]
			]

			// Build Mode: main categories, their subcategories, the pieces of the selected one, and the rotation.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
			[
				SNew(SVerticalBox)
				.Visibility(this, &SCombatLevelDesigner::GetToolVisibility, ECombatDesignTool::Build)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
				[
					SAssignNew(CategoryBox, SWrapBox).PreferredSize(CombatLevelDesigner::PaletteWidth)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
				[
					SAssignNew(SubCategoryBox, SWrapBox).PreferredSize(CombatLevelDesigner::PaletteWidth)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBox)
					.MaxDesiredHeight(250.f)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							SAssignNew(PaletteBox, SWrapBox).PreferredSize(CombatLevelDesigner::PaletteWidth)
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ MakeLabel(INVTEXT("Rotation")) ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
					[
						SNew(STextBlock).Text_Lambda([this]()
						{
							UCombatSubsystem* Current = Subsystem.Get();
							return FText::FromString(FString::Printf(TEXT("%g\u00B0"), Current ? Current->GetDesignPieceDegrees() : 0.f));
						})
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
					[
						MakeButton(INVTEXT("Rotate (R)"), [this]() { if (UCombatSubsystem* Current = Subsystem.Get()) { Current->RotateDesignPiece(1); } })
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						MakeButton(INVTEXT("Back (Shift+R)"), [this]() { if (UCombatSubsystem* Current = Subsystem.Get()) { Current->RotateDesignPiece(-1); } })
					]
				]
				// Wall items: the height of the next one (PageUp / PageDown).
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
				[
					SNew(SHorizontalBox)
					.Visibility_Lambda([this]()
					{
						UCombatSubsystem* Current = Subsystem.Get();
						return Current && Current->IsDesignPieceWallItem() ? EVisibility::Visible : EVisibility::Collapsed;
					})
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ MakeLabel(INVTEXT("Height")) ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
					[
						SNew(STextBlock).Text_Lambda([this]()
						{
							const UCombatSubsystem* Current = Subsystem.Get();
							return FText::FromString(FString::Printf(TEXT("%g cm"), Current ? Current->GetDesignWallItemHeight() : 0.f));
						})
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
					[
						MakeButton(INVTEXT("Up (PgUp)"), [this]() { if (UCombatSubsystem* Current = Subsystem.Get()) { Current->RaiseDesignWallItem(1); } })
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						MakeButton(INVTEXT("Down (PgDn)"), [this]() { if (UCombatSubsystem* Current = Subsystem.Get()) { Current->RaiseDesignWallItem(-1); } })
					]
				]
				// Tintable pieces (solid floors): the swatches, the current color (opens the color picker) and the eyedropper.
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
				[
					SNew(SHorizontalBox)
					.Visibility_Lambda([this]()
					{
						UCombatSubsystem* Current = Subsystem.Get();
						return Current && Current->IsDesignPieceTintable() ? EVisibility::Visible : EVisibility::Collapsed;
					})
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ MakeLabel(INVTEXT("Color")) ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)[ MakeColorSwatches() ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
					[
						SNew(SButton)
						.ToolTipText(INVTEXT("Current color: click for the color picker"))
						.OnClicked(this, &SCombatLevelDesigner::OnPickColorClicked)
						[
							SNew(SBox)
							.WidthOverride(36.f)
							.HeightOverride(18.f)
							[
								SNew(SColorBlock).Color_Lambda([this]()
								{
									const UCombatSubsystem* Current = Subsystem.Get();
									return FLinearColor(Current ? Current->GetDesignPieceColor() : FColor::White);
								})
							]
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						MakeButton(INVTEXT("Pipet (I)"),
							[this]() { if (UCombatSubsystem* Current = Subsystem.Get()) { Current->SetDesignEyedropper(!Current->IsDesignEyedropper()); } },
							[this]() { const UCombatSubsystem* Current = Subsystem.Get(); return Current && Current->IsDesignEyedropper(); })
					]
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Visibility(this, &SCombatLevelDesigner::GetEditVisibility)
				.Text_Lambda([this]()
				{
					return IsTool(ECombatDesignTool::Build)
						? INVTEXT("Left: place   Ctrl+Left: move a piece   Shift+Left: erase   R / Shift+R: rotate   PgUp / PgDn: wall item height   I: pick a floor color   Right click: deselect / put back / erase   Right drag: look")
						: INVTEXT("Left: place   Ctrl+Left: move a unit   Shift+Left: erase   R / Shift+R: rotate   Right click: deselect / put back / erase   Right drag: look");
				})
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
	RebuildPieceCategories();
}

void SCombatLevelDesigner::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	UCombatSubsystem* Current = Subsystem.Get();
	const UCombatPieceCatalog* Catalog = Current && IsTool(ECombatDesignTool::Build) ? Current->GetPieceCatalog() : nullptr;
	if (!Catalog || Current->GetDesignPiece() == FollowedPiece)
	{
		return;
	}
	FollowedPiece = Current->GetDesignPiece();
	const FCombatPieceDefinition* Selected = Catalog->Find(FollowedPiece);
	if (Selected && Selected->Category != SelectedCategory)
	{
		ShowCategory(Selected->Category);
	}
}

bool SCombatLevelDesigner::IsTool(ECombatDesignTool Tool) const
{
	const UCombatSubsystem* Current = Subsystem.Get();
	return Current && Current->GetDesignTool() == Tool;
}

void SCombatLevelDesigner::RebuildPieceCategories()
{
	CategoryBox->ClearChildren();
	UCombatSubsystem* Current = Subsystem.Get();
	const UCombatPieceCatalog* Catalog = Current ? Current->GetPieceCatalog() : nullptr;
	if (!Catalog || Catalog->Pieces.IsEmpty())
	{
		CategoryBox->AddSlot()[ SNew(STextBlock).Text(INVTEXT("No PieceCatalog (Project Settings > Combat), or it is empty.")) ];
		return;
	}

	// Main categories in catalog order; the selected piece (or the first one) picks the category shown first.
	TArray<FString> Groups;
	for (const FCombatPieceDefinition& Definition : Catalog->Pieces)
	{
		Groups.AddUnique(CombatLevelDesigner::GetGroup(Definition.Category));
	}
	const FCombatPieceDefinition* Selected = Catalog->Find(Current->GetDesignPiece());
	if (!Selected)
	{
		Selected = &Catalog->Pieces[0];
		Current->SetDesignPiece(Selected->Id);
	}
	FollowedPiece = Selected->Id;

	for (const FString& Group : Groups)
	{
		CategoryBox->AddSlot().Padding(0.f, 0.f, 4.f, 4.f)
		[
			MakeButton(FText::FromString(Group),
				[this, Group]()
				{
					// The group's first subcategory (catalog order).
					UCombatSubsystem* Clicked = Subsystem.Get();
					const UCombatPieceCatalog* ClickedCatalog = Clicked ? Clicked->GetPieceCatalog() : nullptr;
					const FCombatPieceDefinition* First = ClickedCatalog ? ClickedCatalog->Pieces.FindByPredicate(
						[&Group](const FCombatPieceDefinition& Definition) { return CombatLevelDesigner::GetGroup(Definition.Category) == Group; }) : nullptr;
					if (First)
					{
						ShowCategory(First->Category);
					}
				},
				[this, Group]() { return SelectedGroup == Group; })
		];
	}
	ShowCategory(Selected->Category);
}

void SCombatLevelDesigner::ShowCategory(const FString& Category)
{
	SelectedCategory = Category;
	SelectedGroup = CombatLevelDesigner::GetGroup(Category);
	RebuildSubCategories();
}

void SCombatLevelDesigner::RebuildSubCategories()
{
	SubCategoryBox->ClearChildren();
	UCombatSubsystem* Current = Subsystem.Get();
	const UCombatPieceCatalog* Catalog = Current ? Current->GetPieceCatalog() : nullptr;
	TArray<FString> Categories;
	if (Catalog)
	{
		for (const FCombatPieceDefinition& Definition : Catalog->Pieces)
		{
			if (CombatLevelDesigner::GetGroup(Definition.Category) == SelectedGroup)
			{
				Categories.AddUnique(Definition.Category);
			}
		}
	}
	for (const FString& Category : Categories)
	{
		const FString Sub = CombatLevelDesigner::GetSubCategory(Category);
		if (Sub.IsEmpty())
		{
			continue;
		}
		SubCategoryBox->AddSlot().Padding(0.f, 0.f, 4.f, 4.f)
		[
			MakeButton(FText::FromString(Sub),
				[this, Category]() { ShowCategory(Category); },
				[this, Category]() { return SelectedCategory == Category; })
		];
	}
	RebuildPalette();
}

void SCombatLevelDesigner::RebuildPalette()
{
	PaletteBox->ClearChildren();
	Thumbnails.Reset();
	UCombatSubsystem* Current = Subsystem.Get();
	const UCombatPieceCatalog* Catalog = Current ? Current->GetPieceCatalog() : nullptr;
	if (!Catalog)
	{
		return;
	}
	// Small to large: by area, then the longer side, then the id; large and small pieces share a subcategory.
	TArray<const FCombatPieceDefinition*> Shown;
	for (const FCombatPieceDefinition& Definition : Catalog->Pieces)
	{
		if (Definition.Category == SelectedCategory)
		{
			Shown.Add(&Definition);
		}
	}
	Algo::StableSort(Shown, [](const FCombatPieceDefinition* A, const FCombatPieceDefinition* B)
	{
		const int32 AreaA = A->Size.X * A->Size.Y;
		const int32 AreaB = B->Size.X * B->Size.Y;
		if (AreaA != AreaB)
		{
			return AreaA < AreaB;
		}
		const int32 SideA = FMath::Max(A->Size.X, A->Size.Y);
		const int32 SideB = FMath::Max(B->Size.X, B->Size.Y);
		return SideA != SideB ? SideA < SideB : A->Id < B->Id;
	});
	for (const FCombatPieceDefinition* Definition : Shown)
	{
		PaletteBox->AddSlot().Padding(2.f)[ MakePieceButton(*Definition) ];
	}
}

TSharedRef<SWidget> SCombatLevelDesigner::MakePieceButton(const FCombatPieceDefinition& Definition)
{
	const FString Id = Definition.Id;
	FString Name = Id;
	Id.Split(TEXT("/"), nullptr, &Name, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	Name.RemoveFromStart(TEXT("SM_"));
	// The footprint on the button (details are always one position).
	const FString SizeLabel = Definition.Layer == ECombatPieceLayer::Detail ? FString()
		: Definition.Layer == ECombatPieceLayer::Wall ? FString::Printf(TEXT("%g m"), FMath::Max(Definition.Size.X, 1) / static_cast<float>(FMath::Max(Definition.DetailGrid, 1)))
		: FString::Printf(TEXT("%dx%d"), Definition.Size.X, Definition.Size.Y);

	TSharedRef<SWidget> Picture = SNew(SBox).WidthOverride(CombatLevelDesigner::ThumbnailSize).HeightOverride(CombatLevelDesigner::ThumbnailSize);
#if WITH_EDITOR
	if (Definition.Mesh)
	{
		if (!ThumbnailPool)
		{
			ThumbnailPool = MakeShared<FAssetThumbnailPool>(64);
		}
		const TSharedRef<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(Definition.Mesh.Get(),
			CombatLevelDesigner::ThumbnailSize, CombatLevelDesigner::ThumbnailSize, ThumbnailPool);
		Thumbnails.Add(Thumbnail);
		Picture = SNew(SBox)
			.WidthOverride(CombatLevelDesigner::ThumbnailSize)
			.HeightOverride(CombatLevelDesigner::ThumbnailSize)
			[
				Thumbnail->MakeThumbnailWidget()
			];
	}
#endif

	const FString Layer = StaticEnum<ECombatPieceLayer>()->GetNameStringByValue(static_cast<int64>(Definition.Layer));
	return SNew(SButton)
		.ToolTipText(FText::FromString(FString::Printf(TEXT("%s\n%s, %d x %d%s"), *Id, *Layer, Definition.Size.X, Definition.Size.Y,
			Definition.Layer == ECombatPieceLayer::Floor ? TEXT("") : (Definition.bBlocksWalking || Definition.bBlocksSight ? TEXT(", blocks") : TEXT(", open")))))
		.ButtonColorAndOpacity_Lambda([this, Id]()
		{
			const UCombatSubsystem* Current = Subsystem.Get();
			const bool bSelected = Current && Current->GetDesignPiece() == Id;
			return FSlateColor(bSelected ? CombatLevelDesigner::ActiveColor : FLinearColor::White);
		})
		.OnClicked_Lambda([this, Id]()
		{
			if (UCombatSubsystem* Current = Subsystem.Get())
			{
				Current->SetDesignTool(ECombatDesignTool::Build);
				Current->SetDesignPiece(Id);
			}
			return FReply::Handled();
		})
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ Picture ]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Visibility(SizeLabel.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
				.Text(FText::FromString(SizeLabel))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
				.ColorAndOpacity(FLinearColor(1.f, 0.85f, 0.3f))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox)
				.WidthOverride(CombatLevelDesigner::ThumbnailSize)
				[
					SNew(STextBlock).Text(FText::FromString(Name)).AutoWrapText(true).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
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
		.OnClicked_Lambda([this, OnClick]()
		{
			DeleteConfirmUntil = 0.0;
			OnClick();
			return FReply::Handled();
		})
		.ButtonColorAndOpacity_Lambda([IsActive]()
		{
			return FSlateColor(IsActive && IsActive() ? CombatLevelDesigner::ActiveColor : FLinearColor::White);
		})
		[
			SNew(STextBlock).Text(Label)
		];
}

TSharedRef<SWidget> SCombatLevelDesigner::MakeColorSwatches()
{
	TSharedRef<SHorizontalBox> Swatches = SNew(SHorizontalBox);
	for (const FColor& Color : GetDefault<UCombatSettings>()->FloorColors)
	{
		Swatches->AddSlot().AutoWidth().Padding(0.f, 0.f, 2.f, 0.f)
		[
			SNew(SButton)
			.ContentPadding(1.f)
			.ButtonColorAndOpacity_Lambda([this, Color]()
			{
				const UCombatSubsystem* Current = Subsystem.Get();
				return FSlateColor(Current && Current->GetDesignPieceColor() == Color ? CombatLevelDesigner::ActiveColor : FLinearColor::White);
			})
			.OnClicked_Lambda([this, Color]()
			{
				if (UCombatSubsystem* Current = Subsystem.Get())
				{
					Current->SetDesignPieceColor(Color);
				}
				return FReply::Handled();
			})
			[
				SNew(SBox)
				.WidthOverride(18.f)
				.HeightOverride(18.f)
				[
					SNew(SColorBlock).Color(FLinearColor(Color))
				]
			]
		];
	}
	return Swatches;
}

FReply SCombatLevelDesigner::OnPickColorClicked()
{
	const UCombatSubsystem* Current = Subsystem.Get();
	FColorPickerArgs Args(FLinearColor(Current ? Current->GetDesignPieceColor() : FColor::White),
		FOnLinearColorValueChanged::CreateSPLambda(this, [this](FLinearColor Color)
		{
			if (UCombatSubsystem* Picked = Subsystem.Get())
			{
				Picked->SetDesignPieceColor(Color.ToFColor(true));
			}
		}));
	Args.bUseAlpha = false;
	Args.ParentWidget = AsShared();
	OpenColorPicker(Args);
	return FReply::Handled();
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

FReply SCombatLevelDesigner::OnDeleteClicked()
{
	UCombatSubsystem* CombatSubsystem = Subsystem.Get();
	if (!CombatSubsystem || !SelectedLevel)
	{
		return FReply::Handled();
	}
	if (!IsConfirmingDelete())
	{
		DeleteConfirmUntil = FPlatformTime::Seconds() + CombatLevelDesigner::DeleteConfirmSeconds;
		Message = FString::Printf(TEXT("Click Delete again within 3 s to delete %s."), **SelectedLevel);
		return FReply::Handled();
	}

	DeleteConfirmUntil = 0.0;
	CombatSubsystem->DeleteLevelFile(*SelectedLevel, Message);
	RefreshLevelOptions();
	return FReply::Handled();
}

bool SCombatLevelDesigner::IsConfirmingDelete() const
{
	return DeleteConfirmUntil > 0.0 && FPlatformTime::Seconds() < DeleteConfirmUntil;
}

void SCombatLevelDesigner::RefreshLevelOptions()
{
	// The first time: the level being edited (the default level at the start of play).
	const UCombatSubsystem* CombatSubsystem = Subsystem.Get();
	const FString Previous = SelectedLevel ? *SelectedLevel : CombatSubsystem ? CombatSubsystem->GetDesignLevel().Name : FString();
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
