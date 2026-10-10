// Copyright Epic Games, Inc. All Rights Reserved.

#include "CombatColorVariant.h"

#include "AssetToolsModule.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "FileHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "IDetailsView.h"
#include "ImageCore.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "PropertyEditorModule.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"

#define LOCTEXT_NAMESPACE "CombatColorVariant"

namespace CombatColorVariant
{
	/** As Scripts/CreateColorRegions.py writes them: Region<N>_Rect (u min, v min, u max, v max), Region<N>_Ref, Use<N>, Color<N>. */
	constexpr int32 RegionCount = 4;

	/** Prefix + region + suffix: ("Region", 0, "_Rect") = Region0_Rect, ("Use", 2) = Use2. */
	FName RegionParameter(const TCHAR* Prefix, int32 Region, const TCHAR* Suffix = TEXT(""))
	{
		return FName(*(FString(Prefix) + FString::FromInt(Region) + Suffix));
	}

	bool HasRegion(const UMaterialInstanceConstant* Instance, int32 Region)
	{
		FLinearColor Rect;
		return Instance->GetVectorParameterValue(FHashedMaterialParameterInfo(RegionParameter(TEXT("Region"), Region, TEXT("_Rect"))), Rect)
			&& Rect.R <= Rect.B && Rect.G <= Rect.A;
	}

	UMaterialInstanceConstant* FindRegionInstance(const USkeletalMesh* Mesh)
	{
		for (const FSkeletalMaterial& Material : Mesh->GetMaterials())
		{
			UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Material.MaterialInterface);
			float Unused = 0.f;
			if (Instance && Instance->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Use0")), Unused))
			{
				return Instance;
			}
		}
		return nullptr;
	}

	TArray<FLinearColor> RegionColors(const UMaterialInstanceConstant* Instance)
	{
		TArray<FLinearColor> Result;
		// The palette is the texture of the recolour material's samplers (read from its graph: the compiled material's
		// texture list is not there without a renderer).
		UTexture2D* Palette = nullptr;
		if (const UMaterial* Material = Instance->GetMaterial())
		{
			for (const UMaterialExpression* Expression : Material->GetExpressions())
			{
				if (const UMaterialExpressionTextureSample* Sample = Cast<UMaterialExpressionTextureSample>(Expression))
				{
					if ((Palette = Cast<UTexture2D>(Sample->Texture)) != nullptr)
					{
						break;
					}
				}
			}
		}
		FImage Image;
		const bool bImage = Palette && Palette->Source.IsValid() && Palette->Source.GetMipImage(Image, 0);
		FImage Linear;
		if (bImage)
		{
			Image.CopyTo(Linear, ERawImageFormat::RGBA32F, EGammaSpace::Linear);
		}
		for (int32 Region = 0; Region < RegionCount && HasRegion(Instance, Region); ++Region)
		{
			FLinearColor Ref = FLinearColor::White;
			Instance->GetVectorParameterValue(FHashedMaterialParameterInfo(RegionParameter(TEXT("Region"), Region, TEXT("_Ref"))), Ref);
			FLinearColor Color = FLinearColor::White;
			if (bImage)
			{
				const int32 X = FMath::Clamp(FMath::FloorToInt32(Ref.R * Linear.SizeX), 0, Linear.SizeX - 1);
				const int32 Y = FMath::Clamp(FMath::FloorToInt32(Ref.G * Linear.SizeY), 0, Linear.SizeY - 1);
				Color = Linear.AsRGBA32F()[Y * Linear.SizeX + X];
				Color.A = 1.f;
			}
			Result.Add(Color);
		}
		return Result;
	}

	bool MakeVariant(USkeletalMesh* Mesh, const FString& Name, const TArray<FCombatLookColor>& Colors, FString& OutMessage)
	{
		UMaterialInstanceConstant* Source = FindRegionInstance(Mesh);
		if (!Source)
		{
			OutMessage = FString::Printf(TEXT("%s has no colour regions (run Scripts/CreateColorRegions.py)"), *Mesh->GetName());
			return false;
		}
		const FString Folder = FPackageName::GetLongPackagePath(Mesh->GetOutermost()->GetName());
		const FString CopyName = Mesh->GetName() + TEXT("_") + Name;
		if (FPackageName::DoesPackageExist(Folder / CopyName) || FindObject<UObject>(nullptr, *(Folder / CopyName + TEXT(".") + CopyName)))
		{
			OutMessage = FString::Printf(TEXT("%s already exists"), *CopyName);
			return false;
		}
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		USkeletalMesh* Copy = Cast<USkeletalMesh>(AssetTools.DuplicateAsset(CopyName, Folder, Mesh));
		UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
		Factory->InitialParent = Source->Parent;
		UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(AssetTools.CreateAsset(
			TEXT("MI_") + CopyName, Folder / TEXT("Materials"), UMaterialInstanceConstant::StaticClass(), Factory));
		if (!Copy || !Instance)
		{
			OutMessage = FString::Printf(TEXT("could not create %s"), *CopyName);
			return false;
		}
		// The regions of the original, with the chosen colours fixed (Use = 1); the others keep their colour.
		for (int32 Region = 0; Region < RegionCount && HasRegion(Source, Region); ++Region)
		{
			for (const TCHAR* Suffix : { TEXT("_Rect"), TEXT("_Ref") })
			{
				const FName Parameter = RegionParameter(TEXT("Region"), Region, Suffix);
				FLinearColor Value;
				Source->GetVectorParameterValue(FHashedMaterialParameterInfo(Parameter), Value);
				Instance->SetVectorParameterValueEditorOnly(Parameter, Value);
			}
			float Use = 0.f;
			FLinearColor Color = FLinearColor::White;
			Source->GetScalarParameterValue(FHashedMaterialParameterInfo(RegionParameter(TEXT("Use"), Region)), Use);
			Source->GetVectorParameterValue(FHashedMaterialParameterInfo(RegionParameter(TEXT("Color"), Region)), Color);
			if (Colors.IsValidIndex(Region) && Colors[Region].bOverride)
			{
				Use = 1.f;
				Color = Colors[Region].Color;
			}
			Instance->SetScalarParameterValueEditorOnly(RegionParameter(TEXT("Use"), Region), Use);
			Instance->SetVectorParameterValueEditorOnly(RegionParameter(TEXT("Color"), Region), Color);
		}
		Instance->PostEditChange();
		Copy->Modify();
		for (FSkeletalMaterial& Material : Copy->GetMaterials())
		{
			if (Material.MaterialInterface == Source)
			{
				Material.MaterialInterface = Instance;
			}
		}
		Copy->PostEditChange();
		UEditorLoadingAndSavingUtils::SavePackages({ Copy->GetPackage(), Instance->GetPackage() }, false);
		OutMessage = Folder / CopyName;
		return true;
	}

