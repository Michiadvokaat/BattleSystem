// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatPlayerController.h"
#include "Camera/CameraActor.h"
#include "Combat/CombatCamera.h"
#include "Combat/CombatSettings.h"
#include "Combat/CombatSubsystem.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"

namespace CombatPlayerControllerPrivate
{
	/** Distance used for zoom and dolly steps when the cursor or view does not hit the grid plane. */
	constexpr double FallbackGroundDistance = 1000.0;
	/** Wheel factor on the fly speed while looking around, and its limits. */
	constexpr double FlySpeedWheelFactor = 1.25;
	constexpr double MinFlySpeedScale = 0.1;
	constexpr double MaxFlySpeedScale = 10.0;
	/** Dolly: this many pixels of mouse movement move one CameraZoomStep. */
	constexpr double DollyPixelsPerStep = 10.0;

	bool IsAltDown(const APlayerController& Controller)
	{
		return Controller.IsInputKeyDown(EKeys::LeftAlt) || Controller.IsInputKeyDown(EKeys::RightAlt);
	}

	double AxisInput(const APlayerController& Controller, const FKey& Positive, const FKey& Negative)
	{
		return (Controller.IsInputKeyDown(Positive) ? 1.0 : 0.0) - (Controller.IsInputKeyDown(Negative) ? 1.0 : 0.0);
	}
}

void ACombatPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ACombatPlayerController::OnLeftClick);
	InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &ACombatPlayerController::OnRightClick);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &ACombatPlayerController::OnLeftReleased);
	InputComponent->BindKey(EKeys::RightMouseButton, IE_Released, this, &ACombatPlayerController::OnRightReleased);
	InputComponent->BindKey(EKeys::MiddleMouseButton, IE_Pressed, this, &ACombatPlayerController::OnMiddleClick);
	InputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &ACombatPlayerController::OnWheelUp);
	InputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &ACombatPlayerController::OnWheelDown);
	InputComponent->BindKey(EKeys::F, IE_Pressed, this, &ACombatPlayerController::OnResetCamera);
	InputComponent->BindKey(EKeys::V, IE_Pressed, this, &ACombatPlayerController::OnCycleWalls);
	InputComponent->BindKey(EKeys::R, IE_Pressed, this, &ACombatPlayerController::OnRotatePiece);
	InputComponent->BindKey(EKeys::Z, IE_Pressed, this, &ACombatPlayerController::OnUndoKey);
	InputComponent->BindKey(EKeys::Y, IE_Pressed, this, &ACombatPlayerController::OnRedoKey);
}

void ACombatPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	// A release over the HUD never reaches the game, so check the buttons themselves.
	bPainting &= IsInputKeyDown(EKeys::LeftMouseButton);
	bErasing &= IsInputKeyDown(EKeys::LeftMouseButton);

	UCombatSubsystem* Subsystem = GetWorld()->GetSubsystem<UCombatSubsystem>();
	FVector Point;
	if ((bPainting || bErasing) && Subsystem && Subsystem->IsDesignMode() && GetArenaPointUnderMouse(Subsystem->GetGridHeight(), Point))
	{
		Subsystem->DesignPaint(Point, bErasing, true);
	}

	// Piece tool: the selected piece follows the cursor (orange while Shift is held: erasing).
	if (Subsystem && Subsystem->IsDesignMode() && Subsystem->GetDesignTool() == ECombatDesignTool::Build && GetArenaPointUnderMouse(Subsystem->GetGridHeight(), Point))
	{
		Subsystem->UpdateDesignPiecePreview(Point, IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift));
	}
	else if (Subsystem)
	{
		Subsystem->HideDesignPiecePreview();
	}

	UpdateCameraDrag(DeltaTime);
}

