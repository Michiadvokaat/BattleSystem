// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Math for the free arena camera (ACombatPlayerController). Presentation only. */
namespace CombatCamera
{
	/**
	 * Turns the camera around Pivot: the yaw and pitch change, and the location moves with them, so what the camera
	 * saw at the pivot stays in the same place on screen. The pitch stays within the limits.
	 */
	BATTLESYSTEM_API void Orbit(FVector& Location, FRotator& Rotation, const FVector& Pivot, double DeltaYaw, double DeltaPitch,
		double MinPitch, double MaxPitch);

	/** Keeps the location inside Bounds and the pitch within the limits; the roll is always 0. */
	BATTLESYSTEM_API void Clamp(FVector& Location, FRotator& Rotation, const FBox& Bounds, double MinPitch, double MaxPitch);

	/** Where a ray hits the horizontal plane at Height; false if it runs parallel or points away. */
	BATTLESYSTEM_API bool RayToPlane(const FVector& Origin, const FVector& Direction, double Height, FVector& OutPoint);
}
