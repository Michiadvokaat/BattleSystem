// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Combat/CombatTypes.h"
#include "CombatCommandScript.generated.h"

/**
 * A fixed list of player commands, to test or measure player input without playing: Combat.Simulate and
 * Combat.Batch take it with script=<asset name>. Unit IDs are the indices of the setup's valid entries.
 */
UCLASS(BlueprintType)
class BATTLESYSTEM_API UCombatCommandScript : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Script")
	TArray<FCombatCommand> Commands;
};