void ACombatPlayerController::OnLeftClick()
{
	if (IsInputKeyDown(EKeys::RightMouseButton))
	{
		BeginScreenPan();
		return;
	}
	if (CombatPlayerControllerPrivate::IsAltDown(*this))
	{
		BeginCameraDrag(ECombatCameraDrag::Orbit, EKeys::LeftMouseButton);
		return;
	}

	UCombatSubsystem* Subsystem = GetWorld()->GetSubsystem<UCombatSubsystem>();
	FVector Point;
	if (!Subsystem || !GetArenaPointUnderMouse(Subsystem->GetGridHeight(), Point))
	{
		return;
	}
	// Ctrl: pick up the piece under the cursor to move it, instead of placing.
	if (Subsystem->IsDesignMode() && (IsInputKeyDown(EKeys::LeftControl) || IsInputKeyDown(EKeys::RightControl)))
	{
		Subsystem->PickDesignPiece(Point);
		return;
	}
	if (Subsystem->IsDesignMode())
	{
		// Shift erases. Holding paints or erases a stroke, except when moving a spawn: that takes only this click.
		const bool bErase = IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift);
		const bool bStroke = !Subsystem->IsMovingDesignSpawn();
		bPainting = bStroke && !bErase;
		bErasing = bStroke && bErase;
		Subsystem->DesignPaint(Point, bErase, false);
		return;
	}
	// Acts on release (UpdateCameraDrag), unless the right button joins in for a pan.
	BeginCameraDrag(ECombatCameraDrag::PendingClick, EKeys::LeftMouseButton);
	ClickPoint = Point;
	bHasClickPoint = true;
}

void ACombatPlayerController::OnRightClick()
{
	if (IsInputKeyDown(EKeys::LeftMouseButton))
	{
		BeginScreenPan();
		return;
	}
	if (CombatPlayerControllerPrivate::IsAltDown(*this))
	{
		BeginCameraDrag(ECombatCameraDrag::Dolly, EKeys::RightMouseButton);
		return;
	}

	UCombatSubsystem* Subsystem = GetWorld()->GetSubsystem<UCombatSubsystem>();
	if (!Subsystem)
	{
		return;
	}
	// On release a click: cancel, or in edit mode erase the cell where it went down. Moving first makes it a look drag.
	BeginCameraDrag(ECombatCameraDrag::PendingLook, EKeys::RightMouseButton);
	bHasClickPoint = GetArenaPointUnderMouse(Subsystem->GetGridHeight(), ClickPoint);
}

void ACombatPlayerController::BeginScreenPan()
{
	bPainting = false;
	bErasing = false;
	BeginCameraDrag(ECombatCameraDrag::PanScreen, EKeys::LeftMouseButton);
	CameraDragSecondKey = EKeys::RightMouseButton;
}

void ACombatPlayerController::OnMiddleClick()
{
	BeginCameraDrag(ECombatCameraDrag::Pan, EKeys::MiddleMouseButton);
}

void ACombatPlayerController::OnWheel(double Steps)
{
	using namespace CombatPlayerControllerPrivate;
	if (CameraDrag == ECombatCameraDrag::Look)
	{
		FlySpeedScale = FMath::Clamp(FlySpeedScale * FMath::Pow(FlySpeedWheelFactor, Steps), MinFlySpeedScale, MaxFlySpeedScale);
		return;
	}
	ZoomCamera(Steps);
}

void ACombatPlayerController::OnRotatePiece()
{
	UCombatSubsystem* Subsystem = GetWorld()->GetSubsystem<UCombatSubsystem>();
	if (Subsystem && Subsystem->IsDesignMode() && Subsystem->GetDesignTool() == ECombatDesignTool::Build)
	{
		Subsystem->RotateDesignPiece(IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift) ? -1 : 1);
	}
}

void ACombatPlayerController::OnUndoKey()
{
	UCombatSubsystem* Subsystem = GetWorld()->GetSubsystem<UCombatSubsystem>();
	if (!Subsystem || !(IsInputKeyDown(EKeys::LeftControl) || IsInputKeyDown(EKeys::RightControl)))
	{
		return;
	}
	if (IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift))
	{
		Subsystem->RedoDesign();
	}
	else
	{
		Subsystem->UndoDesign();
	}
}

void ACombatPlayerController::OnRedoKey()
{
	UCombatSubsystem* Subsystem = GetWorld()->GetSubsystem<UCombatSubsystem>();
	if (Subsystem && (IsInputKeyDown(EKeys::LeftControl) || IsInputKeyDown(EKeys::RightControl)))
	{
		Subsystem->RedoDesign();
	}
}

void ACombatPlayerController::OnCycleWalls()
{
	if (UCombatSubsystem* Subsystem = GetWorld()->GetSubsystem<UCombatSubsystem>())
	{
		Subsystem->CycleWallMode();
	}
}

void ACombatPlayerController::OnResetCamera()
{
	if (UCombatSubsystem* Subsystem = GetWorld()->GetSubsystem<UCombatSubsystem>())
	{
		Subsystem->ResetCameraToOverview();
	}
}

