// Copyright Epic Games, Inc. All Rights Reserved.

#include "SCombatSpawnList.h"
#include "Combat/CombatLevel.h"
#include "Combat/CombatSubsystem.h"
#include "Combat/CombatUnitData.h"
#include "Algo/StableSort.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace CombatSpawnList
{
	static const FLinearColor SelectedColor(0.2f, 0.8f, 0.3f);
	static const FLinearColor MovingColor(1.f, 0.6f, 0.1f);
	static constexpr float TypeWidth = 140.f;
	static constexpr float NumberWidth = 48.f;
	static constexpr float TimeWidth = 60.f;
	static constexpr float RotationWidth = 64.f;
}

void SCombatSpawnList::Construct(const FArguments& InArgs)
{
	Subsystem = InArgs._Subsystem;
	for (const FString& Type : CombatUnits::GetAllTypeNames())
	{
		UnitTypeOptions.Add(MakeShared<FString>(Type));
	}

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(10.f)
		// Only in Spawn Mode; it stays built in the other modes.
		.Visibility_Lambda([this]()
		{
			const UCombatSubsystem* Current = Subsystem.Get();
			return IsEditing() && Current->GetDesignTool() == ECombatDesignTool::Spawn ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
			[
				SNew(STextBlock).Text(INVTEXT("Spawns")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[ MakeColumnLabel(INVTEXT("Type"), CombatSpawnList::TypeWidth) ]
				+ SHorizontalBox::Slot().AutoWidth()[ MakeColumnLabel(INVTEXT("Wave"), CombatSpawnList::NumberWidth) ]
				+ SHorizontalBox::Slot().AutoWidth()[ MakeColumnLabel(INVTEXT("X"), CombatSpawnList::NumberWidth) ]
				+ SHorizontalBox::Slot().AutoWidth()[ MakeColumnLabel(INVTEXT("Y"), CombatSpawnList::NumberWidth) ]
				+ SHorizontalBox::Slot().AutoWidth()[ MakeColumnLabel(INVTEXT("Pos"), CombatSpawnList::NumberWidth) ]
				+ SHorizontalBox::Slot().AutoWidth()[ MakeColumnLabel(INVTEXT("Rot"), CombatSpawnList::RotationWidth) ]
				+ SHorizontalBox::Slot().AutoWidth()[ MakeColumnLabel(INVTEXT("Time (s)"), CombatSpawnList::TimeWidth) ]
			]

			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox)
				.MaxDesiredHeight(420.f)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SAssignNew(List, SVerticalBox)
					]
				]
			]
		]
	];
}

void SCombatSpawnList::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	const UCombatSubsystem* CombatSubsystem = Subsystem.Get();
	const bool bEditing = IsEditing();
	if (CombatSubsystem && (CombatSubsystem->GetDesignRevision() != BuiltRevision || bEditing != bBuiltForEditing))
	{
		Rebuild();
	}
}

bool SCombatSpawnList::IsEditing() const
{
	const UCombatSubsystem* CombatSubsystem = Subsystem.Get();
	return CombatSubsystem && CombatSubsystem->IsDesignMode();
}

