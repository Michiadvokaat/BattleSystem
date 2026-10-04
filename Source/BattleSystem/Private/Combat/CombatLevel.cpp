// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatLevel.h"
#include "Combat/CombatSimulation.h"
#include "Combat/CombatSubsystem.h"
#include "Combat/CombatUnitDefinition.h"
#include "HAL/FileManager.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FCombatLevel FCombatLevel::MakeEmpty(const FString& InName, int32 InWidth, int32 InHeight)
{
	FCombatLevel Level;
	Level.Name = InName;
	Level.Width = InWidth;
	Level.Height = InHeight;
	Level.Normalize();
	return Level;
}

void FCombatLevel::Normalize()
{
	Width = FMath::Clamp(Width, MinSize, MaxSize);
	Height = FMath::Clamp(Height, MinSize, MaxSize);
	Rows.SetNum(Height);
	for (FString& Row : Rows)
	{
		Row = Row.Left(Width);
		while (Row.Len() < Width)
		{
			Row.AppendChar(Open);
		}
	}
}

void FCombatLevel::Resize(int32 NewWidth, int32 NewHeight)
{
	Width = NewWidth;
	Height = NewHeight;
	Normalize();
	Units.RemoveAll([this](const FCombatLevelUnit& Unit) { return !IsInBounds(Unit.Cell); });
	for (FCombatLevelWave& Wave : Waves)
	{
		Wave.Spawns.RemoveAll([this](const FCombatLevelSpawn& Spawn) { return !IsInBounds(Spawn.Cell); });
	}
}

TCHAR FCombatLevel::GetCell(const FIntPoint& Cell) const
{
	if (!IsInBounds(Cell) || !Rows.IsValidIndex(Cell.Y) || Cell.X >= Rows[Cell.Y].Len())
	{
		return Open;
	}
	return Rows[Cell.Y][Cell.X];
}

void FCombatLevel::SetCell(const FIntPoint& Cell, TCHAR Kind)
{
	if (IsInBounds(Cell) && Rows.IsValidIndex(Cell.Y) && Cell.X < Rows[Cell.Y].Len())
	{
		Rows[Cell.Y][Cell.X] = Kind;
	}
}

ECombatCellFlags FCombatLevel::FlagsFor(TCHAR Kind)
{
	switch (Kind)
	{
	case Wall: return ECombatCellFlags::Blocked | ECombatCellFlags::BlocksSight;
	case Hedge: return ECombatCellFlags::BlocksSight;
	case Water: return ECombatCellFlags::Blocked;
	default: return ECombatCellFlags::None;
	}
}

int32 FCombatLevel::FindUnitAt(const FIntPoint& Cell) const
{
	return Units.IndexOfByPredicate([&Cell](const FCombatLevelUnit& Unit) { return Unit.Cell == Cell; });
}

int32 FCombatLevel::FindSpawnAt(int32 WaveIndex, const FIntPoint& Cell) const
{
	if (!Waves.IsValidIndex(WaveIndex))
	{
		return INDEX_NONE;
	}
	return Waves[WaveIndex].Spawns.IndexOfByPredicate([&Cell](const FCombatLevelSpawn& Spawn) { return Spawn.Cell == Cell; });
}

bool FCombatLevel::RemoveSpawnsAt(const FIntPoint& Cell)
{
	int32 Removed = 0;
	for (FCombatLevelWave& Wave : Waves)
	{
		Removed += Wave.Spawns.RemoveAll([&Cell](const FCombatLevelSpawn& Spawn) { return Spawn.Cell == Cell; });
	}
	return Removed > 0;
}

void FCombatLevel::ToGridData(FCombatGridData& OutGrid) const
{
	OutGrid.Init(Width, Height, CellSize);
	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			OutGrid.AddFlags(FIntPoint(X, Y), FlagsFor(GetCell(FIntPoint(X, Y))));
		}
	}
}

FString CombatLevels::GetDirectory()
{
	return FPaths::ProjectDir() / TEXT("Levels");
}

TArray<FString> CombatLevels::FindLevelNames()
{
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(GetDirectory() / TEXT("*.json")), true, false);
	TArray<FString> Names;
	for (const FString& File : Files)
	{
		Names.Add(FPaths::GetBaseFilename(File));
	}
	Names.Sort();
	return Names;
}

bool CombatLevels::ToJson(const FCombatLevel& Level, FString& OutJson)
{
	return FJsonObjectConverter::UStructToJsonObjectString(Level, OutJson);
}

bool CombatLevels::FromJson(const FString& Json, FCombatLevel& OutLevel)
{
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(Json, &OutLevel))
	{
		return false;
	}
	OutLevel.Normalize();
	// Older files have no waves; saved again they get the current version.
	OutLevel.FormatVersion = FCombatLevel().FormatVersion;
	return true;
}

