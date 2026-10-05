// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"

enum class ECombatDesignTool : uint8;
class FAssetThumbnail;
class FAssetThumbnailPool;
class SWrapBox;
class UCombatSubsystem;
struct FCombatPieceDefinition;

/**
 * LevelDesigner menu (bottom left): edit mode, grid size, new/load/save/play, plus rename and delete (with a second
 * click to confirm) of the level selected in the dropdown, and the modes with only their own controls: Build Mode
 * shows the piece catalog (a row of main categories, a row of their subcategories, the pieces of the selected one
 * from small to large with thumbnails in the editor and PIE) and the rotation; Unit Mode the unit type and team;
 * Spawn Mode the unit type, the time and the waves (select, add, remove). Placing itself happens with the mouse on
 * the arena (ACombatPlayerController).
 */
class SCombatLevelDesigner : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCombatLevelDesigner) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UCombatSubsystem>, Subsystem)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	/** Shows the selected piece's category when the selected piece changes (eyedropper); category clicks stay. */
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	bool IsEditing() const;
	bool IsTool(ECombatDesignTool Tool) const;
	EVisibility GetToolVisibility(ECombatDesignTool Tool) const { return IsEditing() && IsTool(Tool) ? EVisibility::Visible : EVisibility::Collapsed; }
	/** Fills the main category buttons from the catalog and shows the selected piece's category. */
	void RebuildPieceCategories();
	/** Fills the subcategory buttons of SelectedGroup, then the palette. */
	void RebuildSubCategories();
	/** Fills the palette with the pieces of SelectedCategory, from small to large footprint. */
	void RebuildPalette();
	/** Selects a catalog category ("Group/Sub") and shows it. */
	void ShowCategory(const FString& Category);
	TSharedRef<SWidget> MakePieceButton(const FCombatPieceDefinition& Definition);
	EVisibility GetEditVisibility() const { return IsEditing() ? EVisibility::Visible : EVisibility::Collapsed; }

	TSharedRef<SWidget> MakeButton(const FText& Label, TFunction<void()> OnClick, TFunction<bool()> IsActive = nullptr);
	TSharedRef<SWidget> MakeLabel(const FText& Label);
	/** One button per color of the settings' FloorColors; a click makes it the color of the next tintable piece. */
	TSharedRef<SWidget> MakeColorSwatches();
	/** Opens the color picker on the current tint color. */
	FReply OnPickColorClicked();
	TSharedRef<SWidget> MakeSizeBox(bool bWidth);

	void RefreshLevelOptions();
	void SyncNameFromLevel();
	FReply OnDeleteClicked();
	/** The first Delete click arms it; a second within DeleteConfirmSeconds deletes. Any other button disarms it. */
	bool IsConfirmingDelete() const;

	TWeakObjectPtr<UCombatSubsystem> Subsystem;

	FText NameText;
	FString Message;

	TArray<TSharedPtr<FString>> LevelOptions;
	TSharedPtr<FString> SelectedLevel;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> LevelCombo;
	/** Platform seconds until which a Delete click deletes; 0 = not armed. */
	double DeleteConfirmUntil = 0.0;

	TSharedPtr<SWrapBox> CategoryBox;
	TSharedPtr<SWrapBox> SubCategoryBox;
	TSharedPtr<SWrapBox> PaletteBox;
	/** Main category (Building, Furniture, Props): the part of a catalog category before its first "/". */
	FString SelectedGroup;
	/** Full catalog category, "Group/Sub". */
	FString SelectedCategory;
	/** The selected piece the palette last followed. */
	FString FollowedPiece;
	/** Thumbnails of the palette (editor and PIE only); kept alive while shown. */
	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
	TArray<TSharedPtr<FAssetThumbnail>> Thumbnails;

	TArray<TSharedPtr<FString>> UnitTypeOptions;
	TSharedPtr<FString> SelectedUnitType;
};
