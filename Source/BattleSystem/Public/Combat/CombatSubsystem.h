// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Combat/CombatGrid.h"
#include "Combat/CombatLevel.h"
#include "Combat/CombatReplay.h"
#include "Combat/CombatSimulation.h"
#include "Combat/CombatUnitActor.h"
#include "CombatSubsystem.generated.h"

class ACameraActor;
class UCombatPieceCatalog;
struct FCombatPieceDefinition;
class ACombatProjectileActor;
class UCombatCommandScript;
class UCombatCueTable;
class ACombatUnitActor;
class UCombatUnitDefinition;

BATTLESYSTEM_API DECLARE_LOG_CATEGORY_EXTERN(LogCombat, Log, All);

/** LevelDesigner modes: what a click in the arena places. */
enum class ECombatDesignTool : uint8
{
	/** Build Mode: a piece of the catalog (walls, floors, furniture, props). */
	Build,
	/** Unit Mode: units on the field from the start. */
	Unit,
	/** Spawn Mode: enemies that appear during the selected wave. */
	Spawn,
};

/** What a fight is built from: a level from the LevelDesigner (unset: no fight). */
struct FCombatFightSource
{
	TOptional<FCombatLevel> Level;

	bool IsValid() const { return Level.IsSet(); }
	/** "Level: Name". */
	FString GetName() const;
};

