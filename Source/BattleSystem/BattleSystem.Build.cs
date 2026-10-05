// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class BattleSystem : ModuleRules
{
	public BattleSystem(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "GameplayTags", "DeveloperSettings" });

		PrivateDependencyModuleNames.AddRange(new string[] { "AssetRegistry", "Slate", "SlateCore", "Niagara", "Json", "JsonUtilities", "UMG", "SkeletalMerging", "GeometryCore", "GeometryFramework", "GeometryScriptingCore" });

		if (Target.bBuildEditor)
		{
			// LevelDesigner palette thumbnails (editor and PIE only; packaged builds show the names).
			PrivateDependencyModuleNames.Add("UnrealEd");
		}

		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
