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
	SetSpeed(Speed);
}

void UCombatAnimInstance::SetTuning(float InMoveSpeed, float InLocomotionRate)
{
	MoveSpeed = InMoveSpeed;
	LocomotionRate = InLocomotionRate;
}

void UCombatAnimInstance::SetSpeed(float InSpeed)
{
	const UCombatSettings* Settings = GetDefault<UCombatSettings>();
	Speed = InSpeed;
	StrideSpeed = ComputeStrideSpeed(InSpeed, MeshScale.Z, Settings->LocomotionScaleCompensation);
	LocomotionX = SpeedAxis == 0 ? StrideSpeed : 0.f;
	LocomotionY = SpeedAxis == 1 ? StrideSpeed : 0.f;

	bIsMoving = InSpeed > Settings->LocomotionMovingThreshold;
	SpeedRatio = MoveSpeed > 0.f ? InSpeed / MoveSpeed : 0.f;

	// The samples are read every frame, so a blend space edited in the editor counts at once (a few samples).
	TArray<float, TInlineAllocator<8>> SampleSpeeds;
	if (Locomotion)
	{
		for (const FBlendSample& Sample : Locomotion->GetBlendSamples())
		{
			SampleSpeeds.Add(Sample.SampleValue[SpeedAxis]);
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
	if (BlendSpace)
	{
		// A 1D blend space only has X.
		for (int32 Axis = 0; Axis < 2; ++Axis)
		{
			if (BlendSpace->GetBlendParameter(Axis).DisplayName.Equals(TEXT("Speed"), ESearchCase::IgnoreCase))
			{
				return Axis;
			}
		}
	}
	return 0;
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
