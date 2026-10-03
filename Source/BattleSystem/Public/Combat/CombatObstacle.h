// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CombatObstacle.generated.h"

class ACombatGrid;
class UBoxComponent;
class UStaticMeshComponent;

/**
 * An obstacle in the arena. Its footprint is every grid cell whose center lies inside the box
 * (at least the cell under the actor). Scale or resize the box to change the footprint.
 */
UCLASS()
class BATTLESYSTEM_API ACombatObstacle : public AActor
{
	GENERATED_BODY()

public:
	ACombatObstacle();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Obstacle")
	bool bBlocksWalkability = true;

	/** Used from phase 3 (line of sight). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Obstacle")
	bool bBlocksSight = true;

	void GetFootprint(const ACombatGrid& Grid, TArray<FIntPoint>& OutCells) const;

	virtual void OnConstruction(const FTransform& Transform) override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Obstacle")
	TObjectPtr<UBoxComponent> Box;

	UPROPERTY(VisibleAnywhere, Category = "Obstacle")
	TObjectPtr<UStaticMeshComponent> Mesh;
};
