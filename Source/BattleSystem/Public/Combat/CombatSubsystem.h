// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Combat/CombatSimulation.h"
#include "CombatSubsystem.generated.h"

class ACombatProjectileActor;
class ACombatUnitActor;
class UCombatSetup;
class UCombatUnitDefinition;

BATTLESYSTEM_API DECLARE_LOG_CATEGORY_EXTERN(LogCombat, Log, All);

/**
 * Thin layer between the world and FCombatSimulation: builds a fight from the level's ACombatGrid and a
 * UCombatSetup, runs fixed steps from an accumulator, and drives the ACombatUnitActors.
 * Also registers the console commands Combat.Start, Combat.Simulate and Combat.Stop.
 */
UCLASS()
class BATTLESYSTEM_API UCombatSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual void Deinitialize() override;

	/** Starts a fight with presentation, replacing any running fight. */
	bool StartFight(int32 Seed, const UCombatSetup* Setup);
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

	/** Asset names of all UCombatSetup assets, sorted. */
	static TArray<FString> GetAllSetupNames();

	/**
	 * Builds a simulation config from a setup, using the world's ACombatGrid or, without one (or without
	 * a world), the fallback grid from UCombatSettings. Entries without a definition or outside the grid
	 * are skipped with a warning. OutDefinitions gets the definition per unit ID.
	 */
	static bool BuildSimConfig(UWorld* World, int32 Seed, const UCombatSetup& Setup, FCombatSimConfig& OutConfig,
		FVector& OutGridOrigin, TArray<const UCombatUnitDefinition*>* OutDefinitions = nullptr);

	/** Finds a setup by asset name or object path. An empty string gives the default setup from UCombatSettings. */
	static UCombatSetup* FindSetup(const FString& NameOrPath);

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void DispatchEvents();
	void SpawnProjectileActor(const FCombatEvent& Event);
	void UpdateActors(float Alpha);
	/** Debug lines and distance-map numbers, depending on the Combat.Debug console variable. */
	void DrawDebug(float Alpha) const;
	/** Range circles of area attacks (taunt), depending on the Combat.ShowRanges console variable. */
	void DrawAreaRanges(float Alpha) const;
	void ReportResult() const;
	FVector SimToWorld(const FVector2D& Local) const { return GridOrigin + FVector(Local.X, Local.Y, 0.0); }

	TUniquePtr<FCombatSimulation> Simulation;

	/** Indexed by unit ID. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ACombatUnitActor>> UnitActors;

	/** Indexed by unit ID; kept for per-attack presentation settings such as the projectile actor class. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UCombatUnitDefinition>> UnitDefinitions;

	/** Projectiles in flight, by projectile ID. */
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<ACombatProjectileActor>> ProjectileActors;

	FVector GridOrigin = FVector::ZeroVector;
	double Accumulator = 0.0;
	int32 MaxStepsPerFrame = 5;

	bool bPaused = false;
	float TimeScale = 1.f;
	float TauntRangeOverride = 0.f;
	int32 CurrentSeed = 0;
	FString CurrentSetupName;
};
