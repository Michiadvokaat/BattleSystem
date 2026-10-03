// Copyright Epic Games, Inc. All Rights Reserved.

#include "SCombatUnitList.h"
#include "Combat/CombatSettings.h"
#include "Combat/CombatSubsystem.h"
#include "Combat/CombatUnitDefinition.h"
#include "SCombatUnitWidgets.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace CombatUnitList
{
	static const FLinearColor SelectedColor(0.2f, 0.8f, 0.3f);
	static const FLinearColor TargetingColor(1.f, 0.6f, 0.1f);
	static const FLinearColor DeadColor(0.4f, 0.4f, 0.4f);
}

void SCombatUnitList::Construct(const FArguments& InArgs)
{
	Subsystem = InArgs._Subsystem;

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(10.f)
		.Visibility_Lambda([this]() { return Rows.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
		[
			SNew(SBox)
			.MinDesiredWidth(260.f)
			[
				SAssignNew(List, SVerticalBox)
			]
		]
	];
}

void SCombatUnitList::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	const UCombatSubsystem* CombatSubsystem = Subsystem.Get();
	if (!CombatSubsystem)
	{
		return;
	}
	if (CombatSubsystem->GetFightSerial() != BuiltSerial)
	{
		Rebuild();
	}

	const FCombatSimulation* Simulation = CombatSubsystem->GetSimulation();
	if (!Simulation)
	{
		return;
	}
	for (const FRow& Row : Rows)
	{
		const FCombatUnit& Unit = Simulation->GetUnits()[Row.UnitId];
		Row.HealthBar->SetFraction(Unit.Stats.MaxHP > 0.f ? Unit.HP / Unit.Stats.MaxHP : 0.f);
		Row.StatusIcons->SetIcons(UCombatSubsystem::GetStatusDisplays(Unit));
	}
}

void SCombatUnitList::Rebuild()
{
	const UCombatSubsystem* CombatSubsystem = Subsystem.Get();
	BuiltSerial = CombatSubsystem ? CombatSubsystem->GetFightSerial() : INDEX_NONE;
	Rows.Reset();
	List->ClearChildren();

	const FCombatSimulation* Simulation = CombatSubsystem ? CombatSubsystem->GetSimulation() : nullptr;
	if (!Simulation)
	{
		return;
	}

	List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
	[
		SNew(STextBlock)
		.Text(FText::FromString(FString::Printf(TEXT("Units (team %d)"), CombatSubsystem->GetPlayerTeam())))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
	];

	// Names: the definition's DisplayName, numbered when a type appears more than once.
	TArray<FString> Names;
	TArray<int32> UnitIds;
	for (const FCombatUnit& Unit : Simulation->GetUnits())
	{
		if (Unit.Team != CombatSubsystem->GetPlayerTeam())
		{
			continue;
		}
		const UCombatUnitDefinition* Definition = CombatSubsystem->GetUnitDefinition(Unit.Id);
		FString Name = Definition && !Definition->DisplayName.IsEmpty() ? Definition->DisplayName.ToString()
			: Definition ? Definition->GetName().Replace(TEXT("DA_"), TEXT("")) : FString::Printf(TEXT("Unit %d"), Unit.Id);
		Names.Add(Name);
		UnitIds.Add(Unit.Id);
	}
	for (int32 Index = 0; Index < Names.Num(); ++Index)
	{
		const FString BaseName = Names[Index];
		int32 Count = 0;
		int32 Number = 0;
		for (int32 Other = 0; Other < Names.Num(); ++Other)
		{
			Count += Names[Other] == BaseName ? 1 : 0;
			Number += Other <= Index && Names[Other] == BaseName ? 1 : 0;
		}
		List->AddSlot().AutoHeight().Padding(0.f, 2.f)
		[
			MakeRow(UnitIds[Index], Count > 1 ? FString::Printf(TEXT("%s %d"), *BaseName, Number) : BaseName)
		];
	}
}

