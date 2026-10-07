// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "CombatSettings.generated.h"

class UCombatCueTable;
class UMaterialInterface;
class UCombatPieceCatalog;
class UDataTable;

/** How an active effect is shown on a unit. */
USTRUCT()
struct FCombatStatusIcon
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Status", meta = (Categories = "Effect"))
	FGameplayTag EffectTag;

	/** Short text, for example "T". A stack count is added after it (S2). */
	UPROPERTY(EditAnywhere, Category = "Status")
	FString Label;

	UPROPERTY(EditAnywhere, Category = "Status")
	FLinearColor Color = FLinearColor::White;
};

/** How threat decreases over time. */
UENUM()
enum class ECombatThreatDecayMode : uint8
{
	/** Threat halves every ThreatHalfLife seconds: large values drop fast, small ones linger. */
	HalfLife,
	/** ThreatDecayPerSecond is subtracted every second: whoever built up a lot keeps it long. */
	Linear,
};

/** Combat tuning values. Project Settings > Game > Combat, saved to DefaultGame.ini. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Combat"))
class BATTLESYSTEM_API UCombatSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	/** Simulation steps per second. */
	UPROPERTY(Config, EditAnywhere, Category = "Simulation", meta = (ClampMin = 1, ClampMax = 120))
	int32 TickRate = 20;

	/** Maximum simulation steps per frame; protects against a "spiral of death" on slow frames. */
	UPROPERTY(Config, EditAnywhere, Category = "Simulation", meta = (ClampMin = 1))
	int32 MaxStepsPerFrame = 5;

	/** A fight ends as a time-out after this many seconds (rounded to ticks). */
	UPROPERTY(Config, EditAnywhere, Category = "Simulation", meta = (ClampMin = 1, Units = "s"))
	float FightTimeLimit = 120.f;

	/** Once a unit first reaches attack range, its first attack waits a random 0..this many seconds, drawn from the fight's seed. */
	UPROPERTY(Config, EditAnywhere, Category = "Simulation", meta = (ClampMin = 0, Units = "s"))
	float MaxFirstAttackDelay = 0.5f;

	/**
	 * Seconds a unit stands still after it takes damage (its hit reaction), rounded to ticks; 0 = none. A new hit restarts
	 * it. While standing still (also during windup and a skill's Recovery) it is not pushed by other units.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Simulation", meta = (ClampMin = 0, Units = "s"))
	float HitStagger = 0.2f;

	/** Pause before the first wave of a level, and between a cleared wave and the next (rounded to ticks). */
	UPROPERTY(Config, EditAnywhere, Category = "Waves", meta = (ClampMin = 0.05, Units = "s"))
	float WavePauseSeconds = 5.f;

	/** How often the per-team distance maps (and so the targets) are rebuilt; they are also rebuilt after every death. */
	UPROPERTY(Config, EditAnywhere, Category = "Movement", meta = (ClampMin = 0.05, Units = "s"))
	float RetargetInterval = 0.25f;

	/** How many cells ahead on its route a unit looks for the farthest visible point to steer to. */
	UPROPERTY(Config, EditAnywhere, Category = "Movement", meta = (ClampMin = 1, ClampMax = 32))
	int32 PathLookaheadCells = 8;

	/** Fraction of the overlap between two units that is pushed apart per tick. */
	UPROPERTY(Config, EditAnywhere, Category = "Movement", meta = (ClampMin = 0, ClampMax = 1))
	float SeparationStrength = 0.5f;

	/**
	 * Units keep their radius from walls, blocks and edge walls, but at most this much, so every unit still fits through
	 * an opening of one cell (keep it below half the cell size). 0 = only the center is kept out of walls.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Movement", meta = (ClampMin = 0, Units = "cm"))
	float WallClearance = 45.f;

	/** How threat decreases over time. */
	UPROPERTY(Config, EditAnywhere, Category = "Threat")
	ECombatThreatDecayMode ThreatDecayMode = ECombatThreatDecayMode::HalfLife;

	/** HalfLife mode: seconds in which threat halves. */
	UPROPERTY(Config, EditAnywhere, Category = "Threat", meta = (ClampMin = 0.1, Units = "s", EditCondition = "ThreatDecayMode == ECombatThreatDecayMode::HalfLife"))
	float ThreatHalfLife = 4.f;

	/** Linear mode: threat subtracted per second (down to 0). */
	UPROPERTY(Config, EditAnywhere, Category = "Threat", meta = (ClampMin = 0, EditCondition = "ThreatDecayMode == ECombatThreatDecayMode::Linear"))
	float ThreatDecayPerSecond = 5.f;

	/** Threat below this does not count for targeting. */
	UPROPERTY(Config, EditAnywhere, Category = "Threat", meta = (ClampMin = 0))
	float ThreatThreshold = 5.f;

	/** A unit on a threat target only switches to an enemy with this many times more threat (1.2 = 20% more). */
	UPROPERTY(Config, EditAnywhere, Category = "Threat", meta = (ClampMin = 1))
	float ThreatSwitchRatio = 1.2f;

	/** A unit on a nearest target only switches to an enemy that is this much closer. */
	UPROPERTY(Config, EditAnywhere, Category = "Threat", meta = (ClampMin = 0, Units = "cm"))
	float RetargetDistanceMargin = 150.f;

	/** The team the player commands (unit list, commands). */
	UPROPERTY(Config, EditAnywhere, Category = "Player")
	int32 PlayerTeam = 0;

	/** Ticks between giving a command and running it (3 = 0.15 s); a fixed delay keeps lockstep multiplayer possible. */
	UPROPERTY(Config, EditAnywhere, Category = "Player", meta = (ClampMin = 1, ClampMax = 60))
	int32 CommandDelayTicks = 3;

	/** Replays store a checksum every this many ticks, to show where a different replay starts to differ. */
	UPROPERTY(Config, EditAnywhere, Category = "Player", meta = (ClampMin = 1))
	int32 ReplayCheckpointInterval = 20;

	/** Start a fight automatically when an arena with ACombatGameMode begins play. */
	UPROPERTY(Config, EditAnywhere, Category = "Arena")
	bool bAutoStartFight = false;

	/**
	 * LevelDesigner level (Levels/<name>.json, without .json) loaded when play starts: the control panel and the auto-start
	 * play it, and the Combat.* commands without level= use it. Empty or missing: a new empty level (the commands fail).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Arena")
	FString DefaultLevel;

	/** Seed used by the auto-start. */
	UPROPERTY(Config, EditAnywhere, Category = "Arena")
	int32 DefaultSeed = 42;

	/** Unit types (FCombatUnitRow), heroes and enemies; levels name them by row name. Filled from Data/Units.json (Scripts/ImportCombatData.py). */
	UPROPERTY(Config, EditAnywhere, Category = "Units", meta = (RequiredAssetDataTags = "RowStructure=/Script/BattleSystem.CombatUnitRow"))
	TSoftObjectPtr<UDataTable> UnitTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/DT_Units.DT_Units")));

	/** Skills (FCombatSkillRow) that units own by row name. Filled from Data/Skills.json (Scripts/ImportCombatData.py). */
	UPROPERTY(Config, EditAnywhere, Category = "Units", meta = (RequiredAssetDataTags = "RowStructure=/Script/BattleSystem.CombatSkillRow"))
	TSoftObjectPtr<UDataTable> SkillTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/DT_Skills.DT_Skills")));

	/** Row names of the skill table, sorted (the dropdown of FCombatUnitRow::Skills). */
	UFUNCTION()
	static TArray<FName> GetSkillRowNames();

	/** Row names of the unit table, sorted (the dropdown of ACombatAnimPreview::UnitTypes). */
	UFUNCTION()
	static TArray<FName> GetUnitRowNames();

	/** Maps cue tags to VFX, sound and debug colors. */
	UPROPERTY(Config, EditAnywhere, Category = "Presentation")
	TSoftObjectPtr<UCombatCueTable> CueTable;

	/** Pieces the LevelDesigner can place (walls, floors, furniture); shown levels take their meshes from it. */
	UPROPERTY(Config, EditAnywhere, Category = "Arena")
	TSoftObjectPtr<UCombatPieceCatalog> PieceCatalog = TSoftObjectPtr<UCombatPieceCatalog>(FSoftObjectPath(TEXT("/Game/Environment/DA_PieceCatalog.DA_PieceCatalog")));

	/**
	 * LevelDesigner: the see-through material of the unit ghost under the cursor (Unit and Spawn Mode). It replaces
	 * every material of the unit; vector parameter Color gets the team color, scalar Opacity DesignGhostOpacity.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Presentation")
	TSoftObjectPtr<UMaterialInterface> DesignGhostMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Materials/M_DesignGhost.M_DesignGhost")));

	UPROPERTY(Config, EditAnywhere, Category = "Presentation", meta = (ClampMin = 0, ClampMax = 1))
	float DesignGhostOpacity = 0.45f;

	/** Below this speed a unit counts as standing still for its AnimBP (UCombatAnimInstance::bIsMoving). */
	UPROPERTY(Config, EditAnywhere, Category = "Animation", meta = (ClampMin = 0, Units = "cm/s"))
	float LocomotionMovingThreshold = 5.f;

	/** Limits on UCombatAnimInstance::LocomotionPlayRate (after the unit's LocomotionRate). */
	UPROPERTY(Config, EditAnywhere, Category = "Animation", meta = (ClampMin = 0.01))
	float LocomotionMinPlayRate = 0.5f;

	UPROPERTY(Config, EditAnywhere, Category = "Animation", meta = (ClampMin = 0.01))
	float LocomotionMaxPlayRate = 2.f;

	/**
	 * How much a look's height scale changes the stride: locomotion uses Speed / Lerp(1, height scale, this), so a taller
	 * figure takes longer steps. 0 = ignore the size, 1 = fully in proportion.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Animation", meta = (ClampMin = 0, ClampMax = 1))
	float LocomotionScaleCompensation = 1.f;

	/**
	 * Tintable pieces (solid floors) get a dynamic instance of this material on every slot, with vector parameter
	 * "Color" = the piece color. The engine's BasicShapeMaterial has it (the engine plane itself uses WorldGridMaterial).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Arena")
	TSoftObjectPtr<UMaterialInterface> TintMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")));

	/** LevelDesigner: how much one press (PageUp / PageDown, Up / Down) raises or lowers a wall item. */
	UPROPERTY(Config, EditAnywhere, Category = "Arena", meta = (ClampMin = 1, Units = "cm"))
	float WallItemHeightStep = 10.f;

	/** LevelDesigner: the color swatches for tintable pieces (solid floors); any other color comes from the color picker. */
	UPROPERTY(Config, EditAnywhere, Category = "Arena")
	TArray<FColor> FloorColors = { FColor(235, 235, 230), FColor(160, 160, 160), FColor(70, 70, 75), FColor(190, 150, 105),
		FColor(120, 80, 50), FColor(200, 70, 60), FColor(80, 140, 75), FColor(70, 110, 180), FColor(230, 200, 90) };

	/** Height of border pieces (walls, windows, door frames) while they are lowered (walls Cutaway / Down, key V). */
	UPROPERTY(Config, EditAnywhere, Category = "Presentation", meta = (ClampMin = 1, Units = "cm"))
	float LowWallHeight = 40.f;

	/** Cutaway: a wall goes down when it hides this point of a unit (its feet + this height) from the camera. */
	UPROPERTY(Config, EditAnywhere, Category = "Presentation", meta = (ClampMin = 0, Units = "cm"))
	float CutawayTargetHeight = 60.f;

	/** How long the flash of an area attack going off stays visible. */
	UPROPERTY(Config, EditAnywhere, Category = "Presentation", meta = (ClampMin = 0, Units = "s"))
	float AreaPulseDuration = 0.4f;

	/** Outline of a telegraphed area until it goes off. */
	UPROPERTY(Config, EditAnywhere, Category = "Presentation")
	FLinearColor TelegraphColor = FLinearColor(1.f, 0.5f, 0.f);

	/** Flash color of an area attack without a cue in the cue table (taunts use TauntColor). */
	UPROPERTY(Config, EditAnywhere, Category = "Presentation")
	FLinearColor DefaultAreaColor = FLinearColor(1.f, 0.15f, 0.f);

	UPROPERTY(Config, EditAnywhere, Category = "Presentation")
	FLinearColor TauntColor = FLinearColor(1.f, 0.f, 1.f);

	/** Labels for active effects on units. An effect not listed shows the first letter of its tag's last part, in white. */
	UPROPERTY(Config, EditAnywhere, Category = "Presentation")
	TArray<FCombatStatusIcon> StatusIcons;

	/** Unit color per team index (wraps around). */
	UPROPERTY(Config, EditAnywhere, Category = "Presentation")
	TArray<FLinearColor> TeamColors;

	/** Rotation per mouse pixel while looking around (RMB drag) or orbiting (Alt+LMB drag). */
	UPROPERTY(Config, EditAnywhere, Category = "Camera", meta = (ClampMin = 0.01, Units = "deg"))
	float CameraLookSpeed = 0.2f;

	/** Flying speed with WASD/QE while RMB is held (Shift = 3x; the mouse wheel scales it while flying). */
	UPROPERTY(Config, EditAnywhere, Category = "Camera", meta = (ClampMin = 10, Units = "cm/s"))
	float CameraFlySpeed = 1500.f;

	/** Flying speed factor while Shift is held. */
	UPROPERTY(Config, EditAnywhere, Category = "Camera", meta = (ClampMin = 1))
	float CameraFastMultiplier = 3.f;

	/** One wheel step moves this fraction of the distance to the point under the cursor. */
	UPROPERTY(Config, EditAnywhere, Category = "Camera", meta = (ClampMin = 0.01, ClampMax = 0.9))
	float CameraZoomStep = 0.15f;

	/** A right click that moves less than this is a click (cancel); more is a camera drag. */
	UPROPERTY(Config, EditAnywhere, Category = "Camera", meta = (ClampMin = 0))
	float CameraDragThreshold = 4.f;

	/** How far the camera may go past the edges of the shown grid. */
	UPROPERTY(Config, EditAnywhere, Category = "Camera", meta = (ClampMin = 0, Units = "cm"))
	float CameraBoundsMargin = 1500.f;

	/** Lowest and highest camera position above the grid. */
	UPROPERTY(Config, EditAnywhere, Category = "Camera", meta = (ClampMin = 0, Units = "cm"))
	float CameraMinHeight = 100.f;

	UPROPERTY(Config, EditAnywhere, Category = "Camera", meta = (ClampMin = 0, Units = "cm"))
	float CameraMaxHeight = 8000.f;

	/** Pitch limits: -90 is straight down, 0 is level with the horizon. */
	UPROPERTY(Config, EditAnywhere, Category = "Camera", meta = (ClampMin = -90, ClampMax = 90, Units = "deg"))
	float CameraMinPitch = -90.f;

	UPROPERTY(Config, EditAnywhere, Category = "Camera", meta = (ClampMin = -90, ClampMax = 90, Units = "deg"))
	float CameraMaxPitch = 0.f;

	FLinearColor GetTeamColor(int32 Team) const;
	int32 SecondsToTicks(float Seconds) const { return FMath::RoundToInt32(Seconds * TickRate); }
};