/**
 * Thin layer between the world and FCombatSimulation: builds a fight from a LevelDesigner level (shown on the
 * world's ACombatGrid), runs fixed steps from an accumulator, and drives the ACombatUnitActors.
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

	/** Starts a fight with presentation from a level, replacing any running fight. The level is shown in the arena and the camera fits it. */
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
	 * Range (cm, edge to edge) for every taunt in fights started from the game; 0 = the Range from the Data Asset.
	 * Applied at the next start, so a running fight never changes.
	 */
	void SetTauntRangeOverride(float InRange) { TauntRangeOverride = FMath::Max(InRange, 0.f); }
	float GetTauntRangeOverride() const { return TauntRangeOverride; }

	/** Seed and source name ("Level: Name") of the current (or last) fight. */
	int32 GetCurrentSeed() const { return CurrentSeed; }
	const FString& GetCurrentSourceName() const { return CurrentSourceName; }

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
	/** Walls Up / Cutaway / Down (key V, control panel): presentation only. */
	void CycleWallMode();
	ECombatWallMode GetWallMode() const { return WallMode; }
	FText GetWallModeText() const;

	/** Height of the grid plane (for turning mouse clicks into arena points). */
	double GetGridHeight() const { return GridOrigin.Z; }

	/**
	 * The camera the arena is seen through (the player's view target, an ACameraActor), for the free camera of
	 * ACombatPlayerController. The first call remembers its placed transform, which the overview of a setup returns to.
	 */
	ACameraActor* GetArenaCamera();
	/** Where the free camera may be: the shown grid plus CameraBoundsMargin, CameraMinHeight to CameraMaxHeight above it. */
	FBox GetCameraBounds() const;
	/** Back to the overview: the fitted top view for a level, else the map's own camera (F, Reset camera). */
	void ResetCameraToOverview();

	/** Changes on every start and stop, so UI can rebuild per fight. */
	int32 GetFightSerial() const { return FightSerial; }
	const UCombatUnitDefinition* GetUnitDefinition(int32 UnitId) const;
	/** The actor that shows a unit, or nullptr (no fight, unknown ID). Presentation only. */
	ACombatUnitActor* GetUnitActor(int32 UnitId) const;
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
	/** The first time: loads UCombatSettings::DefaultLevel as the design level, or starts a new empty one. */
	void EnsureDesignLevel();
	void NewDesignLevel();
	/** Loads Levels/<Name>.json for editing. */
	bool LoadDesignLevel(const FString& Name, FString& OutMessage);
	/** Saves the edited level as Levels/<Name>.json (the name is cleaned to letters, digits, - and _). */
	bool SaveDesignLevel(const FString& Name, FString& OutMessage);
	/**
	 * Renames a saved level file (NewName is cleaned like a save). Refused when the new name exists. If it is the
	 * level being edited, that one gets the new name too.
	 */
	bool RenameLevelFile(const FString& OldName, const FString& NewName, FString& OutMessage);
	/** Deletes Levels/<Name>.json. The edited level stays open (unsaved), even if it was that file. */
	bool DeleteLevelFile(const FString& Name, FString& OutMessage);
	/** Starts a fight from the edited level (leaves edit mode). */
	bool PlayDesignLevel(int32 Seed);
	void SetDesignSize(int32 Width, int32 Height);

	/** Also selects the unit type again (Unit and Spawn Mode). */
	void SetDesignTool(ECombatDesignTool Tool) { DesignTool = Tool; bDesignEyedropper = false; bDesignUnitSelected = true; }
	ECombatDesignTool GetDesignTool() const { return DesignTool; }
	void SetDesignUnitType(const FString& Type) { DesignUnitType = Type; bDesignUnitSelected = true; }
	/** Unit and Spawn Mode: whether a unit type is selected to place (a right click deselects it). */
	bool HasDesignUnitSelected() const { return bDesignUnitSelected; }
	/**
	 * Ctrl+click in Unit Mode (a unit) or Spawn Mode (a spawn of the selected wave): picks up the one on the position
	 * under WorldPoint (else the nearest in that cell) to move it; its type, team and rotation become the selection.
	 * The next placement puts it down (one undo step with the pick-up); a right click puts it back.
	 */
	bool PickDesignUnit(const FVector& WorldPoint);
	bool IsMovingDesignUnit() const { return DesignMovingUnit.IsSet() || DesignMovingSpawn.IsSet(); }
	const FString& GetDesignUnitType() const { return DesignUnitType; }
	/**
	 * LevelDesigner undo / redo (Ctrl+Z, Ctrl+Y or Ctrl+Shift+Z, buttons): one step per change of the edited level,
	 * a mouse stroke is one step, at most MaxDesignUndo steps. The level's name is not part of it (Save does not count).
	 */
	void UndoDesign();
	void RedoDesign();
	bool CanUndoDesign() const { return bDesignMode && !DesignUndo.IsEmpty(); }
	bool CanRedoDesign() const { return bDesignMode && !DesignRedo.IsEmpty(); }

	/** Piece tool: the catalog piece to place (its Id) and its rotation (R, Shift+R, Rotate). */
	/** Choosing another piece while moving one puts the moved piece back first. */
	/** Selects the piece to place; a wall item starts at its catalog MountHeight. */
	void SetDesignPiece(const FString& Id);
	const FString& GetDesignPiece() const { return DesignPieceId; }
	/**
	 * Ctrl+click: picks up the topmost piece under WorldPoint to move it: it leaves the level and becomes the selection
	 * (with its rotation and the Piece tool). The next placement puts it down (one undo step with the pick-up).
	 */
	bool PickDesignPiece(const FVector& WorldPoint);
	bool IsMovingDesignPiece() const { return DesignMovingPiece.IsSet(); }
	bool HasDesignPieceSelected() const { return !DesignPieceId.IsEmpty(); }
	/**
	 * Right click in edit mode: puts a moved piece, unit or spawn back, else deselects the selected piece (Build Mode) or
	 * unit type (Unit and Spawn Mode), else erases what is under WorldPoint (Build Mode: the topmost piece; Unit and
	 * Spawn Mode: the unit or spawn there). bHasPoint is false when the click missed the grid plane.
	 */
	void DesignRightClick(const FVector& WorldPoint, bool bHasPoint);
	/** Color of the next tintable piece (solid floor) placed; sRGB. */
	void SetDesignPieceColor(const FColor& Color) { DesignPieceColor = Color; }
	const FColor& GetDesignPieceColor() const { return DesignPieceColor; }
	/** Whether the selected piece takes a color (bTintable). */
	bool IsDesignPieceTintable();
	/**
	 * Eyedropper (button, key I): the next left click in the arena takes the color and the piece of the tintable floor
	 * under it instead of placing; a right click cancels.
	 */
	void SetDesignEyedropper(bool bActive) { bDesignEyedropper = bActive; }
	bool IsDesignEyedropper() const { return bDesignEyedropper; }
	/** Turns the selected piece by Steps of its layer: 45 degrees for details, 11.25 (tilt) for wall items, 90 for the others. */
	void RotateDesignPiece(int32 Steps);
	/** The selected piece's rotation in degrees. */
	float GetDesignPieceDegrees();
	/** Whether the selected piece hangs on walls (Wall layer). */
	bool IsDesignPieceWallItem();
	/** Raises (Steps > 0) or lowers the next wall item by Steps x WallItemHeightStep (PageUp / PageDown); kept for the next one. */
	void RaiseDesignWallItem(int32 Steps);
	float GetDesignWallItemHeight() const { return DesignWallItemHeight; }
	/** The settings' PieceCatalog, loaded on first use; null if none is set. */
	const UCombatPieceCatalog* GetPieceCatalog();
	/** Piece tool: shows where the selected piece goes under WorldPoint (green; red if it does not fit; orange when erasing). */
	void UpdateDesignPiecePreview(const FVector& WorldPoint, bool bErase);
	void HideDesignPiecePreview();
	void SetDesignUnitTeam(int32 Team) { DesignUnitTeam = Team; }
	int32 GetDesignUnitTeam() const { return DesignUnitTeam; }
	/** Unit and Spawn Mode: turns the start pose of the next unit or spawn by Steps of 11.25 degrees (kept for the next one). */
	void RotateDesignUnit(int32 Steps);
	float GetDesignUnitDegrees() const { return CombatLevels::GetUnitYaw(DesignUnitRotation); }
	/**
	 * Whether a unit of Type may stand on Position of Cell: the cell is in the grid and walkable, and the position's
	 * nav sub-cell is open for the type's clearance class (not against a wall or a blocking piece).
	 */
	bool IsDesignSpotFree(const FIntPoint& Cell, int32 Position, const FString& Type);
	/**
	 * Unit and Spawn Mode: a see-through ghost of the selected unit type (in the team color, in its rotation) on the
	 * position (one of 9 per cell) under WorldPoint, with a green plate there, red where it cannot stand.
	 */
	void UpdateDesignUnitGhost(const FVector& WorldPoint);
	void HideDesignUnitGhost();

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
	 * Spawn list edits (wave index, index in that wave's Spawns). Each selects the spawn's wave. A cell that is outside
	 * the grid, on a blocking piece, or already holds a spawn of that wave is refused (the level stays as it was).
	 */
	bool SetDesignSpawn(int32 WaveIndex, int32 SpawnIndex, const FCombatLevelSpawn& Spawn);
	/** Moves a spawn to the end of another wave's spawns. */
	bool MoveDesignSpawnToWave(int32 WaveIndex, int32 SpawnIndex, int32 NewWaveIndex);
	void RemoveDesignSpawn(int32 WaveIndex, int32 SpawnIndex);
	/** The next left click in the arena puts this spawn on the clicked cell; a right click cancels. Again on the same spawn cancels too. */
	void BeginDesignSpawnMove(int32 WaveIndex, int32 SpawnIndex);
	bool IsMovingDesignSpawn() const { return DesignSpawnMove.IsSet(); }
	bool IsMovingDesignSpawn(int32 WaveIndex, int32 SpawnIndex) const { return DesignSpawnMove.IsSet() && DesignSpawnMove.GetValue() == FIntPoint(WaveIndex, SpawnIndex); }
	/**
	 * Start unit edits from the spawn list (index in the level's Units). A cell outside the grid, on a blocking piece,
	 * or holding another unit is refused. Moving works as for spawns.
	 */
	bool SetDesignUnit(int32 UnitIndex, const FCombatLevelUnit& Unit);
	void RemoveDesignUnit(int32 UnitIndex);
	void BeginDesignUnitMove(int32 UnitIndex);
	bool IsMovingDesignUnit(int32 UnitIndex) const { return IsMovingDesignSpawn(INDEX_NONE, UnitIndex); }
	/** Changes with every change of the edited level (and every refused edit), so UI can rebuild. */
	int32 GetDesignRevision() const { return DesignRevision; }

	/**
	 * Mouse on the arena in edit mode. While a spawn is being moved, a place click moves it there and an erase click
	 * only cancels the move. Place: the current tool on the cell (a unit only when bStroke is false,
	 * so dragging does not drop a row of units; the same for spawns in the selected wave). Erase: removes the unit,
	 * the selected wave's spawn and the cell's wall/hedge/water.
	 */
	void DesignPaint(const FVector& WorldPoint, bool bErase, bool bStroke);

	/** Asset names of all UCombatUnitDefinition assets, sorted. */
	static TArray<FString> GetAllUnitDefinitionNames();

	/** Builds a simulation config from a level (it brings its own grid; the world's ACombatGrid only gives the origin). */
	static bool BuildSimConfigFromSource(UWorld* World, int32 Seed, const FCombatFightSource& Source, const FCombatSimSettings& Settings,
		FCombatSimConfig& OutConfig, FVector& OutGridOrigin, TArray<const UCombatUnitDefinition*>* OutDefinitions = nullptr,
		TArray<int32>* OutRotations = nullptr);

	/** A level name ("Name" or "Level: Name") to a fight source, loaded from Levels/; empty gives UCombatSettings::DefaultLevel. */
	static bool ResolveSource(const FString& Name, FCombatFightSource& OutSource);
	/** Finds a unit definition by asset name or object path. */
	static const UCombatUnitDefinition* FindUnitDefinition(const FString& NameOrPath);

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
	/** Shows the source's level in the arena (pieces, resized floor, fitted camera). */
	void ShowSourceInArena(const FCombatFightSource& Source);
	/** Moves the view camera straight above the shown grid, high enough to see all of it. */
	/** The montage tag of a unit's attack: the definition's AnimationTag, else the attack type. */
	FGameplayTag GetAttackAnimationTag(int32 UnitId, int32 AttackIndex) const;
	void FitCameraToShownGrid();
	void RestoreCamera();
	/** The overview: fitted top view for a level (bLevel), else the map's own camera. Remembers which one it showed. */
	void ShowOverview(bool bLevel);
	/** ShowOverview, but only when the kind of overview or the grid size changed, so a camera the player moved stays. */
	void ShowOverviewIfChanged(bool bLevel);
	/** Shows the edited level in the arena and rebuilds the preview units. */
	void RefreshDesignView(bool bFitCamera);
	/** Adds the last recorded level to the undo steps when the edited level differs from it (from RefreshDesignView). */
	void RecordDesignChange();
	void ResetDesignHistory();
	/** Puts a piece that is being moved back where it was (undoing the pick-up). */
	void CancelDesignPieceMove();
	/** Puts a picked-up unit or spawn back (undo of the pick-up, or adding it again after other edits). */
	void CancelDesignUnitMove();
	/** Undo / redo: shows Level as the edited level (keeping the current name). */
	void RestoreDesignLevel(FCombatLevel Level);
	/** The selected piece under WorldPoint and its definition; false without a catalog or a valid selection. */
	bool GetDesignPiecePlacement(const FVector& WorldPoint, FCombatLevelPiece& OutPiece, const FCombatPieceDefinition*& OutDefinition);
	/** Whether a unit may stand on Cell of the edited level: no blocking cell kind and no piece that blocks walking. */
	bool IsDesignCellWalkable(const FIntPoint& Cell) const;
	/** The cell and the unit position in it (0..8) under a world point. */
	void GetDesignSpot(const FVector& WorldPoint, FIntPoint& OutCell, int32& OutPosition) const;
	void DestroyDesignPreviews();
	/** Lowers the walls for the wall mode, with the units (or LevelDesigner previews) as cutaway targets. */
	void UpdateWalls();
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

	/** Indexed like SourceDefinitions: the start rotation (eighth turns) of each unit of a level; empty for setups. */
	TArray<int32> SourceRotations;

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
	FString CurrentSourceName;
	/** The level of the current fight (copied into replays). */
	TOptional<FCombatLevel> CurrentLevel;

	/** The view camera and its transform as placed in the arena, remembered before it was first fitted or moved. */
	TWeakObjectPtr<AActor> ArenaCamera;
	TOptional<FTransform> OriginalCameraTransform;
	/** The overview last shown (level or not, and the grid size), so the camera only resets when it changes. */
	bool bHasOverview = false;
	bool bOverviewIsLevel = false;
	FVector2D OverviewGridSize = FVector2D::ZeroVector;
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
	ECombatDesignTool DesignTool = ECombatDesignTool::Build;
	/** Whether DesignLevel holds a level (edited before), so edit mode goes on with it. */
	bool bHasDesignLevel = false;
	FString DesignUnitType;
	int32 DesignUnitTeam = 0;
	int32 DesignWave = INDEX_NONE;
	float DesignSpawnTime = 0.f;
	/** (wave, spawn index) of the spawn the next arena click moves; (INDEX_NONE, unit index) for a start unit. */
	TOptional<FIntPoint> DesignSpawnMove;
	static constexpr int32 MaxDesignUndo = 50;
	TArray<FCombatLevel> DesignUndo;
	TArray<FCombatLevel> DesignRedo;
	/** The edited level as last recorded; changes are measured against it. */
	FCombatLevel DesignRecorded;
	bool bHasDesignRecorded = false;
	/** Mouse strokes: a press starts a new one; changes during the same stroke merge into one undo step. */
	int32 DesignStrokeSerial = 0;
	int32 ActiveDesignStroke = INDEX_NONE;
	int32 LastRecordedStroke = INDEX_NONE;

	/** The piece being moved, as it was before the pick-up, and the stroke serial of the pick-up (also for units and spawns). */
	TOptional<FCombatLevelPiece> DesignMovingPiece;
	int32 DesignMoveStroke = INDEX_NONE;
	/** The unit or spawn being moved (Ctrl+click), as it was, and the spawn's wave. */
	TOptional<FCombatLevelUnit> DesignMovingUnit;
	TOptional<FCombatLevelSpawn> DesignMovingSpawn;
	int32 DesignMovingSpawnWave = INDEX_NONE;
	bool bDesignUnitSelected = true;

	FString DesignPieceId;
	/** In eighth turns (45 degrees); pieces that turn in quarters use half of it. */
	int32 DesignPieceRotation = 0;
	/** Wall items: the tilt (1/32 turns) and the height (cm) of the next one placed. */
	int32 DesignWallItemTilt = 0;
	float DesignWallItemHeight = 150.f;
	/** The selected piece's rotation in its layer's steps. */
	int32 GetDesignPieceSteps();

	UPROPERTY(Transient)
	TObjectPtr<UCombatPieceCatalog> PieceCatalog;
	int32 DesignRevision = 0;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ACombatUnitActor>> DesignPreviews;

	/** The unit ghost under the cursor (Unit and Spawn Mode), and the type and team it was made for. */
	UPROPERTY(Transient)
	TObjectPtr<ACombatUnitActor> DesignGhost;
	FString DesignGhostType;
	int32 DesignGhostTeam = INDEX_NONE;
	/** Start rotation (1/32 turns) of the next unit or spawn placed. */
	int32 DesignUnitRotation = 0;
	/** Nav grids of the edited level per clearance class (0..CombatNavigation::MaxClass), for IsDesignSpotFree; rebuilt per DesignRevision. */
	TArray<FCombatGridData> DesignNavGrids;
	int32 DesignNavRevision = INDEX_NONE;
	FColor DesignPieceColor = FColor::White;
	bool bDesignEyedropper = false;
	ECombatWallMode WallMode = ECombatWallMode::Cutaway;

	int32 SelectedUnitId = INDEX_NONE;
	bool bAwaitingMoveTarget = false;
	int32 FightSerial = 0;
};
