// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Math/RandomStream.h"
#include "Combat/CombatDistanceMap.h"
#include "Combat/CombatGridData.h"

/** Unit stats as the simulation sees them: copied from a definition at fight start, times in ticks. */
struct FCombatUnitStats
{
	float MaxHP = 100.f;
	/** cm per second. */
	float MoveSpeed = 300.f;
	/** cm. */
	float Radius = 40.f;

	bool bHasAttack = false;
	FGameplayTag AttackType;
	/** Edge-to-edge distance in cm at which the unit can attack. */
	float AttackRange = 150.f;
	float AttackDamage = 10.f;
	/** Ticks between the starts of two attacks. At least 1. */
	int32 AttackCooldownTicks = 20;
	/** Ticks from the start of an attack until it hits. 0 = hits in the same tick. */
	int32 AttackWindupTicks = 0;
};

struct FCombatUnitSpawn
{
	FCombatUnitStats Stats;
	int32 Team = 0;
	FIntPoint StartCell = FIntPoint::ZeroValue;
};

/** Everything a fight needs. A fight is fully determined by this config (on the same build). */
struct FCombatSimConfig
{
	FCombatGridData Grid;
	/** Unit IDs are the indices in this array. */
	TArray<FCombatUnitSpawn> Units;
	int32 Seed = 0;
	int32 TickRate = 20;
	/** The fight ends as a time-out after this many ticks. */
	int32 MaxTicks = 2400;
	/** Once a unit first reaches attack range, its first attack waits a random 0..N ticks, so the seed matters. */
	int32 MaxFirstAttackDelayTicks = 10;
	/** The per-team distance maps (and so the targets) are rebuilt every N ticks, and after every death. */
	int32 RetargetIntervalTicks = 5;
	/** How many cells ahead on the route a unit looks for the farthest visible point to steer to. */
	int32 PathLookaheadCells = 8;
	/** Fraction of the overlap between two units that is pushed apart per tick (0..1). */
	float SeparationStrength = 0.5f;
};

struct FCombatUnit
{
	int32 Id = INDEX_NONE;
	int32 Team = 0;
	FCombatUnitStats Stats;

	/** Grid-local position in cm, at the end of the previous and the current step. */
	FVector2D PreviousPosition = FVector2D::ZeroVector;
	FVector2D Position = FVector2D::ZeroVector;
	/** cm per second during the last step. */
	FVector2D Velocity = FVector2D::ZeroVector;

	float HP = 0.f;
	bool bAlive = true;
	int32 TargetId = INDEX_NONE;
	/** The point the unit steered towards in the last step (the target, or a point on its route). For debugging. */
	FVector2D SteerPoint = FVector2D::ZeroVector;

	/** Ticks until the next attack may start. */
	int32 CooldownTicks = 0;
	/** Ticks to wait in range before the first attack; drawn from the seed. */
	int32 FirstAttackDelayTicks = 0;
	/** Ticks until the current attack hits; 0 = not attacking. */
	int32 WindupTicks = 0;
	int32 WindupTargetId = INDEX_NONE;
};

enum class ECombatEventType : uint8
{
	/** SourceId starts an attack on TargetId. */
	Attack,
	/** SourceId hits TargetId for Amount damage. */
	Hit,
	/** TargetId died. */
	Death,
};

struct FCombatEvent
{
	ECombatEventType Type = ECombatEventType::Attack;
	int32 SourceId = INDEX_NONE;
	int32 TargetId = INDEX_NONE;
	float Amount = 0.f;
};

enum class ECombatOutcome : uint8
{
	InProgress,
	/** Exactly one team has living units left. */
	TeamWon,
	/** All units died in the same step. */
	Draw,
	/** MaxTicks reached. */
	TimeLimit,
};

/**
 * The deterministic combat simulation. Plain C++: no UWorld, actors, timers or UObjects, so it runs
 * with presentation (via UCombatSubsystem) and headless (Combat.Simulate, tests) alike.
 * Same config + same seed = same fight. See the determinism rules in CLAUDE.md.
 */
class BATTLESYSTEM_API FCombatSimulation
{
public:
	explicit FCombatSimulation(const FCombatSimConfig& InConfig);

	/** Advances the fight by one fixed step. Does nothing once the fight is finished. */
	void Step();

	/** Steps until the fight is finished. */
	void RunToEnd();

	bool IsFinished() const { return Outcome != ECombatOutcome::InProgress; }
	ECombatOutcome GetOutcome() const { return Outcome; }
	/** Only valid for ECombatOutcome::TeamWon. */
	int32 GetWinningTeam() const { return WinningTeam; }

	/** Number of steps taken. */
	int32 GetTick() const { return Tick; }
	float GetFixedDt() const { return FixedDt; }
	const FCombatGridData& GetGrid() const { return Config.Grid; }
	const TArray<FCombatUnit>& GetUnits() const { return Units; }
	/** Events of the last step only. */
	const TArray<FCombatEvent>& GetEvents() const { return Events; }
	/** CRC32 of the state after the last step. */
	uint32 GetChecksum() const { return Checksum; }

	/** The distance map towards the enemies of a team, or null if the team has no units. */
	const FCombatDistanceMap* GetDistanceMap(int32 Team) const;

	static const TCHAR* OutcomeToString(ECombatOutcome InOutcome);

private:
	struct FPendingHit
	{
		int32 SourceId;
		int32 TargetId;
		float Damage;
	};

	void RebuildDistanceMaps();
	int32 FindNearestEnemy(const FCombatUnit& Unit) const;
	int32 ChooseTarget(const FCombatUnit& Unit) const;
	void UpdateUnit(FCombatUnit& Unit);
	/** Targeting and attacks; returns the movement the unit wants this step (before separation). */
	FVector2D UpdateCombat(FCombatUnit& Unit);
	FVector2D FindRouteSteerPoint(const FCombatUnit& Unit) const;
	FVector2D ComputeSeparation(const FCombatUnit& Unit) const;
	/** Moves from From towards To without ending in a blocked cell, sliding along one axis if needed. */
	FVector2D ResolveMove(const FVector2D& From, const FVector2D& To) const;
	void TryStartAttack(FCombatUnit& Unit, const FCombatUnit& Target);
	void ApplyPendingHits();
	void UpdateOutcome();
	uint32 ComputeChecksum() const;

	FCombatSimConfig Config;
	float FixedDt;
	FRandomStream Random;

	TArray<FCombatUnit> Units;
	TArray<FCombatEvent> Events;
	TArray<FPendingHit> PendingHits;

	/** Team values in order of first appearance, and the distance map towards each team's enemies. */
	TArray<int32> TeamIds;
	TArray<FCombatDistanceMap> DistanceMaps;
	bool bDistanceMapsDirty = true;

	int32 Tick = 0;
	ECombatOutcome Outcome = ECombatOutcome::InProgress;
	int32 WinningTeam = INDEX_NONE;
	uint32 Checksum = 0;
};
