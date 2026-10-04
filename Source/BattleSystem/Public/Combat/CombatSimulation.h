// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Math/RandomStream.h"
#include "Combat/CombatDistanceMap.h"
#include "Combat/CombatEffects.h"
#include "Combat/CombatGridData.h"
#include "Combat/CombatTypes.h"

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
	/** Threat the target gets on the attacker per point of damage. */
	float ThreatMultiplier = 1.f;
	/** Applied to the target(s) when the attack lands. */
	TArray<FCombatEffectStats> Effects;

	/** Area attacks (AoE, taunt): which units are hit when it goes off. None = only the target. */
	ECombatAreaShape AreaShape = ECombatAreaShape::None;
	/** cm; see ECombatAreaShape for how each shape measures it. */
	float AreaRadius = 0.f;
	/** Cone: cosine of half the cone angle (a unit is inside when the angle to it is at most half the cone angle). */
	float ConeCosHalfAngle = 0.f;
	/** Ticks between the attack firing and the area going off (telegraph). The area stays where it was placed. */
	int32 TelegraphTicks = 0;
	bool bAffectsEnemies = true;
	/** Includes the attacker itself. */
	bool bAffectsAllies = false;
	/** Presentation cue for the impact (hit or area going off). */
	FGameplayTag ImpactCue;

	/** Ranged = needs no walking line (a ranged or line-of-sight AoE attack), and aimed at a target. */
	bool IsRanged() const { return !bNeedsWalkableLine && IsTargeted(); }
	/** Aimed at the unit's target (everything except an area around the attacker itself). */
	bool IsTargeted() const { return AreaShape != ECombatAreaShape::CircleAroundSelf; }
	bool IsArea() const { return AreaShape != ECombatAreaShape::None; }
};

/** A placed area: where an area attack goes off. */
struct FCombatArea
{
	ECombatAreaShape Shape = ECombatAreaShape::None;
	/** Grid-local cm: the target position (CircleAtTarget) or the attacker position (CircleAroundSelf, Cone). */
	FVector2D Center = FVector2D::ZeroVector;
	/** Cone: unit direction from the attacker towards the target. */
	FVector2D Direction = FVector2D(1.0, 0.0);
	float Radius = 0.f;
	float ConeCosHalfAngle = 0.f;
	/** Radius of the attacker, for the edge-to-edge shapes (CircleAroundSelf, Cone). */
	float SourceRadius = 0.f;

	/** Whether a unit of the given radius at Position is inside the area. */
	bool Contains(const FVector2D& Position, float UnitRadius) const;
};

/** An area attack waiting to go off (telegraph). */
struct FCombatPendingArea
{
	int32 Id = INDEX_NONE;
	int32 SourceId = INDEX_NONE;
	int32 Team = 0;
	int32 AttackIndex = INDEX_NONE;
	FCombatArea Area;
	int32 TotalTicks = 0;
	int32 RemainingTicks = 0;
	/** The attacker's damage-dealt multiplier when it fired. */
	float DamageDealtMultiplier = 1.f;
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
	/** Abilities only the player triggers (Ability commands); the AI never uses them. No cooldown, no windup. */
	TArray<FCombatAttackStats> PlayerAbilities;
	/** Innate tags (for example immunities checked by BlockedByTags). */
	FGameplayTagContainer Tags;

	/** Attack indices run over Attacks first, then PlayerAbilities (index Attacks.Num() + ability index). */
	const FCombatAttackStats& GetAttack(int32 Index) const
	{
		return Index < Attacks.Num() ? Attacks[Index] : PlayerAbilities[Index - Attacks.Num()];
	}
	int32 GetPlayerAbilityAttackIndex(int32 AbilityIndex) const { return Attacks.Num() + AbilityIndex; }
};

struct FCombatUnitSpawn
{
	FCombatUnitStats Stats;
	int32 Team = 0;
	FIntPoint StartCell = FIntPoint::ZeroValue;
};

/** An enemy that appears during a wave. */
struct FCombatWaveSpawn
{
	FCombatUnitStats Stats;
	FIntPoint Cell = FIntPoint::ZeroValue;
	/** Ticks after the start of its wave. */
	int32 DelayTicks = 0;
};