bool ACombatPlayerController::GetArenaPointUnderMouse(double PlaneHeight, FVector& OutPoint) const
{
	FVector Origin;
	FVector Direction;
	return DeprojectMousePositionToWorld(Origin, Direction) && CombatCamera::RayToPlane(Origin, Direction, PlaneHeight, OutPoint);
}

bool ACombatPlayerController::GetArenaPointAt(const FVector2D& ScreenPosition, double PlaneHeight, FVector& OutPoint) const
{
	FVector Origin;
	FVector Direction;
	return DeprojectScreenPositionToWorld(ScreenPosition.X, ScreenPosition.Y, Origin, Direction)
		&& CombatCamera::RayToPlane(Origin, Direction, PlaneHeight, OutPoint);
}

void ACombatPlayerController::BeginCameraDrag(ECombatCameraDrag Drag, const FKey& Key)
{
	UCombatSubsystem* Subsystem = GetWorld()->GetSubsystem<UCombatSubsystem>();
	ACameraActor* Camera = Subsystem ? Subsystem->GetArenaCamera() : nullptr;
	float MouseX = 0.f;
	float MouseY = 0.f;
	if (!Camera || !GetMousePosition(MouseX, MouseY))
	{
		return;
	}

	CameraDrag = Drag;
	CameraDragKey = Key;
	CameraDragSecondKey = FKey();
	DragStartMouse = FVector2D(MouseX, MouseY);
	LastMouse = DragStartMouse;
	if (Drag == ECombatCameraDrag::Orbit && !GetArenaPointAt(DragStartMouse, Subsystem->GetGridHeight(), OrbitPivot))
	{
		OrbitPivot = Camera->GetActorLocation() + Camera->GetActorForwardVector() * CombatPlayerControllerPrivate::FallbackGroundDistance;
	}
}

