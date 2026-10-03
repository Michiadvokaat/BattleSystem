// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CombatGameMode.generated.h"

/**
 * Game mode for arena levels. Players start as spectators (the view comes from a camera placed in the
 * level), ACombatHUD shows the control panel, and a fight with the default setup starts automatically if
 * UCombatSettings::bAutoStartFight is set.
 */
UCLASS()
class BATTLESYSTEM_API ACombatGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACombatGameMode();

	virtual void StartPlay() override;
};