struct FCombatWave
{
	TArray<FCombatWaveSpawn> Spawns;
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
	/**
	 * Enemy waves, in order. The first starts WavePauseTicks after the fight starts, every next one WavePauseTicks
	 * after the previous is clear (all its units spawned and no WaveTeam unit alive), or earlier with a CallWave command.
	 */
	TArray<FCombatWave> Waves;
	int32 WaveTeam = 1;
	int32 WavePauseTicks = 100;
	/** Player commands known in advance (a script or a replay), queued at the start in this order. */
	TArray<FCombatCommand> Commands;
	/** Once a unit first reaches attack range, its first attack waits a random 0..N ticks, so the seed matters. */
	int32 MaxFirstAttackDelayTicks = 10;
	/** The per-team distance maps (and so the targets) are rebuilt every N ticks, and after every death. */
	int32 RetargetIntervalTicks = 5;
	/** How many cells ahead on the route a unit looks for the farthest visible point to steer to. */
	int32 PathLookaheadCells = 8;
	/** Fraction of the overlap between two units that is pushed apart per tick (0..1). */
	float SeparationStrength = 0.5f;

	/** Threat decay per tick: threat = threat * ThreatDecayFactorPerTick - ThreatDecayAmountPerTick (half-life or linear). */
	float ThreatDecayFactorPerTick = 1.f;
	float ThreatDecayAmountPerTick = 0.f;
	/** Threat below this does not count for targeting. */
	float ThreatThreshold = 5.f;
	/** A unit on a threat target only switches to an enemy with this many times more threat. */
	float ThreatSwitchRatio = 1.2f;
	/** A unit on a nearest target only switches to an enemy that is this many cm closer (as the crow flies). */
	float RetargetDistanceMargin = 150.f;
};

/** Why a unit has its current target; also the order of priority. */
enum class ECombatTargetReason : uint8
{
	None,
	/** Nearest enemy by walking distance (or as the crow flies if none is reachable). */
	Nearest,
	/** Ranged: an enemy it can shoot right now. */
	Visible,
	/** The enemy with the most threat on this unit. */
	Threat,
	/** The source of a Status.Taunted effect. */
	Taunt,
};

struct FCombatThreatEntry
{
	int32 EnemyId = INDEX_NONE;
	float Threat = 0.f;
};

struct FCombatUnit
{
	int32 Id = INDEX_NONE;
	int32 Team = 0;
	/**
	 * What the unit was made from: its index in the config's Units, or Units.Num() + its index among all wave
	 * spawns (waves in order). The presentation and the statistics find the definition with it.
	 */
	int32 SourceIndex = INDEX_NONE;
	FCombatUnitStats Stats;

	/** Grid-local position in cm, at the end of the previous and the current step. */
	FVector2D PreviousPosition = FVector2D::ZeroVector;
	FVector2D Position = FVector2D::ZeroVector;
	/** cm per second during the last step. */
	FVector2D Velocity = FVector2D::ZeroVector;

	float HP = 0.f;
	bool bAlive = true;
	int32 TargetId = INDEX_NONE;
	ECombatTargetReason TargetReason = ECombatTargetReason::None;
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

	/** Threat per enemy that damaged this unit; at most MaxThreatEntries. */
	TArray<FCombatThreatEntry, TInlineAllocator<8>> Threat;
	FCombatEffectList Effects;

	/** Totals for statistics (Combat.Batch); not part of the checksum. */
	float DamageDealt = 0.f;
	float DamageTaken = 0.f;

	/** Own A* route (start to goal cell): to a target the team map does not lead to, or to a move order's cell. */
	TArray<FIntPoint> Path;

	/** Player Move command: walk to MoveTargetCell, ignoring enemies, until there. */
	bool bHasMoveOrder = false;
	FIntPoint MoveTargetCell = FIntPoint::ZeroValue;

	static constexpr int32 MaxThreatEntries = 8;

