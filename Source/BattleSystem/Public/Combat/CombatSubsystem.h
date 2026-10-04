// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Combat/CombatLevel.h"
#include "Combat/CombatReplay.h"
#include "Combat/CombatSimulation.h"
#include "Combat/CombatUnitActor.h"
#include "CombatSubsystem.generated.h"

class ACombatProjectileActor;
class UCombatCommandScript;
class UCombatCueTable;
class ACombatUnitActor;
class UCombatSetup;
class UCombatUnitDefinition;

BATTLESYSTEM_API DECLARE_LOG_CATEGORY_EXTERN(LogCombat, Log, All);

/** LevelDesigner tools: paint a cell kind, or place units. */
enum class ECombatDesignTool : uint8
{
	Wall,
	Hedge,
	Water,
	Unit,
	/** Enemies that appear during the selected wave. */
	Spawn,
};

/** What a fight is built from: a setup asset on the arena's own grid, or a level from the LevelDesigner. */
struct FCombatFightSource
{
	const UCombatSetup* Setup = nullptr;
	TOptional<FCombatLevel> Level;

	bool IsValid() const { return Setup != nullptr || Level.IsSet(); }
	/** "DA_Setup_Test" or "Level: Name". */
	FString GetName() const;
};

/**
 * Thin layer between the world and FCombatSimulation: builds a fight from the level's ACombatGrid and a
 * UCombatSetup, runs fixed steps from an accumulator, and drives the ACombatUnitActors.
 * Also saves and plays replays, runs batches, and registers the Combat.* console commands.
 */
