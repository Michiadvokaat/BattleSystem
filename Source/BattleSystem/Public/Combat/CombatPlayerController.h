// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "CombatPlayerController.generated.h"

/**
 * Mouse input on the arena: left click selects an own unit or picks a Move target, right click cancels.
 * In LevelDesigner edit mode instead: left places with the current tool, right erases; holding paints a stroke.
 * Clicks are turned into points on the grid plane, so they do not depend on collision. Clicks on the HUD
 * panels go to the UI instead.
 */
UCLASS()
class BATTLESYSTEM_API ACombatPlayerController : public APlayerController
{
	GENERATED_BODY()

protected:
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

private:
	void OnLeftClick();
	void OnRightClick();
	void OnLeftReleased() { bPainting = false; }
	void OnRightReleased() { bErasing = false; }

	/** Edit mode: a mouse button is held and painting/erasing follows the cursor. */
	bool bPainting = false;
	bool bErasing = false;

	/** The point on the grid plane under the mouse. */
	bool GetArenaPointUnderMouse(double PlaneHeight, FVector& OutPoint) const;
};
