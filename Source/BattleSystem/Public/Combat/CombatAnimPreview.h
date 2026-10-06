// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CombatAnimPreview.generated.h"

class ACombatUnitActor;
class UCombatUnitDefinition;

/**
 * Animation tuning without PIE: placed in a map, it shows unit figures that walk back and forth in the editor viewport
 * at their definition's MoveSpeed, with their look and AnimSet, as in a fight. MoveSpeed and LocomotionRate are read every
 * frame and a recompiled AnimBP or edited blend space shows at once; after changing a look, press Rebuild.
 * Units stand side by side along the actor's Y axis and walk along its X axis. The figures are transient (not saved).
 * Presentation only: no simulation.
 */
UCLASS()
class BATTLESYSTEM_API ACombatAnimPreview : public AActor
{
	GENERATED_BODY()

public:
	ACombatAnimPreview();

	/** The units to show; empty = every unit definition. */
	UPROPERTY(EditAnywhere, Category = "Preview")
	TArray<TObjectPtr<UCombatUnitDefinition>> Definitions;

	/** Seed of the looks' random picks (the figure in row n uses Seed + n). */
	UPROPERTY(EditAnywhere, Category = "Preview")
	int32 Seed = 42;

	UPROPERTY(EditAnywhere, Category = "Preview", meta = (ClampMin = 0, Units = "cm"))
	float PathLength = 800.f;

	/** Distance between the rows. */
	UPROPERTY(EditAnywhere, Category = "Preview", meta = (ClampMin = 10, Units = "cm"))
	float Spacing = 150.f;

	/** Time standing still at each end of the path (shows stopping, idle and starting). */
	UPROPERTY(EditAnywhere, Category = "Preview", meta = (ClampMin = 0, Units = "s"))
	float StopTime = 1.5f;

	/** Speed for every unit instead of its MoveSpeed; 0 = the definition's MoveSpeed. */
	UPROPERTY(EditAnywhere, Category = "Preview", meta = (ClampMin = 0, Units = "cm/s"))
	float SpeedOverride = 0.f;

	/** Slow motion, like the control panel's speed buttons: scales the walking and the animation. */
	UPROPERTY(EditAnywhere, Category = "Preview", meta = (ClampMin = 0, ClampMax = 4))
	float TimeScale = 1.f;

	UPROPERTY(EditAnywhere, Category = "Preview")
	bool bPaused = false;

	/** Spawns the figures again (after changing a look or the list). */
	UFUNCTION(CallInEditor, Category = "Preview")
	void Rebuild();

	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }
	virtual void Destroyed() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	struct FPreviewUnit
	{
		TWeakObjectPtr<ACombatUnitActor> Actor;
		TWeakObjectPtr<const UCombatUnitDefinition> Definition;
		/** Along the path, 0..PathLength. */
		float Distance = 0.f;
		/** +1 out, -1 back. */
		float Direction = 1.f;
		float StopLeft = 0.f;
	};

	void ClearUnits();
	TArray<const UCombatUnitDefinition*> GetShownDefinitions() const;

	TArray<FPreviewUnit> Units;
	/** Built at least once, so an empty list (no definitions) does not rebuild every frame. */
	bool bBuilt = false;
};
