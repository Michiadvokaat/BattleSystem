// Copyright Epic Games, Inc. All Rights Reserved.

#include "SCombatLevelDesigner.h"
#include "Combat/CombatLevel.h"
#include "Combat/CombatPieces.h"
#include "Combat/CombatSettings.h"
#include "Combat/CombatSubsystem.h"
#include "HAL/PlatformTime.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
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
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ MakeLabel(INVTEXT("Tool")) ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)[ ToolButton(INVTEXT("Wall"), ECombatDesignTool::Wall) ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)[ ToolButton(INVTEXT("Hedge"), ECombatDesignTool::Hedge) ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)[ ToolButton(INVTEXT("Water"), ECombatDesignTool::Water) ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)[ ToolButton(INVTEXT("Unit"), ECombatDesignTool::Unit) ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)[ ToolButton(INVTEXT("Spawn"), ECombatDesignTool::Spawn) ]
				+ SHorizontalBox::Slot().AutoWidth()[ ToolButton(INVTEXT("Piece"), ECombatDesignTool::Piece) ]
			]

			// Waves: select, add after the selected one, remove the selected one. The arena shows the selected wave's spawns.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
			[
				SNew(SHorizontalBox)
				.Visibility(this, &SCombatLevelDesigner::GetEditVisibility)
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
					const UCombatSubsystem* Current = Subsystem.Get();
					const bool bPlacesUnits = Current && (Current->GetDesignTool() == ECombatDesignTool::Unit || Current->GetDesignTool() == ECombatDesignTool::Spawn);
					return IsEditing() && bPlacesUnits ? EVisibility::Visible : EVisibility::Collapsed;
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
				+ SHorizontalBox::Slot().AutoWidth()
				[
					// Unit: the team. Spawn: the time after the wave start (spawns are always the wave team).
					SNew(SHorizontalBox)
					.Visibility_Lambda([this]()
					{
						const UCombatSubsystem* Current = Subsystem.Get();
						return Current && Current->GetDesignTool() == ECombatDesignTool::Unit ? EVisibility::Visible : EVisibility::Collapsed;
					})
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)[ TeamButton(0) ]
					+ SHorizontalBox::Slot().AutoWidth()[ TeamButton(1) ]
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SHorizontalBox)
					.Visibility_Lambda([this]()
					{
						const UCombatSubsystem* Current = Subsystem.Get();
						return Current && Current->GetDesignTool() == ECombatDesignTool::Spawn ? EVisibility::Visible : EVisibility::Collapsed;
					})
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

			// Piece tool: categories, the pieces of the selected one, and the rotation.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
			[
				SNew(SVerticalBox)
				.Visibility_Lambda([this]() { return IsEditing() && IsPieceTool() ? EVisibility::Visible : EVisibility::Collapsed; })
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
				[
					SAssignNew(CategoryBox, SWrapBox).PreferredSize(CombatLevelDesigner::PaletteWidth)
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
							return FText::FromString(FString::Printf(TEXT("%d\u00B0"), Current ? Current->GetDesignPieceDegrees() : 0));
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
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Visibility(this, &SCombatLevelDesigner::GetEditVisibility)
				.Text_Lambda([this]()
				{
					return IsPieceTool()
						? INVTEXT("Left: place   Shift+Left: erase (this layer)   R / Shift+R: rotate   Right drag: look")
						: INVTEXT("Left: place (hold to paint)   Shift+Left: erase   Right click: erase cell   Right drag: look");
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

bool SCombatLevelDesigner::IsPieceTool() const
{
	const UCombatSubsystem* Current = Subsystem.Get();
	return Current && Current->GetDesignTool() == ECombatDesignTool::Piece;
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

	// Categories in catalog order; the selected piece (or the first one) picks the category shown first.
	TArray<FString> Categories;
	for (const FCombatPieceDefinition& Definition : Catalog->Pieces)
	{
		Categories.AddUnique(Definition.Category);
	}
	const FCombatPieceDefinition* Selected = Catalog->Find(Current->GetDesignPiece());
	if (!Selected)
	{
		Selected = &Catalog->Pieces[0];
		Current->SetDesignPiece(Selected->Id);
	}
	SelectedCategory = Selected->Category;

	for (const FString& Category : Categories)
	{
		CategoryBox->AddSlot().Padding(0.f, 0.f, 4.f, 4.f)
		[
			MakeButton(FText::FromString(Category),
				[this, Category]()
				{
					SelectedCategory = Category;
					RebuildPalette();
				},
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
	for (const FCombatPieceDefinition& Definition : Catalog->Pieces)
	{
		if (Definition.Category == SelectedCategory)
		{
			PaletteBox->AddSlot().Padding(2.f)[ MakePieceButton(Definition) ];
		}
	}
}

TSharedRef<SWidget> SCombatLevelDesigner::MakePieceButton(const FCombatPieceDefinition& Definition)
{
	const FString Id = Definition.Id;
	FString Name = Id;
	Id.Split(TEXT("/"), nullptr, &Name);
	Name.RemoveFromStart(TEXT("SM_"));

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
				Current->SetDesignTool(ECombatDesignTool::Piece);
				Current->SetDesignPiece(Id);
			}
			return FReply::Handled();
		})
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ Picture ]
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
