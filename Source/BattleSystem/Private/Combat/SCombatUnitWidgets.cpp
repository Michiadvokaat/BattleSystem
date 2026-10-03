// Copyright Epic Games, Inc. All Rights Reserved.

#include "SCombatUnitWidgets.h"
#include "Brushes/SlateColorBrush.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void SCombatHealthBar::Construct(const FArguments& InArgs)
{
	BarStyle = FProgressBarStyle()
		.SetBackgroundImage(FSlateColorBrush(FLinearColor(0.02f, 0.02f, 0.02f, 0.85f)))
		.SetFillImage(FSlateColorBrush(FLinearColor::White))
		.SetMarqueeImage(FSlateColorBrush(FLinearColor::White))
		.SetEnableFillAnimation(false);

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(InArgs._TeamColor)
		.Padding(1.f)
		[
			SNew(SProgressBar)
			.Style(&BarStyle)
			.Percent(this, &SCombatHealthBar::GetPercent)
			.FillColorAndOpacity(this, &SCombatHealthBar::GetFillColor)
		]
	];
}

FSlateColor SCombatHealthBar::GetFillColor() const
{
	const FLinearColor Red(1.f, 0.1f, 0.05f);
	const FLinearColor Yellow(1.f, 0.85f, 0.1f);
	const FLinearColor Green(0.15f, 0.9f, 0.2f);
	return Fraction >= 0.5f
		? FMath::Lerp(Yellow, Green, (Fraction - 0.5f) * 2.f)
		: FMath::Lerp(Red, Yellow, Fraction * 2.f);
}

void SCombatStatusIcons::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SAssignNew(Row, SHorizontalBox)
	];
}

void SCombatStatusIcons::SetIcons(const TArray<FCombatStatusDisplay>& InIcons)
{
	if (InIcons == Icons)
	{
		return;
	}
	Icons = InIcons;

	Row->ClearChildren();
	for (const FCombatStatusDisplay& Icon : Icons)
	{
		Row->AddSlot()
		.AutoWidth()
		.Padding(2.f, 0.f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Icon.Label))
			.ColorAndOpacity(Icon.Color)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
			.ShadowOffset(FVector2D(1.0, 1.0))
			.ShadowColorAndOpacity(FLinearColor::Black)
		];
	}
}