	float GetThreatOn(int32 EnemyId) const;
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
	/** Damage including the attacker's damage-dealt multiplier when it fired. */
	float Damage = 0.f;
	float ThreatMultiplier = 1.f;
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
	/** SourceId's attack AttackIndex applied an effect to TargetId. */
	EffectApplied,
	/** A player command for unit SourceId ran (AttackIndex is set for abilities). */
	CommandExecuted,
	/** A player command for unit SourceId could not run (dead unit, blocked cell, unknown ability). */
	CommandRejected,
	/** SourceId placed a telegraphed area (AreaId, Area) that goes off later. */
	AreaTelegraphStarted,
	/** SourceId's area attack AttackIndex went off (AreaId, Area). */
	AreaAttackFired,
	/** Wave WaveIndex started. */
	WaveStarted,
	/** Unit SourceId appeared (wave WaveIndex). */
	UnitSpawned,
};

struct FCombatEvent
{
	ECombatEventType Type = ECombatEventType::Attack;
	int32 SourceId = INDEX_NONE;
	int32 TargetId = INDEX_NONE;
	float Amount = 0.f;
	int32 AttackIndex = INDEX_NONE;
	int32 ProjectileId = INDEX_NONE;
	/** Area events only. */
	int32 AreaId = INDEX_NONE;
	FCombatArea Area;
	/** Presentation cue (the attack's ImpactCue) for Hit and AreaAttackFired. */
	FGameplayTag Cue;
	/** Wave events only. */
	int32 WaveIndex = INDEX_NONE;
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
	/** Telegraphed areas that have not gone off yet, in placement order. */
	const TArray<FCombatPendingArea>& GetPendingAreas() const { return PendingAreas; }

	/**
	 * Queues a player command for a future tick (Tick > GetTick()). Returns false for a tick that has
	 * already run. Accepted commands are kept in the command log, rejected-on-execution ones included.
	 */
	bool QueueCommand(const FCombatCommand& Command);
	/** Every accepted command, in the order given: with setup, seed and settings, this reproduces the fight. */
	const TArray<FCombatCommand>& GetCommandLog() const { return CommandLog; }
	/** Commands that have not run yet, by tick. */
	const TArray<FCombatCommand>& GetPendingCommands() const { return PendingCommands; }
	/** Events of the last step only. */
	const TArray<FCombatEvent>& GetEvents() const { return Events; }
	/** CRC32 of the state after the last step. */
	uint32 GetChecksum() const { return Checksum; }

	/** The distance map towards the enemies of a team, or null if the team has no units. */
	const FCombatDistanceMap* GetDistanceMap(int32 Team) const;

	int32 GetWaveCount() const { return Config.Waves.Num(); }
	/** Waves started so far (the next wave's index). */
	int32 GetWavesStarted() const { return WavesStarted; }
	/** Tick at whose start the next wave starts, or INDEX_NONE while waiting for the field to clear (or none is left). */
	int32 GetNextWaveTick() const { return NextWaveTick; }
	/** A wave still has to start or still has units to spawn; the wave team then counts as alive. */
	bool HasWavesLeft() const { return WavesStarted < Config.Waves.Num() || !PendingSpawns.IsEmpty(); }
	int32 GetWaveTeam() const { return Config.WaveTeam; }

	static const TCHAR* OutcomeToString(ECombatOutcome InOutcome);

private:
	struct FPendingHit
	{
		int32 SourceId;
		int32 TargetId;
		int32 AttackIndex;
		/** Including the attacker's damage-dealt multiplier; the target's damage-taken multiplier is applied on landing. */
		float Damage;
		float ThreatMultiplier;
	};

	/** A spawn of a started wave that has not appeared yet. */
	struct FPendingSpawn
	{
		int32 Tick;
		int32 WaveIndex;
		int32 SpawnIndex;
	};

