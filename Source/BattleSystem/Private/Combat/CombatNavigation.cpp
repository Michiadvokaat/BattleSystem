// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatNavigation.h"

namespace CombatNavigation
{
	int32 GetClearanceClass(float Radius, float CellSize)
	{
		const double SubSize = CellSize / Subdivision;
		// A small tolerance, so a radius exactly on a class border (50 cm) stays in the lower class.
		return FMath::Clamp(FMath::CeilToInt32(Radius / SubSize - 0.5 - 1e-4), 0, MaxClass);
	}

	float GetClassClearance(int32 Class, float CellSize)
	{
		return (FMath::Max(Class, 0) + 0.5f) * CellSize / Subdivision;
	}

	void BuildNavGrid(const FCombatGridData& Grid, int32 Class, FCombatGridData& OutNav)
	{
		const int32 K = Subdivision;
		const double CellSize = Grid.CellSize;
		const double Clearance = GetClassClearance(Class, Grid.CellSize);
		// Comparisons on equal distances (a sub-cell center exactly at the clearance) count as clear.
		const double Tolerance = 1e-3;
		const int32 Reach = FMath::CeilToInt32(Clearance / CellSize) + 1;
		OutNav.Init(Grid.Width * K, Grid.Height * K, Grid.CellSize / K);

		for (int32 Y = 0; Y < OutNav.Height; ++Y)
		{
			for (int32 X = 0; X < OutNav.Width; ++X)
			{
				const FIntPoint Sub(X, Y);
				const FIntPoint Cell(X / K, Y / K);
				if (!Grid.IsWalkable(Cell))
				{
					OutNav.AddFlags(Sub, ECombatCellFlags::Blocked);
					continue;
				}

				// The nearest unwalkable cell (also outside the grid) or edge wall within reach.
				const FVector2D Center = OutNav.CellToLocal(Sub);
				double Nearest = TNumericLimits<double>::Max();
				for (int32 CY = Cell.Y - Reach; CY <= Cell.Y + Reach; ++CY)
				{
					for (int32 CX = Cell.X - Reach; CX <= Cell.X + Reach; ++CX)
					{
						const FIntPoint Other(CX, CY);
						if (!Grid.IsWalkable(Other))
						{
							const double DX = FMath::Max3(CX * CellSize - Center.X, 0.0, Center.X - (CX + 1) * CellSize);
							const double DY = FMath::Max3(CY * CellSize - Center.Y, 0.0, Center.Y - (CY + 1) * CellSize);
							Nearest = FMath::Min(Nearest, FMath::Sqrt(DX * DX + DY * DY));
						}
						else if (Grid.HasEdgeWalls())
						{
							// Its west border (a vertical segment) and its north border (a horizontal one).
							if (Grid.HasEdgeWall(FIntPoint(CX - 1, CY), Other))
							{
								const double DX = Center.X - CX * CellSize;
								const double DY = FMath::Max3(CY * CellSize - Center.Y, 0.0, Center.Y - (CY + 1) * CellSize);
								Nearest = FMath::Min(Nearest, FMath::Sqrt(DX * DX + DY * DY));
							}
							if (Grid.HasEdgeWall(FIntPoint(CX, CY - 1), Other))
							{
								const double DX = FMath::Max3(CX * CellSize - Center.X, 0.0, Center.X - (CX + 1) * CellSize);
								const double DY = Center.Y - CY * CellSize;
								Nearest = FMath::Min(Nearest, FMath::Sqrt(DX * DX + DY * DY));
							}
						}
					}
				}
				if (Nearest < Clearance - Tolerance)
				{
					OutNav.AddFlags(Sub, ECombatCellFlags::Blocked);
				}
			}
		}

		// Edge walls on every sub-cell border along them, so steps never cross one.
		if (Grid.HasEdgeWalls())
		{
			for (int32 CY = 0; CY < Grid.Height; ++CY)
			{
				for (int32 CX = 0; CX < Grid.Width; ++CX)
				{
					const FIntPoint Cell(CX, CY);
					for (int32 S = 0; S < K; ++S)
					{
						if (Grid.HasEdgeWall(FIntPoint(CX - 1, CY), Cell))
						{
							OutNav.AddEdgeWall(FIntPoint(CX * K - 1, CY * K + S), FIntPoint(CX * K, CY * K + S));
						}
						if (Grid.HasEdgeWall(FIntPoint(CX, CY - 1), Cell))
						{
							OutNav.AddEdgeWall(FIntPoint(CX * K + S, CY * K - 1), FIntPoint(CX * K + S, CY * K));
						}
					}
				}
			}
		}
	}

	bool FindOpenCell(const FCombatGridData& Nav, const FVector2D& Local, FIntPoint& OutCell, int32 MaxRings)
	{
		const FIntPoint Start = Nav.LocalToCell(Local);
		if (Nav.IsWalkable(Start))
		{
			OutCell = Start;
			return true;
		}
		for (int32 Ring = 1; Ring <= MaxRings; ++Ring)
		{
			double Best = TNumericLimits<double>::Max();
			bool bFound = false;
			for (int32 DY = -Ring; DY <= Ring; ++DY)
			{
				for (int32 DX = -Ring; DX <= Ring; ++DX)
				{
					const FIntPoint Cell = Start + FIntPoint(DX, DY);
					if (FMath::Max(FMath::Abs(DX), FMath::Abs(DY)) != Ring || !Nav.IsWalkable(Cell))
					{
						continue;
					}
					const double Distance = FVector2D::DistSquared(Nav.CellToLocal(Cell), Local);
					if (Distance < Best)
					{
						Best = Distance;
						OutCell = Cell;
						bFound = true;
					}
				}
			}
			if (bFound)
			{
				return true;
			}
		}
		return false;
	}
}
