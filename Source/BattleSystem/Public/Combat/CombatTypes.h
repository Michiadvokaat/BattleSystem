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
