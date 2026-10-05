// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Subsystems/WorldSubsystem.h"
#include "CombatAppearance.generated.h"

class UAnimInstance;
class USkeletalMesh;
class UStaticMesh;

/** One body part of a look (body, shirt, hat, ...). All meshes of a look must use the same skeleton. */
USTRUCT(BlueprintType)
struct FCombatAppearanceSlot
{
	GENERATED_BODY()

	/** What this part is. Overrides and SetSlotMesh find the slot by this tag. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slot", meta = (Categories = "Slot"))
	FGameplayTag SlotTag;

	/** Meshes to choose from when the unit spawns. One mesh = always that one. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slot")
	TArray<TObjectPtr<USkeletalMesh>> Options;

	/** Chance (0..1) that the slot stays empty, for example 0.5 for a hat on half of the units. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slot", meta = (ClampMin = 0, ClampMax = 1))
	float EmptyChance = 0.f;

	/**
	 * Off: merged into the one body mesh (cheapest, fixed for the unit's life).
	 * On: its own mesh component that follows the body (Leader Pose), so it can change during play.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slot")
	bool bSwappable = false;
};

/** A rigid prop (weapon, shield, ...) attached to a socket of the skeleton. Works with meshes from any pack. */
USTRUCT(BlueprintType)
struct FCombatAppearanceProp
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prop")
	TObjectPtr<UStaticMesh> Mesh;

	/** Socket or bone of the skeleton. Empty = the mesh root (the feet). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prop")
	FName Socket;

	/** Offset relative to the socket. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prop")
	FTransform Offset;
};

/** While the unit has a tag, a swappable slot shows another mesh (or nothing). Presentation only. */
USTRUCT(BlueprintType)
struct FCombatAppearanceOverride
{
	GENERATED_BODY()

	/** An effect tag (Effect.Slow) or a tag an effect grants (Status.Taunted); parent tags match their children. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Override")
	FGameplayTag WhileTag;

	/** A slot with bSwappable on. A slot the look does not have gets its own component. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Override", meta = (Categories = "Slot"))
	FGameplayTag SlotTag;

	/** Mesh shown while the tag is active. Empty = hide the slot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Override")
	TObjectPtr<USkeletalMesh> Mesh;
};

/**
 * The look of a unit: modular skeletal mesh parts, body shape, props and tag overrides.
 * Pure presentation: the simulation never reads it, so changing a look never changes a fight.
 * ACombatUnitActor builds it when the unit's definition has one (UCombatUnitDefinition::Appearance).
 */
UCLASS(BlueprintType)
class BATTLESYSTEM_API UCombatAppearance : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parts")
	TArray<FCombatAppearanceSlot> Slots;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parts")
	TArray<FCombatAppearanceProp> Props;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parts")
	TArray<FCombatAppearanceOverride> Overrides;

	/** Animation Blueprint for the body. Empty = the reference pose. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TSubclassOf<UAnimInstance> AnimClass;

	/** Size of the whole figure. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape", meta = (ClampMin = 0.05))
	float UniformScale = 1.f;

	/** Extra scale across (X and Y): above 1 is wider. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape", meta = (ClampMin = 0.05))
	float WidthScale = 1.f;

	/** Extra scale upwards (Z): above 1 is taller. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape", meta = (ClampMin = 0.05))
	float HeightScale = 1.f;

	/** Rotation of the meshes relative to the unit, which faces +X. Most packs face +Y, hence -90. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape")
	FRotator MeshRotation = FRotator(0.0, -90.0, 0.0);

	/** Combined scale of the mesh component. */
	FVector GetMeshScale() const;

	/**
	 * Chooses a mesh per slot, the same for the same seed (ACombatUnitActor passes a hash of the fight seed and
	 * the unit ID, so a replay looks the same). Result is parallel to Slots; nullptr = empty slot.
	 * Uses its own random stream, never the simulation's.
	 */
	TArray<USkeletalMesh*> PickMeshes(int32 Seed) const;
};

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
