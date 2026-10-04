// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatReplay.h"
#include "Combat/CombatSettings.h"
#include "Combat/CombatSimulation.h"
#include "Combat/CombatTags.h"
#include "HAL/FileManager.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FCombatSimSettings FCombatSimSettings::FromProjectSettings(float InTauntRangeOverride)
{
	const UCombatSettings* Project = GetDefault<UCombatSettings>();

	FCombatSimSettings Settings;
	Settings.TickRate = Project->TickRate;
	Settings.MaxTicks = Project->SecondsToTicks(Project->FightTimeLimit);
	Settings.MaxFirstAttackDelayTicks = Project->SecondsToTicks(Project->MaxFirstAttackDelay);
	Settings.RetargetIntervalTicks = FMath::Max(Project->SecondsToTicks(Project->RetargetInterval), 1);
	Settings.PathLookaheadCells = Project->PathLookaheadCells;
	Settings.SeparationStrength = Project->SeparationStrength;

	if (Project->ThreatDecayMode == ECombatThreatDecayMode::HalfLife)
	{
		Settings.ThreatDecayFactorPerTick = FMath::Pow(0.5f, 1.f / (FMath::Max(Project->ThreatHalfLife, 0.1f) * Settings.TickRate));
		Settings.ThreatDecayAmountPerTick = 0.f;
	}
	else
	{
		Settings.ThreatDecayFactorPerTick = 1.f;
		Settings.ThreatDecayAmountPerTick = Project->ThreatDecayPerSecond / Settings.TickRate;
	}
	Settings.ThreatThreshold = Project->ThreatThreshold;
	Settings.ThreatSwitchRatio = Project->ThreatSwitchRatio;
	Settings.RetargetDistanceMargin = Project->RetargetDistanceMargin;
	Settings.TauntRangeOverride = InTauntRangeOverride;
	Settings.WavePauseTicks = FMath::Max(Project->SecondsToTicks(Project->WavePauseSeconds), 1);
	return Settings;
}

void FCombatSimSettings::ApplyTo(FCombatSimConfig& Config) const
{
	Config.TickRate = TickRate;
	Config.MaxTicks = MaxTicks;
	Config.MaxFirstAttackDelayTicks = MaxFirstAttackDelayTicks;
	Config.RetargetIntervalTicks = RetargetIntervalTicks;
	Config.PathLookaheadCells = PathLookaheadCells;
	Config.SeparationStrength = SeparationStrength;
	Config.ThreatDecayFactorPerTick = ThreatDecayFactorPerTick;
	Config.ThreatDecayAmountPerTick = ThreatDecayAmountPerTick;
	Config.ThreatThreshold = ThreatThreshold;
	Config.ThreatSwitchRatio = ThreatSwitchRatio;
	Config.RetargetDistanceMargin = RetargetDistanceMargin;
	Config.WavePauseTicks = WavePauseTicks;

	if (TauntRangeOverride > 0.f)
	{
		auto OverrideTaunts = [this](FCombatUnitStats& Stats)
		{
			for (FCombatAttackStats& Attack : Stats.Attacks)
			{
				if (Attack.Type.MatchesTagExact(CombatTags::Attack_Taunt))
				{
					Attack.AreaRadius = TauntRangeOverride;
				}
			}
		};
		for (FCombatUnitSpawn& Spawn : Config.Units)
		{
			OverrideTaunts(Spawn.Stats);
		}
		for (FCombatWave& Wave : Config.Waves)
		{
			for (FCombatWaveSpawn& Spawn : Wave.Spawns)
			{
				OverrideTaunts(Spawn.Stats);
			}
		}
	}
}

FString CombatReplay::GetReplayDirectory()
{
	return FPaths::ProjectSavedDir() / TEXT("Replays");
}

TArray<FString> CombatReplay::FindReplayFiles()
{
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(GetReplayDirectory() / TEXT("*.json")), true, false);
	// Names start with a sortable timestamp: newest first.
	Files.Sort([](const FString& A, const FString& B) { return A > B; });
	return Files;
}

FString CombatReplay::ResolvePath(const FString& FileOrPath)
{
	if (FPaths::IsRelative(FileOrPath) && !FileOrPath.Contains(TEXT("/")) && !FileOrPath.Contains(TEXT("\\")))
	{
		return GetReplayDirectory() / FileOrPath;
	}
	return FileOrPath;
}

bool CombatReplay::ToJson(const FCombatReplay& Replay, FString& OutJson)
{
	return FJsonObjectConverter::UStructToJsonObjectString(Replay, OutJson);
}

bool CombatReplay::FromJson(const FString& Json, FCombatReplay& OutReplay)
{
	return FJsonObjectConverter::JsonObjectStringToUStruct(Json, &OutReplay);
}

bool CombatReplay::SaveToFile(const FCombatReplay& Replay, const FString& Path)
{
	FString Json;
	return ToJson(Replay, Json) && FFileHelper::SaveStringToFile(Json, *Path);
}

bool CombatReplay::LoadFromFile(const FString& Path, FCombatReplay& OutReplay)
{
	FString Json;
	return FFileHelper::LoadFileToString(Json, *Path) && FromJson(Json, OutReplay);
}

FString CombatReplay::ChecksumToString(uint32 Checksum)
{
	return FString::Printf(TEXT("0x%08X"), Checksum);
}
