// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/CombatUnitActor.h"
#include "CombatLookEditor.generated.h"

/**
 * Edits a unit's look with a live preview: make a Blueprint of it (Tools/BP_LookEditor), pick a Unit Row and Load From Row;
 * every change to Look in Class Defaults (parts, colours, scale, props, ...) shows at once in the Blueprint's viewport, the
 * figure in its idle animation, turned by Preview Yaw. Save To Row writes the look into the unit table and exports the table
 * to Data/Units.json. Also works placed in a level. Presentation only: the figure is built in the construction script and
 * is not a fighting unit.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Combat Look Editor"))
class BATTLESYSTEM_API ACombatLookEditor : public ACombatUnitActor
{
	GENERATED_BODY()

public:
	ACombatLookEditor();

	/** The row of the unit table that Load and Save use. */
	UPROPERTY(EditAnywhere, Category = "Look Editor", meta = (GetOptions = "BattleSystem.CombatSettings.GetUnitRowNames"))
	FName UnitRow;

	/** The look being edited (as in the unit row). */
	UPROPERTY(EditAnywhere, Category = "Look Editor", meta = (DisplayName = "Look"))
	FCombatLook EditedLook;

	/** Turns the figure, to look at it from all sides. */
	UPROPERTY(EditAnywhere, Category = "Look Editor", meta = (ClampMin = -180, ClampMax = 180, Units = "deg"))
	float PreviewYaw = 0.f;

	/** Seed of the look's random picks (a slot with several options, EmptyChance). */
	UPROPERTY(EditAnywhere, Category = "Look Editor")
	int32 Seed = 0;

	/** Copies the look of Unit Row into Look. */
	UFUNCTION(CallInEditor, Category = "Look Editor")
	void LoadFromRow();

	/** Writes Look into Unit Row of the unit table, saves the table and exports it to Data/Units.json. */
	UFUNCTION(CallInEditor, Category = "Look Editor")
	void SaveToRow();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	/**
	 * After Load on the Blueprint's class defaults (the buttons there act on them): passes the look on to the Blueprint's
	 * instances (its viewport preview, placed actors) and builds them again, and marks the Blueprint modified.
	 */
	void RefreshAfterLoad();
};
