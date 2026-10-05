// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "CombatPlayerController.generated.h"

/** What a held mouse button does to the camera. */
enum class ECombatCameraDrag : uint8
{
	None,
	/** Left button down outside edit mode: a release is a click (select, Move target), the right button makes it PanScreen. */
	PendingClick,
	/** Right button down, not moved past CameraDragThreshold yet: a release now is a click (cancel, or erase in edit mode). */
	PendingLook,
	/** Right drag: look around; WASD/QE fly. */
	Look,
	/** Middle drag: the ground under the cursor follows the cursor. */
	Pan,
	/** Alt + left drag: turn around the ground point where the drag began. */
	Orbit,
	/** Alt + right drag: move forward and back. */
	Dolly,
	/** Left + right drag: sideways along the camera's right, up and down along world Z. */
	PanScreen,
};

/**
 * Mouse input on the arena: left click selects an own unit or picks a Move target, right click cancels.
 * In LevelDesigner edit mode instead: left places with the current tool and Shift + left erases (holding paints or
 * erases a stroke); a right click erases one cell. With the Piece tool the selected piece follows the cursor as a
 * preview (R / Shift+R turn it) and erasing removes pieces of its layer.
 * Clicks are turned into points on the grid plane, so they do not depend on collision. Clicks on the HUD
 * panels go to the UI instead.
 * The camera moves like the editor viewport: right drag looks (+ WASD/QE), middle drag pans, left + right drag moves
 * sideways and up/down, the wheel zooms to the cursor, Alt + left drag orbits, Alt + right drag dollies, F returns to
 * the overview; the same in edit mode. Outside edit mode a left click acts on release, so pressing the right button
 * too never selects.
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
	void OnMiddleClick();
	void OnLeftReleased() { bPainting = false; }
	void OnRightReleased() { bErasing = false; }
	void OnWheelUp() { OnWheel(1.0); }
	void OnWheelDown() { OnWheel(-1.0); }
	void OnWheel(double Steps);
	void OnResetCamera();
	/** LevelDesigner Piece tool: R turns the piece a quarter clockwise, Shift+R back. */
	void OnRotatePiece();

	/** Edit mode: a mouse button is held and painting/erasing follows the cursor. */
	bool bPainting = false;
	bool bErasing = false;

	/** The point on the grid plane under the mouse. */
	bool GetArenaPointUnderMouse(double PlaneHeight, FVector& OutPoint) const;
	/** The point on the grid plane under a screen position. */
	bool GetArenaPointAt(const FVector2D& ScreenPosition, double PlaneHeight, FVector& OutPoint) const;

	void BeginCameraDrag(ECombatCameraDrag Drag, const FKey& Key);
	/** Both buttons are down: stop painting, erasing or a pending click/look and pan instead. */
	void BeginScreenPan();
	/** Moves the camera for the drag in progress; ends it when its button is up. */
	void UpdateCameraDrag(float DeltaTime);
	/** Positive steps zoom in towards the point under the cursor. */
	void ZoomCamera(double Steps);

	ECombatCameraDrag CameraDrag = ECombatCameraDrag::None;
	FKey CameraDragKey;
	/** Look, orbit and dolly put the cursor back here every frame, so a drag never hits the screen edge. */
	FVector2D DragStartMouse = FVector2D::ZeroVector;
	FVector2D LastMouse = FVector2D::ZeroVector;
	FVector OrbitPivot = FVector::ZeroVector;
	/** PendingClick and PendingLook: the arena point under the cursor when the button went down, if there was one. */
	FVector ClickPoint = FVector::ZeroVector;
	bool bHasClickPoint = false;
	/** Left + right: this drag needs both buttons. */
	FKey CameraDragSecondKey;
	/** Changed with the wheel while flying, like the editor's camera speed. */
	double FlySpeedScale = 1.0;
};