void ACombatPlayerController::UpdateCameraDrag(float DeltaTime)
{
	using namespace CombatPlayerControllerPrivate;
	if (CameraDrag == ECombatCameraDrag::None)
	{
		return;
	}

	UCombatSubsystem* Subsystem = GetWorld()->GetSubsystem<UCombatSubsystem>();
	ACameraActor* Camera = Subsystem ? Subsystem->GetArenaCamera() : nullptr;
	float MouseX = 0.f;
	float MouseY = 0.f;
	if (!Camera || !GetMousePosition(MouseX, MouseY))
	{
		CameraDrag = ECombatCameraDrag::None;
		return;
	}
	const FVector2D Mouse(MouseX, MouseY);
	const UCombatSettings* Settings = GetDefault<UCombatSettings>();

	// A release over the HUD never reaches the game, so check the buttons themselves.
	const bool bHeld = IsInputKeyDown(CameraDragKey) && (!CameraDragSecondKey.IsValid() || IsInputKeyDown(CameraDragSecondKey));
	if (CameraDrag == ECombatCameraDrag::PendingClick)
	{
		if (!bHeld)
		{
			CameraDrag = ECombatCameraDrag::None;
			if (bHasClickPoint)
			{
				Subsystem->HandleArenaClick(ClickPoint);
			}
		}
		return;
	}
	if (CameraDrag == ECombatCameraDrag::PendingLook)
	{
		if (!bHeld)
		{
			CameraDrag = ECombatCameraDrag::None;
			if (!Subsystem->IsDesignMode())
			{
				Subsystem->HandleArenaCancel();
			}
			else
			{
				Subsystem->DesignRightClick(ClickPoint, bHasClickPoint);
			}
			return;
		}
		if (FVector2D::Distance(Mouse, DragStartMouse) < Settings->CameraDragThreshold)
		{
			return;
		}
		CameraDrag = ECombatCameraDrag::Look;
	}
	if (!bHeld)
	{
		// Releasing one button of a left + right pan ends it; the other button does nothing until it is released.
		CameraDrag = ECombatCameraDrag::None;
		return;
	}

	FVector Location = Camera->GetActorLocation();
	FRotator Rotation = Camera->GetActorRotation();
	const FVector2D Delta = Mouse - LastMouse;
	bool bKeepCursor = true;

	switch (CameraDrag)
	{
	case ECombatCameraDrag::Look:
	{
		Rotation.Yaw += Delta.X * Settings->CameraLookSpeed;
		Rotation.Pitch -= Delta.Y * Settings->CameraLookSpeed;
		const FRotationMatrix Axes(Rotation);
		const FVector Move = Axes.GetScaledAxis(EAxis::X) * AxisInput(*this, EKeys::W, EKeys::S)
			+ Axes.GetScaledAxis(EAxis::Y) * AxisInput(*this, EKeys::D, EKeys::A)
			+ FVector::UpVector * AxisInput(*this, EKeys::E, EKeys::Q);
		const bool bFast = IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift);
		Location += Move.GetSafeNormal() * Settings->CameraFlySpeed * FlySpeedScale * (bFast ? Settings->CameraFastMultiplier : 1.0) * DeltaTime;
		break;
	}
	case ECombatCameraDrag::Orbit:
		CombatCamera::Orbit(Location, Rotation, OrbitPivot, Delta.X * Settings->CameraLookSpeed, -Delta.Y * Settings->CameraLookSpeed,
			Settings->CameraMinPitch, Settings->CameraMaxPitch);
		break;
	case ECombatCameraDrag::Dolly:
	{
		FVector Ground;
		const double Distance = CombatCamera::RayToPlane(Location, Rotation.Vector(), Subsystem->GetGridHeight(), Ground)
			? FVector::Distance(Location, Ground) : FallbackGroundDistance;
		Location += Rotation.Vector() * (-Delta.Y / DollyPixelsPerStep) * Distance * Settings->CameraZoomStep;
		break;
	}
	case ECombatCameraDrag::PanScreen:
	{
		// Like dolly: one zoom step of the distance to the grid plane per DollyPixelsPerStep pixels.
		FVector Ground;
		const double Distance = CombatCamera::RayToPlane(Location, Rotation.Vector(), Subsystem->GetGridHeight(), Ground)
			? FVector::Distance(Location, Ground) : FallbackGroundDistance;
		const double PerPixel = Distance * Settings->CameraZoomStep / DollyPixelsPerStep;
		Location += FRotationMatrix(Rotation).GetScaledAxis(EAxis::Y) * Delta.X * PerPixel + FVector::UpVector * -Delta.Y * PerPixel;
		break;
	}
	case ECombatCameraDrag::Pan:
	{
		// Both points with the camera where it is now: moving it by their difference puts the old point under the cursor.
		FVector From;
		FVector To;
		if (GetArenaPointAt(LastMouse, Subsystem->GetGridHeight(), From) && GetArenaPointAt(Mouse, Subsystem->GetGridHeight(), To))
		{
			Location += From - To;
		}
		bKeepCursor = false;
		break;
	}
	default:
		break;
	}

	CombatCamera::Clamp(Location, Rotation, Subsystem->GetCameraBounds(), Settings->CameraMinPitch, Settings->CameraMaxPitch);
	Camera->SetActorLocationAndRotation(Location, Rotation);

	if (bKeepCursor)
	{
		SetMouseLocation(FMath::RoundToInt32(DragStartMouse.X), FMath::RoundToInt32(DragStartMouse.Y));
		LastMouse = DragStartMouse;
	}
	else
	{
		LastMouse = Mouse;
	}
}

void ACombatPlayerController::ZoomCamera(double Steps)
{
	UCombatSubsystem* Subsystem = GetWorld()->GetSubsystem<UCombatSubsystem>();
	ACameraActor* Camera = Subsystem ? Subsystem->GetArenaCamera() : nullptr;
	FVector Origin;
	FVector Direction;
	if (!Camera || !DeprojectMousePositionToWorld(Origin, Direction))
	{
		return;
	}

	const UCombatSettings* Settings = GetDefault<UCombatSettings>();
	FVector Location = Camera->GetActorLocation();
	FRotator Rotation = Camera->GetActorRotation();
	FVector Ground;
	const double Distance = CombatCamera::RayToPlane(Location, Direction, Subsystem->GetGridHeight(), Ground)
		? FVector::Distance(Location, Ground) : CombatPlayerControllerPrivate::FallbackGroundDistance;
	// In: a fraction of the distance; out: the inverse, so a step in and a step out cancel each other.
	const double Fraction = FMath::Clamp<double>(Settings->CameraZoomStep, 0.01, 0.9);
	const double PerStep = Steps > 0.0 ? Fraction : -Fraction / (1.0 - Fraction);
	Location += Direction * Distance * PerStep * FMath::Abs(Steps);

	CombatCamera::Clamp(Location, Rotation, Subsystem->GetCameraBounds(), Settings->CameraMinPitch, Settings->CameraMaxPitch);
	Camera->SetActorLocationAndRotation(Location, Rotation);
}