UCLASS()
class BATTLESYSTEM_API UCombatSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual void Deinitialize() override;

	/** Starts a fight with presentation, replacing any running fight. Uses the project settings and the taunt range override. */
	bool StartFight(int32 Seed, const UCombatSetup* Setup);
	bool StartFightWithSettings(int32 Seed, const UCombatSetup* Setup, const FCombatSimSettings& Settings,
		TConstArrayView<FCombatCommand> Commands = {});
	/** Starts a fight from a setup or a level. A level is shown in the arena (grid, blocks) and the camera fits it. */
	bool StartFightFromSource(int32 Seed, const FCombatFightSource& Source, const FCombatSimSettings& Settings,
		TConstArrayView<FCombatCommand> Commands = {});

	/**
	 * A player command for a unit of the player's team. It runs CommandDelayTicks after the current tick
	 * (also while paused). Ignored while a replay plays or after the fight. The Tick of Command is set here.
	 */
	bool IssueCommand(FCombatCommand Command);

	/** Player command: start the next wave now (also during a running wave). */
	bool CallWave();
	/** A wave is left to call, and no call is queued yet. */
	bool CanCallWave() const;
	/** "Wave 2/4, next in 3.2 s" for the control panel; empty for fights without waves. */
	FString GetWaveText() const;

	/** The project settings with this subsystem's taunt range override. */
	FCombatSimSettings GetCurrentSimSettings() const { return FCombatSimSettings::FromProjectSettings(TauntRangeOverride); }

	/** Saves the finished fight as a replay in Saved/Replays. OutMessage says where, or why not. */
	bool SaveReplay(FString& OutMessage) const;
	/** Plays a replay file (a name in Saved/Replays or a path) with its own settings. OutMessage has any warnings. */
	bool PlayReplay(const FString& FileOrPath, FString& OutMessage);
	bool IsPlayingReplay() const { return bIsReplay; }
	/** After a replay finished: whether it came out identical. Empty otherwise. */
	const FString& GetReplayVerdict() const { return ReplayVerdict; }

	/** Runs Count fights headless (seeds StartSeed...) in this world's arena, with the current settings. */
	bool RunBatch(const FCombatFightSource& Source, int32 Count, int32 StartSeed, bool bWriteCsv, FString& OutSummary);
	const FString& GetLastBatchSummary() const { return LastBatchSummary; }

	/** Shared by RunBatch and the Combat.Batch command. Logs and returns the summary. */
	static bool RunBatchInWorld(UWorld* World, const FCombatFightSource& Source, int32 Count, int32 StartSeed,
		const FCombatSimSettings& Settings, bool bWriteCsv, FString& OutSummary, TConstArrayView<FCombatCommand> Commands = {});
	void StopFight();

	const FCombatSimulation* GetSimulation() const { return Simulation.Get(); }

	/** Pause and speed only change how fast fixed steps are taken; the fight itself stays the same. */
	void SetPaused(bool bInPaused) { bPaused = bInPaused; }
	bool IsPaused() const { return bPaused; }
	void SetTimeScale(float InTimeScale) { TimeScale = FMath::Max(InTimeScale, 0.f); }
	float GetTimeScale() const { return TimeScale; }

	/**
	 * Range (cm, edge to edge) for every taunt in fights started with StartFight; 0 = the Range from the Data Asset.
	 * Applied at the next start, so a running fight never changes.
	 */
	void SetTauntRangeOverride(float InRange) { TauntRangeOverride = FMath::Max(InRange, 0.f); }
	float GetTauntRangeOverride() const { return TauntRangeOverride; }

	/** Seed and setup name of the current (or last) fight. */
	int32 GetCurrentSeed() const { return CurrentSeed; }
	const FString& GetCurrentSetupName() const { return CurrentSetupName; }

	/** Player selection and targeting: presentation state only; it reaches the fight through IssueCommand. */
	int32 GetPlayerTeam() const;
	/** Selects a living unit of the player's team (INDEX_NONE or anything else deselects). Cancels Move targeting. */
	void SelectUnit(int32 UnitId);
	int32 GetSelectedUnitId() const { return SelectedUnitId; }
	/** The next arena click picks the selected unit's Move target. */
	void BeginMoveTargeting() { bAwaitingMoveTarget = SelectedUnitId != INDEX_NONE; }
	bool IsAwaitingMoveTarget() const { return bAwaitingMoveTarget; }
	/** Left click on the arena (a point on the grid plane): the Move target while targeting, else select the own unit there. */
	void HandleArenaClick(const FVector& WorldPoint);
	/** Right click: cancel Move targeting, else deselect. */
	void HandleArenaCancel();
	/** Height of the grid plane (for turning mouse clicks into arena points). */
	double GetGridHeight() const { return GridOrigin.Z; }

	/** Changes on every start and stop, so UI can rebuild per fight. */
	int32 GetFightSerial() const { return FightSerial; }
	const UCombatUnitDefinition* GetUnitDefinition(int32 UnitId) const;
	/** UI name of a player ability: its DisplayName, else the last part of its type ("Taunt"). */
	FText GetAbilityName(int32 UnitId, int32 AbilityIndex) const;
	/** What the unit is doing for the player: queued or running order, or "Dead". Empty while the AI runs it. */
	FString GetOrderText(int32 UnitId) const;
	/** The unit's active effects as labels (StatusIcons setting). */
	static TArray<FCombatStatusDisplay> GetStatusDisplays(const FCombatUnit& Unit);

	/**
	 * LevelDesigner (presentation only, no simulation): edit mode stops the fight and shows the level being
	 * edited, with preview units. Play starts a fight from it as it is (saved or not).
	 */
	void EnterDesignMode();
	void ExitDesignMode();
	bool IsDesignMode() const { return bDesignMode; }
	const FCombatLevel& GetDesignLevel() const { return DesignLevel; }
	void NewDesignLevel();
	/** Loads Levels/<Name>.json for editing. */
	bool LoadDesignLevel(const FString& Name, FString& OutMessage);
	/** Saves the edited level as Levels/<Name>.json (the name is cleaned to letters, digits, - and _). */
	bool SaveDesignLevel(const FString& Name, FString& OutMessage);
	/** Starts a fight from the edited level (leaves edit mode). */
	bool PlayDesignLevel(int32 Seed);
	void SetDesignSize(int32 Width, int32 Height);

	void SetDesignTool(ECombatDesignTool Tool) { DesignTool = Tool; }
	ECombatDesignTool GetDesignTool() const { return DesignTool; }
	void SetDesignUnitType(const FString& Type) { DesignUnitType = Type; }
	const FString& GetDesignUnitType() const { return DesignUnitType; }
	void SetDesignUnitTeam(int32 Team) { DesignUnitTeam = Team; }
	int32 GetDesignUnitTeam() const { return DesignUnitTeam; }

	/** The wave the Spawn tool places in and whose spawns are shown; INDEX_NONE when the level has no waves. */
	int32 GetDesignWave() const { return DesignWave; }
	void SetDesignWave(int32 WaveIndex);
	/** Adds an empty wave after the selected one and selects it. */
	void AddDesignWave();
	void RemoveDesignWave();
	/** Seconds after the wave start for spawns placed with the Spawn tool. */
	void SetDesignSpawnTime(float Seconds) { DesignSpawnTime = FMath::Max(Seconds, 0.f); }
	float GetDesignSpawnTime() const { return DesignSpawnTime; }

	/**
	 * Mouse on the arena in edit mode. Place: the current tool on the cell (a unit only when bStroke is false,
	 * so dragging does not drop a row of units; the same for spawns in the selected wave). Erase: removes the unit,
	 * the selected wave's spawn and the cell's wall/hedge/water.
	 */
	void DesignPaint(const FVector& WorldPoint, bool bErase, bool bStroke);

	/** Asset names of all UCombatUnitDefinition assets, sorted. */
	static TArray<FString> GetAllUnitDefinitionNames();

	/** Asset names of all UCombatSetup assets, sorted. */
	static TArray<FString> GetAllSetupNames();

	/**
	 * Builds a simulation config from a setup, using the world's ACombatGrid or, without one (or without
	 * a world), the fallback grid from UCombatSettings. Entries without a definition or outside the grid
	 * are skipped with a warning. OutDefinitions gets the definition per unit ID.
	 */
	static bool BuildSimConfig(UWorld* World, int32 Seed, const UCombatSetup& Setup, const FCombatSimSettings& Settings,
		FCombatSimConfig& OutConfig, FVector& OutGridOrigin, TArray<const UCombatUnitDefinition*>* OutDefinitions = nullptr);

	/** BuildSimConfig for a setup or a level (a level brings its own grid; the arena only gives the origin). */
	static bool BuildSimConfigFromSource(UWorld* World, int32 Seed, const FCombatFightSource& Source, const FCombatSimSettings& Settings,
		FCombatSimConfig& OutConfig, FVector& OutGridOrigin, TArray<const UCombatUnitDefinition*>* OutDefinitions = nullptr);

	/** Setup asset names, then "Level: <name>" for every saved level. */
	static TArray<FString> GetAllSourceNames();
	/** A name from GetAllSourceNames (or a setup name/path) to a fight source; levels are loaded from disk. */
	static bool ResolveSource(const FString& Name, FCombatFightSource& OutSource);
	/** Finds a unit definition by asset name or object path. */
	static const UCombatUnitDefinition* FindUnitDefinition(const FString& NameOrPath);

	/** Finds a setup by asset name or object path. An empty string gives the default setup from UCombatSettings. */
	static UCombatSetup* FindSetup(const FString& NameOrPath);
	/** Finds a command script by asset name or object path. */
	static UCombatCommandScript* FindCommandScript(const FString& NameOrPath);

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void DispatchEvents();
	/** Spawns the actor of a unit that is new in the simulation (fight start or wave spawn). */
	void SpawnUnitActor(const FCombatUnit& Unit);
	void SpawnProjectileActor(const FCombatEvent& Event);
	void UpdateActors(float Alpha);
	/** Debug lines and distance-map numbers, depending on the Combat.Debug console variable. */
	void DrawDebug(float Alpha) const;
	/** Range circles of area attacks (taunt), depending on the Combat.ShowRanges console variable. */
	void DrawAreaRanges(float Alpha) const;
	/** Outlines of telegraphed areas that have not gone off yet, with an inner circle that grows until they do. */
	void DrawPendingAreas() const;
	/** An area attack went off: flash its shape and play its cue. */
	void OnAreaFired(const FCombatEvent& Event);
	/** Plays the cue's VFX and sound at a location, if the cue table has them. */
	void PlayCue(const FGameplayTag& Cue, const FVector& Location) const;
	/** Draws an area's outline; Scale shrinks it towards the center (telegraph progress). */
	void DrawArea(const FCombatArea& Area, const FColor& Color, float Duration, float Thickness, float Scale = 1.f) const;
	/** Logs the result once the fight is over; for a replay also decides the verdict. */
	void ReportResult();
	/** After each step: record a checkpoint, or compare it while a replay plays. */
	void UpdateCheckpoints();
	/** Shows the source's grid in the arena: a level (blocks, resized floor, fitted camera) or the arena's own. */
	void ShowSourceInArena(const FCombatFightSource& Source);
	/** Moves the view camera straight above the shown grid, high enough to see all of it. */
	void FitCameraToShownGrid();
	void RestoreCamera();
	/** Shows the edited level in the arena and rebuilds the preview units. */
	void RefreshDesignView(bool bFitCamera);
	void DestroyDesignPreviews();
	FVector SimToWorld(const FVector2D& Local) const { return GridOrigin + FVector(Local.X, Local.Y, 0.0); }

	TUniquePtr<FCombatSimulation> Simulation;

	/** Indexed by unit ID. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ACombatUnitActor>> UnitActors;

	/** Indexed by unit ID; kept for per-attack presentation settings such as the projectile actor class. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UCombatUnitDefinition>> UnitDefinitions;

	/** Indexed by FCombatUnit::SourceIndex: the definition of every unit the fight can have, wave spawns included. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UCombatUnitDefinition>> SourceDefinitions;

	/** Projectiles in flight, by projectile ID. */
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<ACombatProjectileActor>> ProjectileActors;

	UPROPERTY(Transient)
	TObjectPtr<UCombatCueTable> CueTable;

	FVector GridOrigin = FVector::ZeroVector;
	double Accumulator = 0.0;
	int32 MaxStepsPerFrame = 5;

	bool bPaused = false;
	float TimeScale = 1.f;
	float TauntRangeOverride = 0.f;
	int32 CurrentSeed = 0;
	FString CurrentSetupName;
	FString CurrentSetupPath;
	/** Set when the current fight comes from a level (copied into replays). */
	TOptional<FCombatLevel> CurrentLevel;

	/** The view camera as placed in the arena, before it was fitted to a level. */
	TWeakObjectPtr<AActor> FittedCamera;
	TOptional<FTransform> OriginalCameraTransform;
	FCombatSimSettings CurrentSettings;

	bool bIsReplay = false;
	FCombatReplay PlayingReplay;
	FString ReplayVerdict;
	/** Checksums of this fight every ReplayCheckpointInterval ticks (saved in replays). */
	int32 CheckpointInterval = 20;
	TArray<FString> Checkpoints;
	/** Replay: the first checkpoint tick that differed, or INDEX_NONE. */
	int32 FirstDifferentTick = INDEX_NONE;
	FString LastBatchSummary;

	bool bDesignMode = false;
	FCombatLevel DesignLevel;
	ECombatDesignTool DesignTool = ECombatDesignTool::Wall;
	FString DesignUnitType;
	int32 DesignUnitTeam = 0;
	int32 DesignWave = INDEX_NONE;
	float DesignSpawnTime = 0.f;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ACombatUnitActor>> DesignPreviews;

	int32 SelectedUnitId = INDEX_NONE;
	bool bAwaitingMoveTarget = false;
	int32 FightSerial = 0;
};
