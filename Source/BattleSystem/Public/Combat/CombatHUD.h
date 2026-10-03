// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "CombatHUD.generated.h"

class SWidget;

/** Shows the combat control panel (top left), the player's unit list (top right), the LevelDesigner (bottom left) and a mouse cursor; input goes to both the game and the UI, so the console still works. */
UCLASS()
class BATTLESYSTEM_API ACombatHUD : public AHUD
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	TSharedPtr<SWidget> PanelContainer;
	TSharedPtr<SWidget> UnitListContainer;
	TSharedPtr<SWidget> DesignerContainer;
};
