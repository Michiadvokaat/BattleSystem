#include "Combat/CombatHideZones.h"

#include "Components/MeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

int32 UCombatHideZonesLibrary::GetHideZones(const USkeletalMesh* Mesh)
{
	const UCombatHideZones* Zones = Mesh ? const_cast<USkeletalMesh*>(Mesh)->GetAssetUserData<UCombatHideZones>() : nullptr;
	return Zones ? Zones->ZoneMask : 0;
}

void UCombatHideZonesLibrary::SetHideZones(USkeletalMesh* Mesh, int32 ZoneMask)
{
	if (!Mesh)
	{
		return;
	}
	Mesh->Modify();
	if (ZoneMask == 0)
	{
		Mesh->RemoveUserDataOfClass(UCombatHideZones::StaticClass());
		return;
	}
	UCombatHideZones* Zones = Mesh->GetAssetUserData<UCombatHideZones>();
	if (!Zones)
	{
		Zones = NewObject<UCombatHideZones>(Mesh);
		Mesh->AddAssetUserData(Zones);
	}
	Zones->ZoneMask = ZoneMask;
}

FName UCombatHideZonesLibrary::ZoneParameterName(int32 Index)
{
	return FName(*FString::Printf(TEXT("HideZones%d"), Index));
}

TArray<TObjectPtr<UMaterialInstanceDynamic>> UCombatHideZonesLibrary::GetZoneMaterials(UMeshComponent* Component)
{
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Result;
	if (!Component)
	{
		return Result;
	}
	const FHashedMaterialParameterInfo Probe(ZoneParameterName(0));
	for (int32 Index = 0; Index < Component->GetNumMaterials(); ++Index)
	{
		UMaterialInterface* Material = Component->GetMaterial(Index);
		FLinearColor Unused;
		if (!Material || !Material->GetVectorParameterValue(Probe, Unused))
		{
			continue;
		}
		UMaterialInstanceDynamic* Dynamic = Cast<UMaterialInstanceDynamic>(Material);
		if (!Dynamic)
		{
			Dynamic = Component->CreateAndSetMaterialInstanceDynamic(Index);
		}
		Result.Add(Dynamic);
	}
	return Result;
}

void UCombatHideZonesLibrary::ApplyHideZones(const TArray<TObjectPtr<UMaterialInstanceDynamic>>& Materials, int32 ZoneMask)
{
	for (int32 Parameter = 0; Parameter < ZoneParameterCount; ++Parameter)
	{
		FLinearColor Flags;
		for (int32 Lane = 0; Lane < 4; ++Lane)
		{
			Flags.Component(Lane) = (ZoneMask >> (Parameter * 4 + Lane)) & 1 ? 1.f : 0.f;
		}
		for (UMaterialInstanceDynamic* Material : Materials)
		{
			Material->SetVectorParameterValue(ZoneParameterName(Parameter), Flags);
		}
	}
}
