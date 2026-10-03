// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/CombatUnitActor.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SCompoundWidget.h"

class SHorizontalBox;

/** Health bar above a unit: a fill from green (full) via yellow to red (nearly dead), with a team-colored rim. */
class SCombatHealthBar : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCombatHealthBar) {}
		SLATE_ARGUMENT(FLinearColor, TeamColor)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** 0..1 */
	void SetFraction(float InFraction) { Fraction = FMath::Clamp(InFraction, 0.f, 1.f); }

private:
	TOptional<float> GetPercent() const { return Fraction; }
	FSlateColor GetFillColor() const;

	float Fraction = 1.f;

	/** Plain white fill (tinted by GetFillColor) on a dark background; the default style's fill has its own tint. */
	FProgressBarStyle BarStyle;
};

/** A row of short colored labels for a unit's active effects. */
class SCombatStatusIcons : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCombatStatusIcons) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Rebuilds the row only when the icons changed. */
	void SetIcons(const TArray<FCombatStatusDisplay>& InIcons);

private:
	TSharedPtr<SHorizontalBox> Row;
	TArray<FCombatStatusDisplay> Icons;
};
