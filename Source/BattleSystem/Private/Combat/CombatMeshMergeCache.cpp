// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatMeshMergeCache.h"
#include "Combat/CombatSubsystem.h"
#include "Engine/SkeletalMesh.h"
#include "SkeletalMergingLibrary.h"

bool UCombatMeshMergeCache::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// Editor: the figures of an ACombatAnimPreview.
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::Editor;
}

USkeletalMesh* UCombatMeshMergeCache::GetMergedMesh(const TArray<USkeletalMesh*>& Parts)
{
	TArray<TObjectPtr<USkeletalMesh>> Meshes;
	FString Key;
	for (USkeletalMesh* Part : Parts)
	{
		if (Part)
		{
			Meshes.Add(Part);
			Key += Part->GetPathName() + TEXT("|");
		}
	}
	if (Meshes.Num() <= 1)
	{
		return Meshes.IsEmpty() ? nullptr : Meshes[0].Get();
	}

	if (const TObjectPtr<USkeletalMesh>* Cached = MergedMeshes.Find(Key))
	{
		return *Cached;
	}

	FSkeletalMeshMergeParams Params;
	Params.MeshesToMerge = Meshes;
	Params.Skeleton = Meshes[0]->GetSkeleton();
	USkeletalMesh* Merged = USkeletalMergingLibrary::MergeMeshes(Params);
	if (!Merged)
	{
		UE_LOG(LogCombat, Warning, TEXT("Merging %d meshes failed (do they share one skeleton?): %s"), Meshes.Num(), *Key);
	}
	// A failed merge is cached too, so it is not retried for every unit.
	MergedMeshes.Add(Key, Merged);
	return Merged;
}
