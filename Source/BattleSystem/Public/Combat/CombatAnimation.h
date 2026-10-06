// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "CombatAnimation.generated.h"

class UAnimMontage;
class UBlendSpace;
class UCombatAnimInstance;

/** A montage for an action: an attack, a hit reaction, the death. */
USTRUCT(BlueprintType)
struct FCombatAnimAction
{
	GENERATED_BODY()

	/**
	 * Anim.Hit, Anim.Death, an attack's AnimationTag (Anim.Throw), or an attack type (Attack.Melee) for attacks
	 * without an AnimationTag. A tag without an entry uses the entry of its nearest parent tag.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Action")
	FGameplayTag Tag;

	/** Played in the AnimBP's DefaultSlot. In an attack, the notify "Impact" is lined up with the simulation's hit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Action")
	TObjectPtr<UAnimMontage> Montage;
};

/**
 * The animations of one skeleton: the AnimBP, locomotion and the montages per action. A UCombatAppearance points to
 * it, so all looks on the same skeleton share one set. Presentation only.
 */
UCLASS(BlueprintType)
class BATTLESYSTEM_API UCombatAnimSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Animation Blueprint with UCombatAnimInstance as parent (Locomotion by Speed, then the DefaultSlot). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TSubclassOf<UCombatAnimInstance> AnimClass;

	/** Idle to walk to run, by speed in cm/s. Passed to the AnimBP as Locomotion. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UBlendSpace> Locomotion;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TArray<FCombatAnimAction> Actions;

	/** Played now and then while the unit stands still and plays nothing else. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Idle")
	TArray<TObjectPtr<UAnimMontage>> IdleBreaks;

	/** Time standing still before an idle break: random between these. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Idle", meta = (ClampMin = 0, Units = "s"))
	float MinIdleBreakInterval = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Idle", meta = (ClampMin = 0, Units = "s"))
	float MaxIdleBreakInterval = 15.f;

	/** How fast the figure turns to a new facing; 0 = at once. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = 0, Units = "deg"))
	float TurnRate = 720.f;

	/** How long a dead figure stays lying after its death montage before it disappears. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Death", meta = (ClampMin = 0, Units = "s"))
	float CorpseDuration = 4.f;

	/** Limits on the speed-up or slow-down that lines an attack's Impact up with the hit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = 0.01))
	float MinPlayRate = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = 0.01))
	float MaxPlayRate = 4.f;

	/** The montage for a tag: an exact entry, else the entry of the nearest parent tag; nullptr if none. */
	UAnimMontage* FindMontage(FGameplayTag Tag) const;

	/** Play rate that puts a notify at ImpactTime (montage seconds) at WindupSeconds; 1 without either. Clamped. */
	float GetPlayRateForImpact(float ImpactTime, float WindupSeconds) const;

	/** Time in the montage of its first notify named "Impact"; negative if it has none. */
	static float FindImpactTime(const UAnimMontage* Montage);
};

/**
 * Parent class for the units' Animation Blueprints. ACombatUnitActor fills the variables every frame (inputs only);
 * the AnimGraph decides what to play with them: for example a state machine Idle <-> Locomotion on bIsMoving, a Blend
 * Space Player on Locomotion at (LocomotionX, LocomotionY) with Play Rate LocomotionPlayRate, then the DefaultSlot for
 * the montages. A death montage is held at its end.
 */
