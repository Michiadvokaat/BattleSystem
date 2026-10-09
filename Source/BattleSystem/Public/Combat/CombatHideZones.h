// Hide zones: parts of the body that clothing covers, hidden by the body material so the skin does not poke through.

#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CombatHideZones.generated.h"

class UMaterialInstanceDynamic;
class UMeshComponent;
class USkeletalMesh;

/**
 * A zone of the body. The order is the bit order of the zone masks and must match BODY_ZONES in
 * Scripts/Blender/FitToHero.py (which writes the body's zone codes and the clothing's masks).
 */
UENUM(BlueprintType)
enum class ECombatBodyZone : uint8
{
	Neck,
	/** Around the neck opening, the top band of the trunk (the trunk is cut in bands by height, so tops and pants each cover whole bands). */
	Collar,
	Chest,
	/** The belly above the pelvis. */
	Waist,
	/** The lowest band of the trunk, in the overlap of tops and pants. */
	Pelvis,
	UpperArmL,
	UpperArmR,
	ForeArmL,
	ForeArmR,
	HandL,
	HandR,
	ThighL,
	ThighR,
	ShinL,
	ShinR,
	FootL,
	FootR,
	/** The top of the head, above the brows. */
	Crown,
	/** The back of the head below the crown. */
	BackOfHead,
	Ears,
	Count UMETA(Hidden)
};

/**
 * On a clothing mesh: the body zones it covers (a bit per ECombatBodyZone). Set by Scripts/ImportFittedParts.py from
 * the coverage FitToHero.py measured; a look hides the zones of everything it shows.
 */
UCLASS(BlueprintType)
class BATTLESYSTEM_API UCombatHideZones : public UAssetUserData
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Look", meta = (Bitmask, BitmaskEnum = "/Script/BattleSystem.ECombatBodyZone"))
	int32 ZoneMask = 0;
};

UCLASS()
class BATTLESYSTEM_API UCombatHideZonesLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The zones a mesh covers (0 without hide zones or without a mesh). */
	UFUNCTION(BlueprintPure, Category = "Combat|Look")
	static int32 GetHideZones(const USkeletalMesh* Mesh);

	/** Sets the zones a mesh covers (0 removes them). For the import scripts; the caller saves the mesh. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Look")
	static void SetHideZones(USkeletalMesh* Mesh, int32 ZoneMask);

	/**
	 * Dynamic instances of the component's materials that have the hide zone parameters (the body material); creates them
	 * the first time. Other materials (clothing) are left alone.
	 */
	static TArray<TObjectPtr<UMaterialInstanceDynamic>> GetZoneMaterials(UMeshComponent* Component);

	/** Hides the zones in ZoneMask on those materials and shows the others. */
	static void ApplyHideZones(const TArray<TObjectPtr<UMaterialInstanceDynamic>>& Materials, int32 ZoneMask);

	/** The vector parameters of the body material, four zones each (x, y, z, w = 1: hidden). */
	static FName ZoneParameterName(int32 Index);
	static constexpr int32 ZoneParameterCount = (static_cast<int32>(ECombatBodyZone::Count) + 3) / 4;
};