TSharedRef<SWidget> SCombatUnitList::MakeRow(int32 UnitId, const FString& Name)
{
	UCombatSubsystem* CombatSubsystem = Subsystem.Get();
	const FCombatUnit& Unit = CombatSubsystem->GetSimulation()->GetUnits()[UnitId];
	const FLinearColor TeamColor = GetDefault<UCombatSettings>()->GetTeamColor(Unit.Team);

	FRow& Row = Rows.AddDefaulted_GetRef();
	Row.UnitId = UnitId;
	Row.HealthBar = SNew(SCombatHealthBar).TeamColor(TeamColor);
	Row.StatusIcons = SNew(SCombatStatusIcons);

	// Actions: Move (then a click in the arena), and one button per player ability.
	TSharedRef<SWrapBox> Actions = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4.0, 4.0));
	Actions->AddSlot()
	[
		SNew(SButton)
		.ButtonColorAndOpacity_Lambda([this, UnitId]()
		{
			const UCombatSubsystem* Current = Subsystem.Get();
			const bool bTargeting = Current && Current->IsAwaitingMoveTarget() && Current->GetSelectedUnitId() == UnitId;
			return FSlateColor(bTargeting ? CombatUnitList::TargetingColor : FLinearColor::White);
		})
		.OnClicked_Lambda([this, UnitId]()
		{
			if (UCombatSubsystem* Current = Subsystem.Get())
			{
				Current->SelectUnit(UnitId);
				Current->BeginMoveTargeting();
			}
			return FReply::Handled();
		})
		[
			SNew(STextBlock).Text_Lambda([this, UnitId]()
			{
				const UCombatSubsystem* Current = Subsystem.Get();
				const bool bTargeting = Current && Current->IsAwaitingMoveTarget() && Current->GetSelectedUnitId() == UnitId;
				return bTargeting ? INVTEXT("Move: click the arena") : INVTEXT("Move");
			})
		]
	];
	for (int32 AbilityIndex = 0; AbilityIndex < Unit.Stats.PlayerAbilities.Num(); ++AbilityIndex)
	{
		Actions->AddSlot()
		[
			SNew(SButton)
			.OnClicked_Lambda([this, UnitId, AbilityIndex]()
			{
				if (UCombatSubsystem* Current = Subsystem.Get())
				{
					FCombatCommand Command;
					Command.Type = ECombatCommandType::Ability;
					Command.UnitId = UnitId;
					Command.AbilityIndex = AbilityIndex;
					Current->IssueCommand(Command);
				}
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Text(CombatSubsystem->GetAbilityName(UnitId, AbilityIndex))
			]
		];
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SButton)
			.IsEnabled_Lambda([this, UnitId]() { return IsAlive(UnitId); })
			.ButtonColorAndOpacity_Lambda([this, UnitId]()
			{
				return FSlateColor(IsSelected(UnitId) ? CombatUnitList::SelectedColor : FLinearColor::White);
			})
			.OnClicked_Lambda([this, UnitId]()
			{
				if (UCombatSubsystem* Current = Subsystem.Get())
				{
					Current->SelectUnit(IsSelected(UnitId) ? INDEX_NONE : UnitId);
				}
				return FReply::Handled();
			})
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Name))
					.ColorAndOpacity_Lambda([this, UnitId]()
					{
						return FSlateColor(IsAlive(UnitId) ? FLinearColor::White : CombatUnitList::DeadColor);
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f)
				[
					SNew(SBox).WidthOverride(60.f).HeightOverride(8.f)[ Row.HealthBar.ToSharedRef() ]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox).MinDesiredWidth(40.f)[ Row.StatusIcons.ToSharedRef() ]
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(6.f, 1.f)
		[
			SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
			.ColorAndOpacity(FLinearColor(0.8f, 0.8f, 0.8f))
			.Text_Lambda([this, UnitId]()
			{
				const UCombatSubsystem* Current = Subsystem.Get();
				return FText::FromString(Current ? Current->GetOrderText(UnitId) : FString());
			})
			.Visibility_Lambda([this, UnitId]()
			{
				const UCombatSubsystem* Current = Subsystem.Get();
				return Current && !Current->GetOrderText(UnitId).IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed;
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f, 0.f, 4.f)
		[
			SNew(SBox)
			.Visibility_Lambda([this, UnitId]()
			{
				return IsSelected(UnitId) && IsAlive(UnitId) ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				Actions
			]
		];
}

bool SCombatUnitList::IsAlive(int32 UnitId) const
{
	const UCombatSubsystem* CombatSubsystem = Subsystem.Get();
	const FCombatSimulation* Simulation = CombatSubsystem ? CombatSubsystem->GetSimulation() : nullptr;
	return Simulation && Simulation->GetUnits().IsValidIndex(UnitId) && Simulation->GetUnits()[UnitId].bAlive;
}

bool SCombatUnitList::IsSelected(int32 UnitId) const
{
	const UCombatSubsystem* CombatSubsystem = Subsystem.Get();
	return CombatSubsystem && CombatSubsystem->GetSelectedUnitId() == UnitId;
}
