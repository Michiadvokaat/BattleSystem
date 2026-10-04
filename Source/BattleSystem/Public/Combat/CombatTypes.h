// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CombatTypes.generated.h"

/** Shape of an area attack. */
UENUM(BlueprintType)
enum class ECombatAreaShape : uint8
{
	/** Not an area attack. */
	None,
	/** A circle around the attacker itself; hits units whose edge is within AreaRadius of the attacker's edge. Needs no target. */
	CircleAroundSelf,
	/** A circle on the target's position; hits units whose edge is within AreaRadius of that point. */
	CircleAtTarget,
	/** A fan from the attacker towards the target: within AreaRadius (edge to edge) and ConeAngle. */
	Cone,
};

/** What a player command tells a unit to do. */
UENUM(BlueprintType)
enum class ECombatCommandType : uint8
{
	/** Walk to TargetCell without attacking or targeting; the AI takes over again on arrival. Overrides taunts. */
	Move,
	/** Use player ability AbilityIndex of the unit right away (no cooldown, no windup). */
	Ability,
	/** Start the next wave right away, also during a running wave (UnitId is ignored). Rejected when no wave is left. */
	CallWave,
};

/**
 * A player command. Commands are part of what determines a fight: the same setup, seed, settings and
 * commands always give the same fight. They run at the start of their Tick, in the order they were given.
 */
USTRUCT(BlueprintType)
struct FCombatCommand
{
	GENERATED_BODY()

	/** Simulation tick at whose start the command runs (ticks start at 1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Command", meta = (ClampMin = 1))
	int32 Tick = 1;

	/** Unit ID: the index of the unit among the setup's valid entries. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Command", meta = (ClampMin = 0))
	int32 UnitId = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Command")
	ECombatCommandType Type = ECombatCommandType::Move;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Command", meta = (EditCondition = "Type == ECombatCommandType::Move"))
	FIntPoint TargetCell = FIntPoint::ZeroValue;

	/** Index in the unit definition's PlayerAbilities. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Command", meta = (ClampMin = 0, EditCondition = "Type == ECombatCommandType::Ability"))
	int32 AbilityIndex = 0;
};
