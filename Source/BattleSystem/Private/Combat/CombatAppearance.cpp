// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatAppearance.h"
#include "Combat/CombatSubsystem.h"
#include "Engine/SkeletalMesh.h"
#include "SkeletalMergingLibrary.h"

FVector UCombatAppearance::GetMeshScale() const
{
	return FVector(UniformScale * WidthScale, UniformScale * WidthScale, UniformScale * HeightScale);
}

TArray<USkeletalMesh*> UCombatAppearance::PickMeshes(int32 Seed) const
{
	// One draw for the empty chance and one for the option per slot, in slot order, so adding options to one slot
	// does not change the picks of the slots before it.
	FRandomStream Stream(Seed);
	TArray<USkeletalMesh*> Picks;
	Picks.Reserve(Slots.Num());
	for (const FCombatAppearanceSlot& Slot : Slots)
	{
		const float EmptyRoll = Stream.GetFraction();
		const int32 OptionRoll = Stream.RandHelper(FMath::Max(Slot.Options.Num(), 1));
		const bool bEmpty = Slot.Options.IsEmpty() || EmptyRoll < Slot.EmptyChance;
		Picks.Add(bEmpty ? nullptr : Slot.Options[OptionRoll].Get());
	}
	return Picks;
}

bool UCombatMeshMergeCache::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
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
