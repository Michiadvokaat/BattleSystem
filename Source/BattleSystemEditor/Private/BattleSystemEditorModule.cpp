// Copyright Epic Games, Inc. All Rights Reserved.

#include "CombatColorVariant.h"
#include "ContentBrowserMenuContexts.h"
#include "Engine/SkeletalMesh.h"
#include "Modules/ModuleManager.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "BattleSystemEditor"

/** Editor tools: "Make Color Variant..." in the Content Browser's menu for skeletal meshes. */
class FBattleSystemEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FBattleSystemEditorModule::RegisterMenus));
	}

	virtual void ShutdownModule() override
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
	}

private:
	void RegisterMenus()
	{
		FToolMenuOwnerScoped Owner(this);
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.SkeletalMesh");
		FToolMenuSection& Section = Menu->FindOrAddSection("CombatLook", LOCTEXT("CombatLook", "Combat Look"));
		Section.AddDynamicEntry("MakeColorVariant", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
		{
			const UContentBrowserAssetContextMenuContext* Context = InSection.FindContext<UContentBrowserAssetContextMenuContext>();
			if (!Context)
			{
				return;
			}
			InSection.AddMenuEntry("MakeColorVariant",
				LOCTEXT("MakeColorVariant", "Make Color Variant..."),
				LOCTEXT("MakeColorVariantTip", "A copy of each selected mesh next to it with its colour regions in new colours."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([Context]()
				{
					CombatColorVariant::OpenDialog(Context->LoadSelectedObjects<USkeletalMesh>());
				})));
		}));
	}
};

IMPLEMENT_MODULE(FBattleSystemEditorModule, BattleSystemEditor)

#undef LOCTEXT_NAMESPACE
