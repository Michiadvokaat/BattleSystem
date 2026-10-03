// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatPlayerController.h"
#include "Combat/CombatSubsystem.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"

void ACombatPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ACombatPlayerController::OnLeftClick);
	InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &ACombatPlayerController::OnRightClick);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &ACombatPlayerController::OnLeftReleased);
	InputComponent->BindKey(EKeys::RightMouseButton, IE_Released, this, &ACombatPlayerController::OnRightReleased);
}

void ACombatPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	// A release over the HUD never reaches the game, so check the buttons themselves.
	bPainting &= IsInputKeyDown(EKeys::LeftMouseButton);
	bErasing &= IsInputKeyDown(EKeys::RightMouseButton);

	UCombatSubsystem* Subsystem = GetWorld()->GetSubsystem<UCombatSubsystem>();
	FVector Point;
	if ((bPainting || bErasing) && Subsystem && Subsystem->IsDesignMode() && GetArenaPointUnderMouse(Subsystem->GetGridHeight(), Point))
	{
		Subsystem->DesignPaint(Point, bErasing, true);
	}
}

void ACombatPlayerController::OnLeftClick()
{
	UCombatSubsystem* Subsystem = GetWorld()->GetSubsystem<UCombatSubsystem>();
	FVector Point;
	if (!Subsystem || !GetArenaPointUnderMouse(Subsystem->GetGridHeight(), Point))
	{
		return;
	}
	if (Subsystem->IsDesignMode())
	{
		bPainting = true;
		Subsystem->DesignPaint(Point, false, false);
		return;
	}
	Subsystem->HandleArenaClick(Point);
}

void ACombatPlayerController::OnRightClick()
{
	UCombatSubsystem* Subsystem = GetWorld()->GetSubsystem<UCombatSubsystem>();
	if (!Subsystem)
	{
		return;
	}
	FVector Point;
	if (Subsystem->IsDesignMode())
	{
		bErasing = true;
		if (GetArenaPointUnderMouse(Subsystem->GetGridHeight(), Point))
		{
			Subsystem->DesignPaint(Point, true, false);
		}
		return;
	}
	Subsystem->HandleArenaCancel();
}

bool ACombatPlayerController::GetArenaPointUnderMouse(double PlaneHeight, FVector& OutPoint) const
{
	FVector Origin;
	FVector Direction;
	if (!DeprojectMousePositionToWorld(Origin, Direction) || FMath::IsNearlyZero(Direction.Z))
	{
		return false;
	}

	const double Distance = (PlaneHeight - Origin.Z) / Direction.Z;
	if (Distance < 0.0)
	{
		return false;
	}
	OutPoint = Origin + Direction * Distance;
	return true;
}
