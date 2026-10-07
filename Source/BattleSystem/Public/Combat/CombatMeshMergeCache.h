// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CombatMeshMergeCache.generated.h"

class USkeletalMesh;

/**
 * Merged body meshes of the current world, one per combination of parts, so units with the same parts share one
 * mesh. Uses the SkeletalMerging plugin. Cooked builds need "Allow CPU Access" on the source meshes.
 */
UCLASS()
class BATTLESYSTEM_API UCombatMeshMergeCache : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** The merged mesh of these parts (nullptrs are skipped); a single part is returned as is. */
	USkeletalMesh* GetMergedMesh(const TArray<USkeletalMesh*>& Parts);

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** Key: the parts' path names joined with "|". */
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<USkeletalMesh>> MergedMeshes;
};
