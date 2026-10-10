// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Combat/CombatUnitData.h"
#include "CombatColorVariant.generated.h"

class USkeletalMesh;

/**
 * The settings of "Make Color Variant..." (right click on skeletal meshes in the Content Browser): a copy of each mesh next
 * to it, <mesh>_<Name>, with its own recolour material instance (Materials/MI_<copy>) whose colour regions have these
 * colours fixed. Works on meshes with colour regions (Scripts/CreateColorRegions.py).
 */
UCLASS(Transient)
class UCombatColorVariantSettings : public UObject
{
	GENERATED_BODY()

public:
	/** Added to the mesh's name: SK_..._Shirt_01 + Red = SK_..._Shirt_01_Red. */
	UPROPERTY(EditAnywhere, Category = "Color Variant")
	FString Name;

	/** Colour regions 1 (the main colour) to 4, filled with the first mesh's own colours; tick Override to change one. */
	UPROPERTY(EditAnywhere, EditFixedSize, Category = "Color Variant")
	TArray<FCombatLookColor> Colors;
};

/** The colour variants for scripts (Python: unreal.CombatColorVariantLibrary.make_color_variant). */
UCLASS()
class UCombatColorVariantLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** As "Make Color Variant...": a copy <mesh>_<Name> next to the mesh with these colours; the path of the copy, or "" (see the log). */
	UFUNCTION(BlueprintCallable, Category = "Combat|Look")
	static FString MakeColorVariant(USkeletalMesh* Mesh, const FString& Name, const TArray<FCombatLookColor>& Colors);

	/** The own colour of each colour region of the mesh (empty without regions). */
	UFUNCTION(BlueprintCallable, Category = "Combat|Look")
	static TArray<FLinearColor> GetRegionColors(USkeletalMesh* Mesh);
};

namespace CombatColorVariant
{
	/** The recolour material instance of a mesh (the first slot whose material has colour regions), or null. */
	class UMaterialInstanceConstant* FindRegionInstance(const USkeletalMesh* Mesh);

	/** The own colour of each colour region of the instance (from its palette texture at the region's reference spot). */
	TArray<FLinearColor> RegionColors(const UMaterialInstanceConstant* Instance);

	/** Makes and saves the coloured copy; false (with the reason) if the mesh has no colour regions or the name is taken. */
	bool MakeVariant(USkeletalMesh* Mesh, const FString& Name, const TArray<FCombatLookColor>& Colors, FString& OutMessage);

	/** Opens the dialog for the meshes and makes their variants on Create. */
	void OpenDialog(const TArray<USkeletalMesh*>& Meshes);
}
