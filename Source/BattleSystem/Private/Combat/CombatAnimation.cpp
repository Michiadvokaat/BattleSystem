// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatAnimation.h"
#include "Animation/AnimMontage.h"
#include "Animation/BlendSpace.h"
#include "Combat/CombatSettings.h"

UAnimMontage* UCombatAnimSet::FindMontage(FGameplayTag Tag) const
{
	if (!Tag.IsValid())
	{
		return nullptr;
	}
	// The longest entry tag that Tag matches is the nearest one (Anim.Throw.Heavy -> Anim.Throw -> Anim).
	const FCombatAnimAction* Best = nullptr;
	for (const FCombatAnimAction& Action : Actions)
	{
		if (Action.Montage && Action.Tag.IsValid() && Tag.MatchesTag(Action.Tag)
			&& (!Best || Action.Tag.GetGameplayTagParents().Num() > Best->Tag.GetGameplayTagParents().Num()))
		{
			Best = &Action;
		}
	}
	return Best ? Best->Montage.Get() : nullptr;
}

float UCombatAnimSet::GetPlayRateForImpact(float ImpactTime, float WindupSeconds) const
{
	const float Rate = ImpactTime > 0.f && WindupSeconds > 0.f ? ImpactTime / WindupSeconds : 1.f;
	return FMath::Clamp(Rate, MinPlayRate, FMath::Max(MinPlayRate, MaxPlayRate));
}

float UCombatAnimSet::FindImpactTime(const UAnimMontage* Montage)
{
	if (Montage)
	{
		for (const FAnimNotifyEvent& Notify : Montage->Notifies)
		{
			if (Notify.NotifyName == TEXT("Impact"))
			{
				return Notify.GetTriggerTime();
			}
		}
	}
	return -1.f;
}

void UCombatAnimInstance::SetLocomotion(UBlendSpace* InLocomotion)
{
	Locomotion = InLocomotion;
	SpeedAxis = FindSpeedAxis(InLocomotion);
	ForwardAxis = FindAxis(InLocomotion, TEXT("Forward"));
	RightAxis = FindAxis(InLocomotion, TEXT("Right"));
	SetLocalVelocity(LocalVelocity);
}

void UCombatAnimInstance::SetTuning(float InMoveSpeed, float InLocomotionRate)
{
	MoveSpeed = InMoveSpeed;
	LocomotionRate = InLocomotionRate;
}

void UCombatAnimInstance::SetLocalVelocity(const FVector2D& InLocalVelocity)
{
	const UCombatSettings* Settings = GetDefault<UCombatSettings>();
	LocalVelocity = InLocalVelocity;
	Speed = InLocalVelocity.Size();
	Direction = Speed > UE_KINDA_SMALL_NUMBER ? FMath::RadiansToDegrees(FMath::Atan2(InLocalVelocity.Y, InLocalVelocity.X)) : 0.f;
	StrideSpeed = ComputeStrideSpeed(Speed, MeshScale.Z, Settings->LocomotionScaleCompensation);
	const FVector2D StrideVelocity = Speed > UE_KINDA_SMALL_NUMBER ? InLocalVelocity * (StrideSpeed / Speed) : FVector2D::ZeroVector;
	const FVector2D Coordinates = GetBlendCoordinates(StrideVelocity, SpeedAxis, ForwardAxis, RightAxis);
	LocomotionX = Coordinates.X;
	LocomotionY = Coordinates.Y;

	bIsMoving = Speed > Settings->LocomotionMovingThreshold;
	SpeedRatio = MoveSpeed > 0.f ? Speed / MoveSpeed : 0.f;

	// The samples are read every frame, so a blend space edited in the editor counts at once (a few samples). In a
	// velocity blend space a sample's speed is its distance from the middle.
	const bool bVelocitySpace = ForwardAxis != INDEX_NONE && RightAxis != INDEX_NONE;
	TArray<float, TInlineAllocator<8>> SampleSpeeds;
	if (Locomotion)
	{
		for (const FBlendSample& Sample : Locomotion->GetBlendSamples())
		{
			SampleSpeeds.Add(bVelocitySpace ? FVector2D(Sample.SampleValue[ForwardAxis], Sample.SampleValue[RightAxis]).Size()
				: Sample.SampleValue[SpeedAxis]);
		}
	}
	FindMovingSampleRange(SampleSpeeds, Settings->LocomotionMovingThreshold, SlowestSampleSpeed, FastestSampleSpeed);
	// Moving or not goes by the real speed; the stride by the speed at the standard size.
	LocomotionPlayRate = bIsMoving ? ComputeLocomotionPlayRate(StrideSpeed, 0.f, SlowestSampleSpeed, FastestSampleSpeed,
		LocomotionRate, Settings->LocomotionMinPlayRate, Settings->LocomotionMaxPlayRate) : 1.f;
}