	FCombatUnit& AddUnit(const FCombatUnitStats& Stats, int32 Team, const FIntPoint& Cell, int32 SourceIndex);
	void StartWave();
	/** Starts a wave whose time has come and spawns the units that are due. */
	void UpdateWaves();
	/** After the step: once the field is clear, the pause until the next wave starts. */
	void UpdateWaveClear();
	void RebuildDistanceMaps();
	int32 FindNearestEnemy(const FCombatUnit& Unit) const;
	/** Taunt immediately; otherwise every RetargetIntervalTicks (or without a valid target) by priority, with hysteresis. */
	void UpdateTarget(FCombatUnit& Unit);
	void ChooseTarget(FCombatUnit& Unit) const;
	int32 FindNearestByWalking(const FCombatUnit& Unit) const;
	bool CanShootNow(const FCombatUnit& Unit, const FCombatUnit& Other) const;
	/** An area-around-self attack that is off cooldown and worth using now (see FindReadyAreaAttack in the .cpp). */
	int32 FindReadyAreaAttack(const FCombatUnit& Unit) const;
	/** Whether an area attack of Source affects Other (team flags), ignoring the shape. */
	static bool AffectsUnit(const FCombatAttackStats& Attack, const FCombatUnit& Source, const FCombatUnit& Other);
	/** Places an area for an area attack: now, or as a telegraph that goes off later. */
	void PlaceArea(const FCombatUnit& Unit, int32 AttackIndex, int32 TargetId);
	/** Queues hits on every affected unit inside the area, using the positions at the start of this step. */
	void ResolveArea(int32 SourceId, int32 Team, int32 AttackIndex, const FCombatArea& Area, int32 AreaId, float DamageDealtMultiplier);
	void UpdatePendingAreas();
	void ExecuteDueCommands();
	void ExecuteCommand(const FCombatCommand& Command);
	/** Movement for a unit with a move order (clears the order on arrival). */
	FVector2D UpdateMoveOrder(FCombatUnit& Unit);
	/** Steers along the unit's own A* path to GoalCell; returns the steer point (Fallback if there is no path). */
	FVector2D SteerAlongPath(FCombatUnit& Unit, const FIntPoint& GoalCell, const FVector2D& Fallback) const;
	/** For units with a ranged attack: the nearest enemy that attack can hit right now, or INDEX_NONE. */
	int32 FindVisibleEnemyInRange(const FCombatUnit& Unit) const;
	/** The attack with the smallest range that can reach the target now (cooldown not considered), or INDEX_NONE. */
	int32 FindUsableAttack(const FCombatUnit& Unit, double Gap, bool bClearWalkingLine, bool bLineOfSight) const;
	void UpdateUnit(FCombatUnit& Unit);
	/** Targeting and attacks; returns the movement the unit wants this step (before separation). */
	FVector2D UpdateCombat(FCombatUnit& Unit);
	FVector2D FindRouteSteerPoint(FCombatUnit& Unit) const;
	FVector2D ComputeSeparation(const FCombatUnit& Unit) const;
	/** Moves from From towards To without ending in a blocked cell, sliding along one axis if needed. */
	FVector2D ResolveMove(const FVector2D& From, const FVector2D& To) const;
	void TryStartAttack(FCombatUnit& Unit, const FCombatUnit& Target, int32 AttackIndex);
	/** Starts an attack (event, cooldown, windup or fire); TargetId is INDEX_NONE for area attacks. */
	void StartAttack(FCombatUnit& Unit, int32 TargetId, int32 AttackIndex);
	/** End of the windup: queue the hit, or spawn the projectile. */
	void FireAttack(const FCombatUnit& Unit, int32 AttackIndex, int32 TargetId);
	void UpdateProjectiles();
	void EndProjectile(FCombatProjectile& Projectile);
	void ApplyPendingHits();
	void AddThreat(FCombatUnit& Unit, int32 EnemyId, float Amount);
	void DecayThreat(FCombatUnit& Unit);
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
	TArray<FCombatPendingArea> PendingAreas;
	int32 NextAreaId = 0;

	int32 WavesStarted = 0;
	int32 NextWaveTick = INDEX_NONE;
	/** In the order the waves started, each wave's spawns in their order. */
	TArray<FPendingSpawn> PendingSpawns;
	/** Per wave: the SourceIndex of its first spawn. */
	TArray<int32> WaveFirstSourceIndex;

	/** Commands not yet run, ordered by tick and then by the order they were given. */
	TArray<FCombatCommand> PendingCommands;
	TArray<FCombatCommand> CommandLog;

	/** Team values in order of first appearance, and the distance map towards each team's enemies. */
	TArray<int32> TeamIds;
	TArray<FCombatDistanceMap> DistanceMaps;
	bool bDistanceMapsDirty = true;

	int32 Tick = 0;
	ECombatOutcome Outcome = ECombatOutcome::InProgress;
	int32 WinningTeam = INDEX_NONE;
	uint32 Checksum = 0;
};
