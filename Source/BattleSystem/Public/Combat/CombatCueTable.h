// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "CombatCueTable.generated.h"

class UNiagaraSystem;
class USoundBase;

/** What the presentation does for one cue tag. Everything is optional. */
USTRUCT(BlueprintType)
struct FCombatCue
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cue", meta = (Categories = "Cue"))
	FGameplayTag CueTag;

	/** Spawned at the impact (the hit unit, or the area's center). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cue")
	TObjectPtr<UNiagaraSystem> Niagara;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cue")
	TObjectPtr<USoundBase> Sound;

	/** Color of the debug area drawing for this cue. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cue")
	FLinearColor DebugColor = FLinearColor(1.f, 0.4f, 0.f);
};

/** Maps simulation cue tags (FCombatEvent::Cue) to VFX, sound and debug colors. Presentation only. */
UCLASS(BlueprintType)
class BATTLESYSTEM_API UCombatCueTable : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cues")
	TArray<FCombatCue> Cues;

	/** The entry for a tag (exact match), or null. */
	const FCombatCue* FindCue(const FGameplayTag& CueTag) const;
};