float UCombatAnimInstance::ComputeStrideSpeed(float InSpeed, float HeightScale, float Compensation)
{
	const float Divisor = FMath::Lerp(1.f, HeightScale, FMath::Clamp(Compensation, 0.f, 1.f));
	return Divisor > UE_KINDA_SMALL_NUMBER ? InSpeed / Divisor : InSpeed;
}

bool UCombatAnimInstance::FindMovingSampleRange(TConstArrayView<float> Speeds, float Threshold, float& OutSlowest, float& OutFastest)
{
	OutSlowest = 0.f;
	OutFastest = 0.f;
	bool bFound = false;
	for (const float SampleSpeed : Speeds)
	{
		if (SampleSpeed > Threshold)
		{
			OutSlowest = bFound ? FMath::Min(OutSlowest, SampleSpeed) : SampleSpeed;
			OutFastest = bFound ? FMath::Max(OutFastest, SampleSpeed) : SampleSpeed;
			bFound = true;
		}
	}
	return bFound;
}

float UCombatAnimInstance::ComputeLocomotionPlayRate(float InSpeed, float Threshold, float Slowest, float Fastest, float Rate, float MinRate, float MaxRate)
{
	if (InSpeed <= Threshold)
	{
		return 1.f;
	}
	float Base = 1.f;
	if (Fastest > 0.f && InSpeed > Fastest)
	{
		Base = InSpeed / Fastest;
	}
	else if (Slowest > 0.f && InSpeed < Slowest)
	{
		Base = InSpeed / Slowest;
	}
	return FMath::Clamp(Base * Rate, MinRate, FMath::Max(MinRate, MaxRate));
}

int32 UCombatAnimInstance::FindSpeedAxis(const UBlendSpace* BlendSpace)
{
	const int32 Axis = FindAxis(BlendSpace, TEXT("Speed"));
	return Axis == INDEX_NONE ? 0 : Axis;
}

int32 UCombatAnimInstance::FindAxis(const UBlendSpace* BlendSpace, const TCHAR* Name)
{
	if (BlendSpace)
	{
		// A 1D blend space only has X.
		for (int32 Axis = 0; Axis < 2; ++Axis)
		{
			if (BlendSpace->GetBlendParameter(Axis).DisplayName.Equals(Name, ESearchCase::IgnoreCase))
			{
				return Axis;
			}
		}
	}
	return INDEX_NONE;
}

FVector2D UCombatAnimInstance::ToLocalVelocity(const FVector2D& WorldVelocity, float YawDegrees)
{
	// UE yaw: 0 = +X, 90 = +Y, so the figure's right is its forward turned 90 degrees.
	const float Yaw = FMath::DegreesToRadians(YawDegrees);
	const FVector2D Forward(FMath::Cos(Yaw), FMath::Sin(Yaw));
	const FVector2D Right(-Forward.Y, Forward.X);
	return FVector2D(FVector2D::DotProduct(WorldVelocity, Forward), FVector2D::DotProduct(WorldVelocity, Right));
}

FVector2D UCombatAnimInstance::GetBlendCoordinates(const FVector2D& InLocalVelocity, int32 InSpeedAxis, int32 InForwardAxis, int32 InRightAxis)
{
	float Coordinates[2] = { 0.f, 0.f };
	if (InForwardAxis != INDEX_NONE && InRightAxis != INDEX_NONE)
	{
		Coordinates[InForwardAxis] = InLocalVelocity.X;
		Coordinates[InRightAxis] = InLocalVelocity.Y;
	}
	else
	{
		Coordinates[FMath::Clamp(InSpeedAxis, 0, 1)] = InLocalVelocity.Size();
	}
	return FVector2D(Coordinates[0], Coordinates[1]);
}

void UCombatAnimInstance::PlayDeath(UAnimMontage* Montage)
{
	bDead = true;
	DeathMontage = Montage;
	if (Montage)
	{
		Montage_Play(Montage);
	}
}

void UCombatAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	// Pause just before the montage would blend out, so the figure keeps lying in its last pose.
	if (DeathMontage && Montage_IsPlaying(DeathMontage))
	{
		const float HoldTime = DeathMontage->GetPlayLength() - DeathMontage->BlendOut.GetBlendTime() - 0.05f;
		if (Montage_GetPosition(DeathMontage) >= HoldTime)
		{
			Montage_Pause(DeathMontage);
		}
	}
}
