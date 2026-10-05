// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatCamera.h"

namespace CombatCamera
{
	void Orbit(FVector& Location, FRotator& Rotation, const FVector& Pivot, double DeltaYaw, double DeltaPitch,
		double MinPitch, double MaxPitch)
	{
		FRotator NewRotation(FMath::Clamp(Rotation.Pitch + DeltaPitch, MinPitch, MaxPitch), Rotation.Yaw + DeltaYaw, 0.0);
		// The pivot's offset in the camera's own frame stays the same.
		const FVector LocalOffset = Rotation.UnrotateVector(Location - Pivot);
		Location = Pivot + NewRotation.RotateVector(LocalOffset);
		Rotation = NewRotation;
	}

	void Clamp(FVector& Location, FRotator& Rotation, const FBox& Bounds, double MinPitch, double MaxPitch)
	{
		Location = FVector(
			FMath::Clamp(Location.X, Bounds.Min.X, Bounds.Max.X),
			FMath::Clamp(Location.Y, Bounds.Min.Y, Bounds.Max.Y),
			FMath::Clamp(Location.Z, Bounds.Min.Z, Bounds.Max.Z));
		Rotation = FRotator(FMath::Clamp(Rotation.Pitch, MinPitch, MaxPitch), Rotation.Yaw, 0.0);
	}

	bool RayToPlane(const FVector& Origin, const FVector& Direction, double Height, FVector& OutPoint)
	{
		if (FMath::IsNearlyZero(Direction.Z))
		{
			return false;
		}
		const double Distance = (Height - Origin.Z) / Direction.Z;
		if (Distance < 0.0)
		{
			return false;
		}
		OutPoint = Origin + Direction * Distance;
		return true;
	}
}