void SCombatSpawnList::Rebuild()
{
	const UCombatSubsystem* CombatSubsystem = Subsystem.Get();
	BuiltRevision = CombatSubsystem ? CombatSubsystem->GetDesignRevision() : INDEX_NONE;
	bBuiltForEditing = IsEditing();
	List->ClearChildren();
	if (!CombatSubsystem || !bBuiltForEditing)
	{
		return;
	}

	// Enemies placed with the Unit tool stand on the field from the start; shown so they are not mistaken for spawns.
	const FCombatLevel& Level = CombatSubsystem->GetDesignLevel();
	TArray<int32> StartEnemies;
	for (int32 Index = 0; Index < Level.Units.Num(); ++Index)
	{
		if (Level.Units[Index].Team != CombatSubsystem->GetPlayerTeam())
		{
			StartEnemies.Add(Index);
		}
	}
	if (!StartEnemies.IsEmpty())
	{
		List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(TEXT("Start enemies (%d)"), StartEnemies.Num())))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		];
		for (const int32 UnitIndex : StartEnemies)
		{
			List->AddSlot().AutoHeight().Padding(0.f, 1.f)[ MakeStartUnitRow(UnitIndex) ];
		}
	}

	const TArray<FCombatLevelWave>& Waves = Level.Waves;
	if (Waves.IsEmpty())
	{
		List->AddSlot().AutoHeight().Padding(0.f, StartEnemies.IsEmpty() ? 0.f : 6.f, 0.f, 0.f)
		[
			MakeHint(INVTEXT("No waves yet: use the Spawn tool or + in the Wave row."))
		];
		return;
	}

	for (int32 WaveIndex = 0; WaveIndex < Waves.Num(); ++WaveIndex)
	{
		const TArray<FCombatLevelSpawn>& Spawns = Waves[WaveIndex].Spawns;
		List->AddSlot().AutoHeight().Padding(0.f, WaveIndex > 0 || !StartEnemies.IsEmpty() ? 6.f : 0.f, 0.f, 2.f)[ MakeWaveHeader(WaveIndex, Spawns.Num()) ];
		if (Spawns.IsEmpty())
		{
			List->AddSlot().AutoHeight().Padding(8.f, 1.f, 0.f, 1.f)[ MakeHint(INVTEXT("(empty: use the Spawn tool)")) ];
		}

		// By time; equal times keep the placement order (the order in the file, which stays as it is).
		TArray<int32> Order;
		for (int32 Index = 0; Index < Spawns.Num(); ++Index)
		{
			Order.Add(Index);
		}
		Algo::StableSortBy(Order, [&Spawns](int32 Index) { return Spawns[Index].Time; });
		for (const int32 SpawnIndex : Order)
		{
			List->AddSlot().AutoHeight().Padding(0.f, 1.f)[ MakeSpawnRow(WaveIndex, SpawnIndex) ];
		}
	}
}

TSharedRef<SWidget> SCombatSpawnList::MakeWaveHeader(int32 WaveIndex, int32 SpawnCount)
{
	// Clicking the header selects the wave (its spawns are shown in the arena).
	return SNew(SButton)
		.OnClicked_Lambda([this, WaveIndex]()
		{
			if (UCombatSubsystem* CombatSubsystem = Subsystem.Get())
			{
				CombatSubsystem->SetDesignWave(WaveIndex);
			}
			return FReply::Handled();
		})
		.ButtonColorAndOpacity_Lambda([this, WaveIndex]()
		{
			const UCombatSubsystem* CombatSubsystem = Subsystem.Get();
			const bool bSelected = CombatSubsystem && CombatSubsystem->GetDesignWave() == WaveIndex;
			return FSlateColor(bSelected ? CombatSpawnList::SelectedColor : FLinearColor::White);
		})
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(TEXT("Wave %d (%d)"), WaveIndex + 1, SpawnCount)))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		];
}

