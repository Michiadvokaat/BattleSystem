// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;
class UCombatSubsystem;

/**
 * LevelDesigner spawn list (bottom right, edit mode only): the enemies that stand on the field from the start
 * (start units not on the player's team), then every wave with its spawns sorted by time. Each row has type,
 * wave, X, Y and time (spawns) to edit, a Move button (then a click in the arena) and a remove button.
 * Rebuilt whenever the edited level changes (UCombatSubsystem::GetDesignRevision).
 */
class SCombatSpawnList : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCombatSpawnList) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UCombatSubsystem>, Subsystem)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	void Rebuild();
	TSharedRef<SWidget> MakeWaveHeader(int32 WaveIndex, int32 SpawnCount);
	TSharedRef<SWidget> MakeSpawnRow(int32 WaveIndex, int32 SpawnIndex);
	TSharedRef<SWidget> MakeStartUnitRow(int32 UnitIndex);
	TSharedRef<SWidget> MakeTypeBox(const FString& Type, TFunction<void(const FString&)> OnChanged);
	TSharedRef<SWidget> MakeRowButtons(TFunction<void()> OnMove, TFunction<bool()> IsMoving, TFunction<void()> OnRemove);
	static TSharedRef<SWidget> MakeHint(const FText& Text);
	TSharedRef<SWidget> MakeIntBox(int32 Value, int32 Min, int32 Max, TFunction<void(int32)> OnCommitted);
	/** A start rotation (1/32 turns) as degrees in steps of 11.25; commits the rotation in steps. */
	TSharedRef<SWidget> MakeRotationBox(int32 Rotation, TFunction<void(int32)> OnCommitted);
	static TSharedRef<SWidget> MakeColumnLabel(const FText& Label, float Width);

	bool IsEditing() const;

	TWeakObjectPtr<UCombatSubsystem> Subsystem;
	TSharedPtr<SVerticalBox> List;
	TArray<TSharedPtr<FString>> UnitTypeOptions;
	int32 BuiltRevision = INDEX_NONE;
	bool bBuiltForEditing = false;
};
