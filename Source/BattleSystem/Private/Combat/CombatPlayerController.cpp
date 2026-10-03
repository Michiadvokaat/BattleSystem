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
}

void ACombatPlayerController::OnLeftClick()
{
	UCombatSubsystem* Subsystem = GetWorld()->GetSubsystem<UCombatSubsystem>();
	FVector Point;
	if (Subsystem && GetArenaPointUnderMouse(Subsystem->GetGridHeight(), Point))
	{
		Subsystem->HandleArenaClick(Point);
	}
}

void ACombatPlayerController::OnRightClick()
{
	if (UCombatSubsystem* Subsystem = GetWorld()->GetSubsystem<UCombatSubsystem>())
	{
		Subsystem->HandleArenaCancel();
	}
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