TSharedRef<SWidget> SCombatSpawnList::MakeSpawnRow(int32 WaveIndex, int32 SpawnIndex)
{
	const UCombatSubsystem* CombatSubsystem = Subsystem.Get();
	const FCombatLevel& Level = CombatSubsystem->GetDesignLevel();
	const FCombatLevelSpawn Spawn = Level.Waves[WaveIndex].Spawns[SpawnIndex];

	// Every edit starts from the spawn as it is now and changes one field.
	auto Edit = [this, WaveIndex, SpawnIndex](TFunction<void(FCombatLevelSpawn&)> Change)
	{
		UCombatSubsystem* Current = Subsystem.Get();
		if (!Current || !Current->GetDesignLevel().Waves.IsValidIndex(WaveIndex)
			|| !Current->GetDesignLevel().Waves[WaveIndex].Spawns.IsValidIndex(SpawnIndex))
		{
			return;
		}
		FCombatLevelSpawn Changed = Current->GetDesignLevel().Waves[WaveIndex].Spawns[SpawnIndex];
		Change(Changed);
		Current->SetDesignSpawn(WaveIndex, SpawnIndex, Changed);
	};

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			MakeTypeBox(Spawn.Type, [Edit](const FString& Type) { Edit([Type](FCombatLevelSpawn& Changed) { Changed.Type = Type; }); })
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			MakeIntBox(WaveIndex + 1, 1, Level.Waves.Num(), [this, WaveIndex, SpawnIndex](int32 Wave)
			{
				if (UCombatSubsystem* Current = Subsystem.Get())
				{
					Current->MoveDesignSpawnToWave(WaveIndex, SpawnIndex, Wave - 1);
				}
			})
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			MakeIntBox(Spawn.Cell.X, 0, Level.Width - 1, [Edit](int32 X) { Edit([X](FCombatLevelSpawn& Changed) { Changed.Cell.X = X; }); })
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			MakeIntBox(Spawn.Cell.Y, 0, Level.Height - 1, [Edit](int32 Y) { Edit([Y](FCombatLevelSpawn& Changed) { Changed.Cell.Y = Y; }); })
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			MakeIntBox(Spawn.Position, 0, 8, [Edit](int32 Position) { Edit([Position](FCombatLevelSpawn& Changed) { Changed.Position = Position; }); })
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			MakeRotationBox(Spawn.Rotation, [Edit](int32 Rotation) { Edit([Rotation](FCombatLevelSpawn& Changed) { Changed.Rotation = Rotation; }); })
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(SBox)
			.WidthOverride(CombatSpawnList::TimeWidth - 4.f)
			[
				SNew(SSpinBox<float>)
				.MinValue(0.f)
				.MaxValue(600.f)
				.Delta(0.5f)
				.Value(Spawn.Time)
				.OnValueCommitted_Lambda([Edit](float Time, ETextCommit::Type)
				{
					Edit([Time](FCombatLevelSpawn& Changed) { Changed.Time = Time; });
				})
			]
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			MakeRowButtons(
				[this, WaveIndex, SpawnIndex]() { if (UCombatSubsystem* Current = Subsystem.Get()) { Current->BeginDesignSpawnMove(WaveIndex, SpawnIndex); } },
				[this, WaveIndex, SpawnIndex]() { const UCombatSubsystem* Current = Subsystem.Get(); return Current && Current->IsMovingDesignSpawn(WaveIndex, SpawnIndex); },
				[this, WaveIndex, SpawnIndex]() { if (UCombatSubsystem* Current = Subsystem.Get()) { Current->RemoveDesignSpawn(WaveIndex, SpawnIndex); } })
		];
}

TSharedRef<SWidget> SCombatSpawnList::MakeStartUnitRow(int32 UnitIndex)
{
	const UCombatSubsystem* CombatSubsystem = Subsystem.Get();
	const FCombatLevel& Level = CombatSubsystem->GetDesignLevel();
	const FCombatLevelUnit Unit = Level.Units[UnitIndex];

	auto Edit = [this, UnitIndex](TFunction<void(FCombatLevelUnit&)> Change)
	{
		UCombatSubsystem* Current = Subsystem.Get();
		if (!Current || !Current->GetDesignLevel().Units.IsValidIndex(UnitIndex))
		{
			return;
		}
		FCombatLevelUnit Changed = Current->GetDesignLevel().Units[UnitIndex];
		Change(Changed);
		Current->SetDesignUnit(UnitIndex, Changed);
	};

	// No wave and no time: these stand on the field from the start.
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			MakeTypeBox(Unit.Type, [Edit](const FString& Type) { Edit([Type](FCombatLevelUnit& Changed) { Changed.Type = Type; }); })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(CombatSpawnList::NumberWidth)[ MakeHint(INVTEXT("start")) ]
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			MakeIntBox(Unit.Cell.X, 0, Level.Width - 1, [Edit](int32 X) { Edit([X](FCombatLevelUnit& Changed) { Changed.Cell.X = X; }); })
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			MakeIntBox(Unit.Cell.Y, 0, Level.Height - 1, [Edit](int32 Y) { Edit([Y](FCombatLevelUnit& Changed) { Changed.Cell.Y = Y; }); })
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			MakeIntBox(Unit.Position, 0, 8, [Edit](int32 Position) { Edit([Position](FCombatLevelUnit& Changed) { Changed.Position = Position; }); })
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			MakeRotationBox(Unit.Rotation, [Edit](int32 Rotation) { Edit([Rotation](FCombatLevelUnit& Changed) { Changed.Rotation = Rotation; }); })
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox).WidthOverride(CombatSpawnList::TimeWidth)
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			MakeRowButtons(
				[this, UnitIndex]() { if (UCombatSubsystem* Current = Subsystem.Get()) { Current->BeginDesignUnitMove(UnitIndex); } },
				[this, UnitIndex]() { const UCombatSubsystem* Current = Subsystem.Get(); return Current && Current->IsMovingDesignUnit(UnitIndex); },
				[this, UnitIndex]() { if (UCombatSubsystem* Current = Subsystem.Get()) { Current->RemoveDesignUnit(UnitIndex); } })
		];
}

