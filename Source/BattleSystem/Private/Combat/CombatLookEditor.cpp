// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatLookEditor.h"
#include "Combat/CombatSettings.h"
#include "Combat/CombatSubsystem.h"
#include "Combat/CombatUnitData.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/DataTable.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Kismet/DataTableFunctionLibrary.h"
#include "Misc/Paths.h"
#include "Widgets/Notifications/SNotificationList.h"

#if WITH_EDITOR
#include "FileHelpers.h"
#include "Kismet2/BlueprintEditorUtils.h"
#endif

namespace
{
	void Notify(const FString& Message, bool bSuccess)
	{
		UE_LOG(LogCombat, Log, TEXT("Look Editor: %s"), *Message);
#if WITH_EDITOR
		FNotificationInfo Info(FText::FromString(Message));
		Info.ExpireDuration = 4.f;
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
#endif
	}

	FCombatUnitRow* FindUnitRow(UDataTable*& OutTable, FName RowName)
	{
		OutTable = GetDefault<UCombatSettings>()->UnitTable.LoadSynchronous();
		return OutTable ? OutTable->FindRow<FCombatUnitRow>(RowName, TEXT("Look Editor"), false) : nullptr;
	}
}

ACombatLookEditor::ACombatLookEditor()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ACombatLookEditor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// Only the figure: no placeholder shape, health bar or labels.
	BodyMesh->SetVisibility(false);
	NoseMesh->SetVisibility(false);
	HealthBarWidget->SetVisibility(false);
	StatusWidget->SetVisibility(false);
	if (!EditedLook.HasParts())
	{
		return;
	}
	bBuildingInConstruction = true;
	InitLook(EditedLook, Seed);
	bBuildingInConstruction = false;
	HealthBarWidget->SetVisibility(false);
	StatusWidget->SetVisibility(false);
	CharacterMesh->SetRelativeRotation(EditedLook.MeshRotation + FRotator(0.f, PreviewYaw, 0.f));
}

void ACombatLookEditor::LoadFromRow()
{
	UDataTable* Table = nullptr;
	const FCombatUnitRow* Row = FindUnitRow(Table, UnitRow);
	if (!Row)
	{
		Notify(FString::Printf(TEXT("no row '%s' in the unit table"), *UnitRow.ToString()), false);
		return;
	}
	Modify();
	EditedLook = Row->Look;
	RefreshAfterLoad();
	Notify(FString::Printf(TEXT("loaded the look of %s"), *UnitRow.ToString()), true);
}

void ACombatLookEditor::SaveToRow()
{
#if WITH_EDITOR
	UDataTable* Table = nullptr;
	FCombatUnitRow* Row = FindUnitRow(Table, UnitRow);
	if (!Row)
	{
		Notify(FString::Printf(TEXT("no row '%s' in the unit table"), *UnitRow.ToString()), false);
		return;
	}
	Table->Modify();
	Row->Look = EditedLook;
	Table->PostEditChange();
	Table->MarkPackageDirty();
	const bool bSaved = UEditorLoadingAndSavingUtils::SavePackages({ Table->GetPackage() }, false);
	// The JSON in git stays the truth, written the way Scripts/ImportCombatData.py export writes it.
	const FString JsonPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data/Units.json"));
	const bool bExported = UDataTableFunctionLibrary::ExportDataTableToJSONFile(Table, JsonPath);
	Notify(FString::Printf(TEXT("saved the look into %s (%s, %s)"), *UnitRow.ToString(),
		bSaved ? TEXT("table saved") : TEXT("table NOT saved"), bExported ? TEXT("Units.json written") : TEXT("Units.json NOT written")),
		bSaved && bExported);
#endif
}

void ACombatLookEditor::RefreshAfterLoad()
{
#if WITH_EDITOR
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		TArray<UObject*> Instances;
		GetArchetypeInstances(Instances);
		for (UObject* Instance : Instances)
		{
			if (ACombatLookEditor* Editor = Cast<ACombatLookEditor>(Instance))
			{
				Editor->EditedLook = EditedLook;
				Editor->UnitRow = UnitRow;
				Editor->RerunConstructionScripts();
			}
		}
		if (UBlueprint* Blueprint = Cast<UBlueprint>(GetClass()->ClassGeneratedBy))
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		}
		return;
	}
	RerunConstructionScripts();
#endif
}

#if WITH_EDITOR
void ACombatLookEditor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	// A row picked for an empty look loads at once.
	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(ACombatLookEditor, UnitRow) && !EditedLook.HasParts())
	{
		LoadFromRow();
	}
}
#endif
