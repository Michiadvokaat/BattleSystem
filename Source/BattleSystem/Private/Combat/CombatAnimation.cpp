// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatAnimation.h"
#include "Animation/AnimMontage.h"
#include "Animation/BlendSpace.h"

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

void UCombatAnimInstance::SetSpeed(float InSpeed)
{
	Speed = InSpeed;
	LocomotionX = SpeedAxis == 0 ? InSpeed : 0.f;
	LocomotionY = SpeedAxis == 1 ? InSpeed : 0.f;
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