bool CombatLevels::Save(const FCombatLevel& Level)
{
	FString Json;
	return !Level.Name.IsEmpty() && ToJson(Level, Json)
		&& FFileHelper::SaveStringToFile(Json, *(GetDirectory() / (Level.Name + TEXT(".json"))));
}

bool CombatLevels::Load(const FString& Name, FCombatLevel& OutLevel)
{
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *(GetDirectory() / (Name + TEXT(".json")))) || !FromJson(Json, OutLevel))
	{
		return false;
	}
	OutLevel.Name = Name;
	return true;
}

FString CombatLevels::CleanName(const FString& Name)
{
	FString Clean;
	for (const TCHAR Char : Name)
	{
		if (FChar::IsAlnum(Char) || Char == TEXT('_') || Char == TEXT('-'))
		{
			Clean.AppendChar(Char);
		}
	}
	return Clean;
}

bool CombatLevels::Exists(const FString& Name)
{
	return !Name.IsEmpty() && IFileManager::Get().FileExists(*(GetDirectory() / (Name + TEXT(".json"))));
}

bool CombatLevels::Rename(const FString& OldName, const FString& NewName)
{
	// Through load and save, so the name inside the file changes too.
	FCombatLevel Level;
	if (NewName.IsEmpty() || Exists(NewName) || !Load(OldName, Level))
	{
		return false;
	}
	Level.Name = NewName;
	return Save(Level) && Delete(OldName);
}

bool CombatLevels::Delete(const FString& Name)
{
	return Exists(Name) && IFileManager::Get().Delete(*(GetDirectory() / (Name + TEXT(".json"))));
}

bool CombatLevels::BuildConfig(const FCombatLevel& Level, int32 TickRate, TFunctionRef<const UCombatUnitDefinition*(const FString&)> Resolve,
	FCombatSimConfig& OutConfig, TArray<const UCombatUnitDefinition*>* OutDefinitions)
{
	Level.ToGridData(OutConfig.Grid);

	OutConfig.Units.Reset();
	for (int32 Index = 0; Index < Level.Units.Num(); ++Index)
	{
		const FCombatLevelUnit& Entry = Level.Units[Index];
		const UCombatUnitDefinition* Definition = Resolve(Entry.Type);
		if (!Definition)
		{
			UE_LOG(LogCombat, Warning, TEXT("Level %s: unit %d has unknown type '%s', skipped."), *Level.Name, Index, *Entry.Type);
			continue;
		}
		if (!OutConfig.Grid.IsWalkable(Entry.Cell))
		{
			UE_LOG(LogCombat, Warning, TEXT("Level %s: unit %d stands on cell (%d,%d), which is outside the grid or blocked; skipped."),
				*Level.Name, Index, Entry.Cell.X, Entry.Cell.Y);
			continue;
		}

		FCombatUnitSpawn& Spawn = OutConfig.Units.AddDefaulted_GetRef();
		Spawn.Stats = Definition->ToSimStats(TickRate);
		Spawn.Team = Entry.Team;
		Spawn.StartCell = Entry.Cell;
		if (OutDefinitions)
		{
			OutDefinitions->Add(Definition);
		}
	}

	if (OutConfig.Units.IsEmpty())
	{
		UE_LOG(LogCombat, Error, TEXT("Level %s: no valid units."), *Level.Name);
		return false;
	}

	// Every wave is kept (also when empty), so wave numbers stay as in the designer.
	OutConfig.Waves.Reset();
	for (int32 WaveIndex = 0; WaveIndex < Level.Waves.Num(); ++WaveIndex)
	{
		FCombatWave& Wave = OutConfig.Waves.AddDefaulted_GetRef();
		const TArray<FCombatLevelSpawn>& Spawns = Level.Waves[WaveIndex].Spawns;
		for (int32 Index = 0; Index < Spawns.Num(); ++Index)
		{
			const FCombatLevelSpawn& Entry = Spawns[Index];
			const UCombatUnitDefinition* Definition = Resolve(Entry.Type);
			if (!Definition)
			{
				UE_LOG(LogCombat, Warning, TEXT("Level %s: wave %d spawn %d has unknown type '%s', skipped."), *Level.Name, WaveIndex + 1, Index, *Entry.Type);
				continue;
			}
			if (!OutConfig.Grid.IsWalkable(Entry.Cell))
			{
				UE_LOG(LogCombat, Warning, TEXT("Level %s: wave %d spawn %d is on cell (%d,%d), which is outside the grid or blocked; skipped."),
					*Level.Name, WaveIndex + 1, Index, Entry.Cell.X, Entry.Cell.Y);
				continue;
			}

			FCombatWaveSpawn& Spawn = Wave.Spawns.AddDefaulted_GetRef();
			Spawn.Stats = Definition->ToSimStats(TickRate);
			Spawn.Cell = Entry.Cell;
			Spawn.DelayTicks = FMath::Max(FMath::RoundToInt32(Entry.Time * TickRate), 0);
			if (OutDefinitions)
			{
				OutDefinitions->Add(Definition);
			}
		}
	}
	return true;
}
