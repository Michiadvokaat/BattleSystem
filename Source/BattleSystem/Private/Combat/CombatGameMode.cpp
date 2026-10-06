// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatGameMode.h"
#include "Combat/CombatHUD.h"
#include "Combat/CombatPlayerController.h"
#include "Combat/CombatSettings.h"
#include "Combat/CombatSubsystem.h"
#include "Engine/World.h"

ACombatGameMode::ACombatGameMode()
{
	bStartPlayersAsSpectators = true;
	HUDClass = ACombatHUD::StaticClass();
	PlayerControllerClass = ACombatPlayerController::StaticClass();
}

void ACombatGameMode::StartPlay()
{
	// All actors (including the grid) have begun play after this.
	Super::StartPlay();

	const UCombatSettings* Settings = GetDefault<UCombatSettings>();
	if (!Settings->bAutoStartFight)
	{
		return;
	}

	UCombatSubsystem* Subsystem = GetWorld()->GetSubsystem<UCombatSubsystem>();
	if (!Subsystem)
	{
		return;
	}

	// The default level, as the LevelDesigner and the control panel get it.
	Subsystem->EnsureDesignLevel();
	Subsystem->PlayDesignLevel(Settings->DefaultSeed);
}
