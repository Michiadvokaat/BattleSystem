// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatGameMode.h"
#include "Combat/CombatHUD.h"
#include "Combat/CombatPlayerController.h"
#include "Combat/CombatSettings.h"
#include "Combat/CombatSetup.h"
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
	// All actors (including the grid and obstacles) have begun play after this.
	Super::StartPlay();

	const UCombatSettings* Settings = GetDefault<UCombatSettings>();
	if (!Settings->bAutoStartFight)
	{
		return;
	}

	UCombatSubsystem* Subsystem = GetWorld()->GetSubsystem<UCombatSubsystem>();
	UCombatSetup* Setup = UCombatSubsystem::FindSetup(FString());
	if (!Subsystem || !Setup)
	{
		UE_LOG(LogCombat, Warning, TEXT("Auto-start skipped: no default setup in Project Settings > Game > Combat."));
		return;
	}

	Subsystem->StartFight(Settings->DefaultSeed, Setup);
}
