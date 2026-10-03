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
	return true;
}
