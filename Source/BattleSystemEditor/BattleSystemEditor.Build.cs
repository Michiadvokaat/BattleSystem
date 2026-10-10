// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// Editor-only tools for BattleSystem (Content Browser actions), kept out of the game module.
public class BattleSystemEditor : ModuleRules
{
	public BattleSystemEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });

		PrivateDependencyModuleNames.AddRange(new string[] { "BattleSystem", "UnrealEd", "AssetTools", "ContentBrowser", "ToolMenus",
			"Slate", "SlateCore", "InputCore", "PropertyEditor", "ImageCore" });
	}
}
