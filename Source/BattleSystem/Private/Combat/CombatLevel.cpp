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
}

void FCombatLevel::Resize(int32 NewWidth, int32 NewHeight)
{
	Width = NewWidth;
	Height = NewHeight;
	Normalize();
	Units.RemoveAll([this](const FCombatLevelUnit& Unit) { return !IsInBounds(Unit.Cell); });
	Pieces.RemoveAll([this](const FCombatLevelPiece& Piece) { return !IsPieceInBounds(Piece); });
	for (FCombatLevelWave& Wave : Waves)
	{
		Wave.Spawns.RemoveAll([this](const FCombatLevelSpawn& Spawn) { return !IsInBounds(Spawn.Cell); });
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

FIntPoint FCombatLevelPiece::GetRotatedSize() const
{
	return Rotation % 2 == 0 ? Size : FIntPoint(Size.Y, Size.X);
}

FVector2D FCombatLevelPiece::GetDetailCenter(float CellSize) const
{
	const int32 Grid = FMath::Max(DetailGrid, 1);
	const int32 Position = FMath::Clamp(Detail, 0, Grid * Grid - 1);
	return FVector2D((Cell.X + (Position % Grid + 0.5) / Grid) * CellSize, (Cell.Y + (Position / Grid + 0.5) / Grid) * CellSize);
}

void FCombatLevelPiece::GetCells(TArray<FIntPoint>& OutCells) const
{
	OutCells.Reset();
	if (Layer == ECombatPieceLayer::Edge)
	{
		return;
	}
	if (Layer == ECombatPieceLayer::Detail)
	{
		OutCells.Add(Cell);
		return;
	}
	const FIntPoint RotatedSize = GetRotatedSize();
	for (int32 Y = 0; Y < FMath::Max(RotatedSize.Y, 1); ++Y)
	{
		for (int32 X = 0; X < FMath::Max(RotatedSize.X, 1); ++X)
		{
			OutCells.Add(Cell + FIntPoint(X, Y));
		}
	}
}

void FCombatLevelPiece::GetEdges(TArray<TPair<FIntPoint, FIntPoint>>& OutEdges) const
{
	OutEdges.Reset();
	if (Layer != ECombatPieceLayer::Edge)
	{
		return;
	}
	for (int32 Index = 0; Index < FMath::Max(Size.X, 1); ++Index)
	{
		if (IsHorizontalEdge())
		{
			OutEdges.Emplace(FIntPoint(Cell.X + Index, Cell.Y - 1), FIntPoint(Cell.X + Index, Cell.Y));
		}
		else
		{
			OutEdges.Emplace(FIntPoint(Cell.X - 1, Cell.Y + Index), FIntPoint(Cell.X, Cell.Y + Index));
		}
	}
}

bool FCombatLevelPiece::Overlaps(const FCombatLevelPiece& Other) const
{
	if (Layer != Other.Layer || Slot != Other.Slot)
	{
		return false;
	}
	if (Layer == ECombatPieceLayer::Detail)
	{
		// Equal grids compare positions; different grids compare the centers' sub-cells of the finer one.
		return Cell == Other.Cell && GetDetailCenter(1.f).Equals(Other.GetDetailCenter(1.f), 0.5 / FMath::Max3(DetailGrid, Other.DetailGrid, 1));
	}
	if (Layer == ECombatPieceLayer::Edge)
	{
		TArray<TPair<FIntPoint, FIntPoint>> Mine;
		TArray<TPair<FIntPoint, FIntPoint>> Theirs;
		GetEdges(Mine);
		Other.GetEdges(Theirs);
		return Mine.ContainsByPredicate([&Theirs](const TPair<FIntPoint, FIntPoint>& Edge) { return Theirs.Contains(Edge); });
	}
	TArray<FIntPoint> Mine;
	TArray<FIntPoint> Theirs;
	GetCells(Mine);
	Other.GetCells(Theirs);
	return Mine.ContainsByPredicate([&Theirs](const FIntPoint& Cell) { return Theirs.Contains(Cell); });
}

bool FCombatLevel::IsPieceInBounds(const FCombatLevelPiece& Piece) const
{
	if (Piece.Layer == ECombatPieceLayer::Edge)
	{
		// A border line: horizontal ones may lie on row borders 0..Height, vertical ones on column borders 0..Width.
		const int32 Length = FMath::Max(Piece.Size.X, 1);
		return Piece.IsHorizontalEdge()
			? Piece.Cell.X >= 0 && Piece.Cell.X + Length <= Width && Piece.Cell.Y >= 0 && Piece.Cell.Y <= Height
			: Piece.Cell.Y >= 0 && Piece.Cell.Y + Length <= Height && Piece.Cell.X >= 0 && Piece.Cell.X <= Width;
	}
	if (Piece.Layer == ECombatPieceLayer::Detail)
	{
		return IsInBounds(Piece.Cell) && Piece.Detail >= 0 && Piece.Detail < FMath::Square(FMath::Max(Piece.DetailGrid, 1));
	}
	const FIntPoint RotatedSize = Piece.GetRotatedSize();
	return IsInBounds(Piece.Cell) && IsInBounds(Piece.Cell + FIntPoint(FMath::Max(RotatedSize.X, 1) - 1, FMath::Max(RotatedSize.Y, 1) - 1));
}

bool FCombatLevel::PlacePiece(const FCombatLevelPiece& Piece)
{
	if (!IsPieceInBounds(Piece))
	{
		return false;
	}
	Pieces.RemoveAll([&Piece](const FCombatLevelPiece& Other) { return Other.Overlaps(Piece); });
	Pieces.Add(Piece);
	return true;
}

int32 FCombatLevel::FindPieceAt(ECombatPieceLayer Layer, const FIntPoint& Cell) const
{
	TArray<FIntPoint> Cells;
	for (int32 Index = 0; Index < Pieces.Num(); ++Index)
	{
		if (Pieces[Index].Layer == Layer)
		{
			Pieces[Index].GetCells(Cells);
			if (Cells.Contains(Cell))
			{
				return Index;
			}
		}
	}
	return INDEX_NONE;
}

int32 FCombatLevel::FindPieceUnder(const FVector2D& Local, float EdgeReach) const
{
	const FVector2D InCells = Local / FMath::Max(CellSize, 1.f);
	const FIntPoint Cell(FMath::FloorToInt32(InCells.X), FMath::FloorToInt32(InCells.Y));

	// A detail whose position (on its own grid) is under the point.
	for (int32 Index = 0; Index < Pieces.Num(); ++Index)
	{
		const FCombatLevelPiece& Piece = Pieces[Index];
		if (Piece.Layer == ECombatPieceLayer::Detail && Piece.Cell == Cell)
		{
			const int32 Grid = FMath::Max(Piece.DetailGrid, 1);
			const int32 X = FMath::Clamp(FMath::FloorToInt32((InCells.X - Cell.X) * Grid), 0, Grid - 1);
			const int32 Y = FMath::Clamp(FMath::FloorToInt32((InCells.Y - Cell.Y) * Grid), 0, Grid - 1);
			if (Piece.Detail == X + Y * Grid)
			{
				return Index;
			}
		}
	}

	// Of the pieces that match, one with a slot (on top) wins, else the first one.
	auto Topmost = [this](TFunctionRef<bool(const FCombatLevelPiece&)> Matches)
	{
		int32 Found = INDEX_NONE;
		for (int32 Index = 0; Index < Pieces.Num(); ++Index)
		{
			if (Matches(Pieces[Index]))
			{
				if (!Pieces[Index].Slot.IsEmpty())
				{
					return Index;
				}
				Found = Found == INDEX_NONE ? Index : Found;
			}
		}
		return Found;
	};

	TArray<FIntPoint> Covered;
	const int32 CellPiece = Topmost([&Cell, &Covered](const FCombatLevelPiece& Piece)
	{
		Piece.GetCells(Covered);
		return Piece.Layer == ECombatPieceLayer::Cell && Covered.Contains(Cell);
	});
	if (CellPiece != INDEX_NONE)
	{
		return CellPiece;
	}

	// The nearest border line within reach: a row border (between Y - 1 and Y) or a column border (between X - 1 and X).
	const double RowDistance = FMath::Abs(InCells.Y - FMath::RoundToDouble(InCells.Y));
	const double ColumnDistance = FMath::Abs(InCells.X - FMath::RoundToDouble(InCells.X));
	TArray<TPair<FIntPoint, FIntPoint>> Borders;
	if (RowDistance <= EdgeReach && RowDistance <= ColumnDistance)
	{
		const int32 Row = FMath::RoundToInt32(InCells.Y);
		Borders.Emplace(FIntPoint(Cell.X, Row - 1), FIntPoint(Cell.X, Row));
	}
	else if (ColumnDistance <= EdgeReach)
	{
		const int32 Column = FMath::RoundToInt32(InCells.X);
		Borders.Emplace(FIntPoint(Column - 1, Cell.Y), FIntPoint(Column, Cell.Y));
	}
	if (!Borders.IsEmpty())
	{
		TArray<TPair<FIntPoint, FIntPoint>> Edges;
		const int32 EdgePiece = Topmost([&Borders, &Edges](const FCombatLevelPiece& Piece)
		{
			Piece.GetEdges(Edges);
			return Edges.Contains(Borders[0]);
		});
		if (EdgePiece != INDEX_NONE)
		{
			return EdgePiece;
		}
	}

	return FindPieceAt(ECombatPieceLayer::Floor, Cell);
}

void FCombatLevel::ToGridData(FCombatGridData& OutGrid) const
{
	OutGrid.Init(Width, Height, CellSize);

	// Borders with a door frame (an opening that does not block): a wall there is passable; a leaf or window still blocks.
	TArray<TPair<FIntPoint, FIntPoint>> OpenBorders;
	TArray<TPair<FIntPoint, FIntPoint>> Edges;
	for (const FCombatLevelPiece& Piece : Pieces)
	{
		if (Piece.Layer == ECombatPieceLayer::Edge && Piece.Slot == FCombatLevelPiece::OpeningSlot && !Piece.bBlocksWalking && !Piece.bBlocksSight)
		{
			Piece.GetEdges(Edges);
			OpenBorders.Append(Edges);
		}
	}

	// Pieces in list order; flags and edge walls only add, so the order does not change the result.
	TArray<FIntPoint> Cells;
	for (const FCombatLevelPiece& Piece : Pieces)
	{
		// A blocking detail blocks its whole cell: the simulation has no sub-cells.
		if (Piece.Layer == ECombatPieceLayer::Cell || Piece.Layer == ECombatPieceLayer::Detail)
		{
			const ECombatCellFlags Flags = (Piece.bBlocksWalking ? ECombatCellFlags::Blocked : ECombatCellFlags::None)
				| (Piece.bBlocksSight ? ECombatCellFlags::BlocksSight : ECombatCellFlags::None);
			Piece.GetCells(Cells);
			for (const FIntPoint& Cell : Cells)
			{
				OutGrid.AddFlags(Cell, Flags);
			}
		}
		else if (Piece.Layer == ECombatPieceLayer::Edge && (Piece.bBlocksWalking || Piece.bBlocksSight))
		{
			Piece.GetEdges(Edges);
			for (const TPair<FIntPoint, FIntPoint>& Edge : Edges)
			{
				if (!Piece.Slot.IsEmpty() || !OpenBorders.Contains(Edge))
				{
					OutGrid.AddEdgeWall(Edge.Key, Edge.Value);
				}
			}
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
	// Before version 5 units faced the other side: team 0 +X, the rest and spawns -X.
	if (OutLevel.FormatVersion < 5)
	{
		for (FCombatLevelUnit& Unit : OutLevel.Units)
		{
			Unit.Rotation = Unit.Team == 0 ? 0 : UnitRotationSteps / 2;
		}
		for (FCombatLevelWave& Wave : OutLevel.Waves)
		{
			for (FCombatLevelSpawn& Spawn : Wave.Spawns)
			{
				Spawn.Rotation = UnitRotationSteps / 2;
			}
		}
	}
	// Older files have no waves, pieces or rotations (and rows, which are ignored); saved again they get the current version.
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
	FCombatSimConfig& OutConfig, TArray<const UCombatUnitDefinition*>* OutDefinitions, TArray<int32>* OutRotations)
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
		if (OutRotations)
		{
			OutRotations->Add(Entry.Rotation);
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
			if (OutRotations)
			{
				OutRotations->Add(Entry.Rotation);
			}
		}
	}
	return true;
}
