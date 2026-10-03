// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CombatSetup.generated.h"

class UCombatUnitDefinition;

USTRUCT(BlueprintType)
struct FCombatSetupEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Setup")
	TObjectPtr<UCombatUnitDefinition> Definition;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Setup", meta = (ClampMin = 0))
	int32 Team = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Setup")
	FIntPoint StartCell = FIntPoint::ZeroValue;
};

/** A test lineup: which units fight, for which team, starting in which cell. */
UCLASS(BlueprintType)
class BATTLESYSTEM_API UCombatSetup : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Setup")
	TArray<FCombatSetupEntry> Units;
};
