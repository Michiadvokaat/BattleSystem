// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatHUD.h"
#include "Combat/CombatSubsystem.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "SCombatControlPanel.h"
#include "SCombatLevelDesigner.h"
#include "SCombatUnitList.h"
#include "Widgets/Layout/SBox.h"

void ACombatHUD::BeginPlay()
{
	Super::BeginPlay();

	UGameViewportClient* Viewport = GetWorld()->GetGameViewport();
	if (!Viewport)
	{
		return;
	}

	// The full-screen box lets clicks outside the panel through to the game.
	PanelContainer = SNew(SBox)
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		.Padding(16.f)
		.Visibility(EVisibility::SelfHitTestInvisible)
		[
			SNew(SCombatControlPanel)
			.Subsystem(GetWorld()->GetSubsystem<UCombatSubsystem>())
		];
	Viewport->AddViewportWidgetContent(PanelContainer.ToSharedRef());

	UnitListContainer = SNew(SBox)
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Top)
		.Padding(16.f)
		.Visibility(EVisibility::SelfHitTestInvisible)
		[
			SNew(SCombatUnitList)
			.Subsystem(GetWorld()->GetSubsystem<UCombatSubsystem>())
		];
	Viewport->AddViewportWidgetContent(UnitListContainer.ToSharedRef());

	DesignerContainer = SNew(SBox)
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Bottom)
		.Padding(16.f)
		.Visibility(EVisibility::SelfHitTestInvisible)
		[
			SNew(SCombatLevelDesigner)
			.Subsystem(GetWorld()->GetSubsystem<UCombatSubsystem>())
		];
	Viewport->AddViewportWidgetContent(DesignerContainer.ToSharedRef());

	if (APlayerController* PlayerController = GetOwningPlayerController())
	{
		PlayerController->SetShowMouseCursor(true);

		FInputModeGameAndUI InputMode;
		InputMode.SetHideCursorDuringCapture(false);
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PlayerController->SetInputMode(InputMode);
	}
}

void ACombatHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		for (TSharedPtr<SWidget>* Container : { &PanelContainer, &UnitListContainer, &DesignerContainer })
		{
			if (*Container)
			{
				Viewport->RemoveViewportWidgetContent(Container->ToSharedRef());
			}
		}
	}
	PanelContainer.Reset();
	UnitListContainer.Reset();
	DesignerContainer.Reset();

	Super::EndPlay(EndPlayReason);
}