TSharedRef<SWidget> SCombatSpawnList::MakeTypeBox(const FString& Type, TFunction<void(const FString&)> OnChanged)
{
	const TSharedPtr<FString>* SelectedType = UnitTypeOptions.FindByPredicate([&Type](const TSharedPtr<FString>& Option) { return *Option == Type; });
	return SNew(SBox)
		.WidthOverride(CombatSpawnList::TypeWidth)
		.Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(SComboBox<TSharedPtr<FString>>)
			.OptionsSource(&UnitTypeOptions)
			.InitiallySelectedItem(SelectedType ? *SelectedType : nullptr)
			.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) { return SNew(STextBlock).Text(FText::FromString(*Item)); })
			.OnSelectionChanged_Lambda([OnChanged](TSharedPtr<FString> Item, ESelectInfo::Type SelectInfo)
			{
				if (Item && SelectInfo != ESelectInfo::Direct)
				{
					OnChanged(*Item);
				}
			})
			[
				SNew(STextBlock).Text(FText::FromString(Type))
			]
		];
}

TSharedRef<SWidget> SCombatSpawnList::MakeRowButtons(TFunction<void()> OnMove, TFunction<bool()> IsMoving, TFunction<void()> OnRemove)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(SButton)
			.ToolTipText(INVTEXT("Then click a cell in the arena (right click cancels)."))
			.OnClicked_Lambda([OnMove]() { OnMove(); return FReply::Handled(); })
			.ButtonColorAndOpacity_Lambda([IsMoving]() { return FSlateColor(IsMoving() ? CombatSpawnList::MovingColor : FLinearColor::White); })
			[
				SNew(STextBlock).Text(INVTEXT("Move"))
			]
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton)
			.ToolTipText(INVTEXT("Remove"))
			.OnClicked_Lambda([OnRemove]() { OnRemove(); return FReply::Handled(); })
			[
				SNew(STextBlock).Text(INVTEXT("x"))
			]
		];
}

TSharedRef<SWidget> SCombatSpawnList::MakeHint(const FText& Text)
{
	return SNew(STextBlock).Text(Text).ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f)).Font(FCoreStyle::GetDefaultFontStyle("Italic", 9));
}

TSharedRef<SWidget> SCombatSpawnList::MakeIntBox(int32 Value, int32 Min, int32 Max, TFunction<void(int32)> OnCommitted)
{
	// Committed only (Enter, focus loss, end of a drag): every commit rebuilds the list.
	return SNew(SBox)
		.WidthOverride(CombatSpawnList::NumberWidth)
		.Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(SSpinBox<int32>)
			.MinValue(Min)
			.MaxValue(FMath::Max(Min, Max))
			.Delta(1)
			.Value(Value)
			.OnValueCommitted_Lambda([OnCommitted, Value](int32 NewValue, ETextCommit::Type)
			{
				if (NewValue != Value)
				{
					OnCommitted(NewValue);
				}
			})
		];
}

TSharedRef<SWidget> SCombatSpawnList::MakeRotationBox(int32 Rotation, TFunction<void(int32)> OnCommitted)
{
	const float StepDegrees = CombatLevels::GetUnitYaw(1);
	return SNew(SBox)
		.WidthOverride(CombatSpawnList::RotationWidth)
		.Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(SSpinBox<float>)
			.MinValue(0.f)
			.MaxValue(360.f - StepDegrees)
			.Delta(StepDegrees)
			.Value(CombatLevels::GetUnitYaw(Rotation))
			.OnValueCommitted_Lambda([OnCommitted, Rotation, StepDegrees](float Degrees, ETextCommit::Type)
			{
				const int32 NewRotation = FMath::RoundToInt32(Degrees / StepDegrees) % CombatLevels::UnitRotationSteps;
				if (NewRotation != Rotation)
				{
					OnCommitted(NewRotation);
				}
			})
		];
}

TSharedRef<SWidget> SCombatSpawnList::MakeColumnLabel(const FText& Label, float Width)
{
	return SNew(SBox)
		.WidthOverride(Width)
		[
			SNew(STextBlock).Text(Label).ColorAndOpacity(FLinearColor(0.75f, 0.75f, 0.75f))
		];
}