	void OpenDialog(const TArray<USkeletalMesh*>& Meshes)
	{
		if (Meshes.IsEmpty())
		{
			return;
		}
		UCombatColorVariantSettings* Settings = NewObject<UCombatColorVariantSettings>();
		Settings->AddToRoot();
		Settings->Name = TEXT("Variant");
		// The first mesh's own colours, so it is clear which region is which.
		if (const UMaterialInstanceConstant* Instance = FindRegionInstance(Meshes[0]))
		{
			for (const FLinearColor& Color : RegionColors(Instance))
			{
				FCombatLookColor& Entry = Settings->Colors.AddDefaulted_GetRef();
				Entry.Color = Color;
			}
		}

		FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		FDetailsViewArgs Args;
		Args.bAllowSearch = false;
		Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
		TSharedRef<IDetailsView> Details = PropertyEditor.CreateDetailView(Args);
		Details->SetObject(Settings);

		const FText Title = Meshes.Num() == 1
			? FText::Format(LOCTEXT("TitleOne", "Make Color Variant of {0}"), FText::FromString(Meshes[0]->GetName()))
			: FText::Format(LOCTEXT("TitleMany", "Make Color Variant of {0} meshes"), Meshes.Num());
		TSharedRef<SWindow> Window = SNew(SWindow).Title(Title).ClientSize(FVector2D(480, 420)).SupportsMinimize(false).SupportsMaximize(false);
		bool bCreate = false;
		Window->SetContent(
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().FillHeight(1.f).Padding(4.f)[Details]
			+ SVerticalBox::Slot().AutoHeight().Padding(8.f).HAlign(HAlign_Right)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f)
				[
					SNew(SButton).Text(LOCTEXT("Create", "Create")).OnClicked_Lambda([&bCreate, Window]()
					{
						bCreate = true;
						Window->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton).Text(LOCTEXT("Cancel", "Cancel")).OnClicked_Lambda([Window]()
					{
						Window->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
			]);
		GEditor->EditorAddModalWindow(Window);

		if (bCreate)
		{
			FString Name = Settings->Name.TrimStartAndEnd().Replace(TEXT(" "), TEXT("_"));
			TArray<FString> Made, Failed;
			for (USkeletalMesh* Mesh : Meshes)
			{
				FString Message;
				(MakeVariant(Mesh, Name, Settings->Colors, Message) ? Made : Failed).Add(Message);
			}
			FString Report = FString::Printf(TEXT("Made %d colour variant(s):\n%s"), Made.Num(), *FString::Join(Made, TEXT("\n")));
			if (!Failed.IsEmpty())
			{
				Report += FString::Printf(TEXT("\n\nNot made:\n%s"), *FString::Join(Failed, TEXT("\n")));
			}
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Report));
		}
		Settings->RemoveFromRoot();
	}
}

FString UCombatColorVariantLibrary::MakeColorVariant(USkeletalMesh* Mesh, const FString& Name, const TArray<FCombatLookColor>& Colors)
{
	FString Message;
	if (Mesh && CombatColorVariant::MakeVariant(Mesh, Name, Colors, Message))
	{
		return Message;
	}
	UE_LOG(LogTemp, Warning, TEXT("Make Color Variant: %s"), Mesh ? *Message : TEXT("no mesh"));
	return FString();
}

TArray<FLinearColor> UCombatColorVariantLibrary::GetRegionColors(USkeletalMesh* Mesh)
{
	const UMaterialInstanceConstant* Instance = Mesh ? CombatColorVariant::FindRegionInstance(Mesh) : nullptr;
	return Instance ? CombatColorVariant::RegionColors(Instance) : TArray<FLinearColor>();
}

#undef LOCTEXT_NAMESPACE