UCLASS()
class BATTLESYSTEM_API UCombatAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** Simulation speed in cm/s (the speed buttons and pause act on the play rate, not on this). */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float Speed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	TObjectPtr<UBlendSpace> Locomotion;

	/**
	 * Coordinates for the Locomotion blend space, by the names of its axes: with axes "Forward" and "Right" the local
	 * velocity (LocalVelocity at the standard size), else StrideSpeed on the axis named "Speed" (X if none) and 0 on the
	 * other. Wire them to the Blend Space Player's X and Y, so one AnimBP works for every blend space.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float LocomotionX = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float LocomotionY = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	bool bDead = false;

	/** Speed is above UCombatSettings::LocomotionMovingThreshold. */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	bool bIsMoving = false;

	/** Velocity relative to the figure's facing (cm/s): X forward, Y to its right. Real size; see StrideSpeed. */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	FVector2D LocalVelocity = FVector2D::ZeroVector;

	/** Direction of LocalVelocity in degrees: 0 forward, 90 to the right, -90 to the left, +-180 backwards. */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float Direction = 0.f;

	/** The unit definition's MoveSpeed (cm/s): the speed it walks at without slows or hastes. */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float MoveSpeed = 0.f;

	/** Speed / MoveSpeed (0 without a MoveSpeed). */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float SpeedRatio = 0.f;

	/** The look's mesh scale (UCombatAppearance::GetMeshScale); 1 without a look. */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	FVector MeshScale = FVector::OneVector;

	/**
	 * Speed at the standard figure size (ComputeStrideSpeed): Speed / Lerp(1, MeshScale.Z, LocomotionScaleCompensation).
	 * LocomotionX/Y and LocomotionPlayRate use it, so a taller figure walks with longer, slower steps.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float StrideSpeed = 0.f;

	/** The unit definition's LocomotionRate (tuning per unit type). */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float LocomotionRate = 1.f;

	/**
	 * Play rate that keeps the feet on the ground (ComputeLocomotionPlayRate): 1 between the slowest and fastest moving
	 * sample of Locomotion (on its speed axis, or the distance from the middle with Forward/Right axes; there the blend
	 * space blends the stride), StrideSpeed / that sample beyond them,
	 * times LocomotionRate, clamped by the settings. 1 while standing still. Wire it to the Blend Space Player's Play Rate.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float LocomotionPlayRate = 1.f;

	/** Speeds of the slowest and fastest sample of Locomotion above the moving threshold (0 if none); read every frame. */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float SlowestSampleSpeed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float FastestSampleSpeed = 0.f;

	/** Sets the blend space and finds its axes (Speed, or Forward and Right). */
	void SetLocomotion(UBlendSpace* InLocomotion);

	/** Per unit type: the definition's MoveSpeed and LocomotionRate. */
	void SetTuning(float InMoveSpeed, float InLocomotionRate);

	void SetMeshScale(const FVector& InMeshScale) { MeshScale = InMeshScale; }

	/** Sets the velocity relative to the facing (X forward, Y right) and everything derived from it. */
	void SetLocalVelocity(const FVector2D& InLocalVelocity);

	/** Axis of a blend space named "Speed" (any case): 0 = X, 1 = Y; 0 if none or no blend space. */
	static int32 FindSpeedAxis(const UBlendSpace* BlendSpace);

	/** Axis of a blend space with this name (any case): 0 = X, 1 = Y; INDEX_NONE if none or no blend space. */
	static int32 FindAxis(const UBlendSpace* BlendSpace, const TCHAR* Name);

	/** A world (or grid) velocity seen from a figure facing YawDegrees: X forward, Y to its right. */
	static FVector2D ToLocalVelocity(const FVector2D& WorldVelocity, float YawDegrees);

	/**
	 * Blend space coordinates for a local velocity: on InForwardAxis and InRightAxis when both are set, else its length
	 * on InSpeedAxis (the other coordinate 0).
	 */
	static FVector2D GetBlendCoordinates(const FVector2D& InLocalVelocity, int32 InSpeedAxis, int32 InForwardAxis, int32 InRightAxis);

	/** InSpeed / Lerp(1, HeightScale, Compensation); InSpeed if that divisor is not positive. */
	static float ComputeStrideSpeed(float InSpeed, float HeightScale, float Compensation);

	/** The slowest and fastest of Speeds above Threshold; false (both 0) if there is none. */
	static bool FindMovingSampleRange(TConstArrayView<float> Speeds, float Threshold, float& OutSlowest, float& OutFastest);

	/**
	 * 1 below Threshold (standing still); else Speed / Fastest above Fastest, Speed / Slowest below Slowest, 1 between them
	 * (Fastest 0: 1), then times Rate and clamped to MinRate..MaxRate.
	 */
	static float ComputeLocomotionPlayRate(float InSpeed, float Threshold, float Slowest, float Fastest, float Rate, float MinRate, float MaxRate);

	/** Plays the death montage (if any) and keeps its last pose. */
	void PlayDeath(UAnimMontage* Montage);

protected:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> DeathMontage;

	int32 SpeedAxis = 0;
	/** Both set: a velocity blend space (Forward/Right axes). */
	int32 ForwardAxis = INDEX_NONE;
	int32 RightAxis = INDEX_NONE;
};
