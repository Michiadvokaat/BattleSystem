// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Math/RandomStream.h"
#include "Combat/CombatDistanceMap.h"
#include "Combat/CombatGridData.h"

/** One attack as the simulation sees it. */
struct FCombatAttackStats
{
	FGameplayTag Type;
	/** Edge-to-edge distance in cm at which the attack can start. */
	float Range = 150.f;
	float Damage = 10.f;
	/** Ticks between the starts of two uses of this attack. At least 1. */
	int32 CooldownTicks = 20;
	/** Ticks from the start of the attack until it hits or fires. 0 = same tick. */
	int32 WindupTicks = 0;
	/** Melee: needs a clear walking line to the target. Units with only such attacks are melee units. */
	bool bNeedsWalkableLine = true;
	/** Needs line of sight (no sight-blocking cells) to the target. */
	bool bNeedsLineOfSight = false;
	/** cm per second of the projectile it fires; 0 = no projectile, the hit lands directly. */
	float ProjectileSpeed = 0.f;
	/** Index of this attack in the unit definition's Attacks, for the presentation layer. */
	int32 SourceIndex = INDEX_NONE;

	bool IsRanged() const { return !bNeedsWalkableLine; }
};

/** Unit stats as the simulation sees them: copied from a definition at fight start, times in ticks. */
struct FCombatUnitStats
{
	float MaxHP = 100.f;
	/** cm per second. */
	float MoveSpeed = 300.f;
	/** cm. */
	float Radius = 40.f;

	TArray<FCombatAttackStats, TInlineAllocator<2>> Attacks;
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

	/** Ticks until each attack (same index as Stats.Attacks) may start again. */
	TArray<int32, TInlineAllocator<2>> AttackCooldowns;
	/** Ticks to wait in range before the first attack; drawn from the seed. */
	int32 FirstAttackDelayTicks = 0;
	/** Ticks until the current attack hits or fires; 0 = not attacking. */
	int32 WindupTicks = 0;
	int32 WindupTargetId = INDEX_NONE;
	int32 WindupAttackIndex = INDEX_NONE;
};

/** A projectile in flight. It homes in on its target and is removed when it hits, is blocked, or its target dies. */
struct FCombatProjectile
{
	int32 Id = INDEX_NONE;
	int32 SourceId = INDEX_NONE;
	int32 TargetId = INDEX_NONE;
	/** Index in the source unit's Stats.Attacks. */
	int32 AttackIndex = INDEX_NONE;
	int32 Team = 0;
	/** Grid-local position in cm, at the end of the previous and the current step. */
	FVector2D PreviousPosition = FVector2D::ZeroVector;
	FVector2D Position = FVector2D::ZeroVector;
	/** cm per second. */
	float Speed = 0.f;
	float Damage = 0.f;
	bool bEnded = false;
};

enum class ECombatEventType : uint8
{
	/** SourceId starts attack AttackIndex on TargetId. */
	Attack,
	/** SourceId hits TargetId for Amount damage. */
	Hit,
	/** TargetId died. */
	Death,
	/** SourceId fired projectile ProjectileId (attack AttackIndex) at TargetId. */
	ProjectileSpawned,
	/** Projectile ProjectileId is gone: it hit, was blocked, or its target died. */
	ProjectileEnded,
};

struct FCombatEvent
{
	ECombatEventType Type = ECombatEventType::Attack;
	int32 SourceId = INDEX_NONE;
	int32 TargetId = INDEX_NONE;
	float Amount = 0.f;
	int32 AttackIndex = INDEX_NONE;
	int32 ProjectileId = INDEX_NONE;
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
	/** Projectiles in flight after the last step, in spawn order. */
	const TArray<FCombatProjectile>& GetProjectiles() const { return Projectiles; }
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
	/** For units with a ranged attack: the nearest enemy that attack can hit right now, or INDEX_NONE. */
	int32 FindVisibleEnemyInRange(const FCombatUnit& Unit) const;
	/** The attack with the smallest range that can reach the target now (cooldown not considered), or INDEX_NONE. */
	int32 FindUsableAttack(const FCombatUnit& Unit, double Gap, bool bClearWalkingLine, bool bLineOfSight) const;
	void UpdateUnit(FCombatUnit& Unit);
	/** Targeting and attacks; returns the movement the unit wants this step (before separation). */
	FVector2D UpdateCombat(FCombatUnit& Unit);
	FVector2D FindRouteSteerPoint(const FCombatUnit& Unit) const;
	FVector2D ComputeSeparation(const FCombatUnit& Unit) const;
	/** Moves from From towards To without ending in a blocked cell, sliding along one axis if needed. */
	FVector2D ResolveMove(const FVector2D& From, const FVector2D& To) const;
	void TryStartAttack(FCombatUnit& Unit, const FCombatUnit& Target, int32 AttackIndex);
	/** End of the windup: queue the hit, or spawn the projectile. */
	void FireAttack(const FCombatUnit& Unit, int32 AttackIndex, int32 TargetId);
	void UpdateProjectiles();
	void EndProjectile(FCombatProjectile& Projectile);
	void ApplyPendingHits();
	void UpdateOutcome();
	uint32 ComputeChecksum() const;

	FCombatSimConfig Config;
	float FixedDt;
	FRandomStream Random;

	TArray<FCombatUnit> Units;
	TArray<FCombatEvent> Events;
	TArray<FPendingHit> PendingHits;
	TArray<FCombatProjectile> Projectiles;
	int32 NextProjectileId = 0;

	/** Team values in order of first appearance, and the distance map towards each team's enemies. */
	TArray<int32> TeamIds;
	TArray<FCombatDistanceMap> DistanceMaps;
	bool bDistanceMapsDirty = true;

	int32 Tick = 0;
	ECombatOutcome Outcome = ECombatOutcome::InProgress;
	int32 WinningTeam = INDEX_NONE;
	uint32 Checksum = 0;
};
