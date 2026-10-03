// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CombatUnitActor.generated.h"

class SCombatHealthBar;
class SCombatStatusIcons;
class UMaterialInstanceDynamic;
class UWidgetComponent;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * Presentation of one simulation unit. Spawned and driven by UCombatSubsystem: it only follows the
 * simulation (interpolated position, events) and contains no gameplay logic.
 * Placeholder look: a cylinder (melee) or cube (ranged) in the team color with a "nose" that shows the facing,
 * a health bar above it and its active effects as short labels on its center (both screen-space widgets).
 */
/** One status label shown on a unit, for example "T" in magenta while taunted. */
struct FCombatStatusDisplay
{
	FString Label;
	FLinearColor Color = FLinearColor::White;

	bool operator==(const FCombatStatusDisplay& Other) const { return Label == Other.Label && Color == Other.Color; }
};

UCLASS()
class BATTLESYSTEM_API ACombatUnitActor : public AActor
{
	GENERATED_BODY()

public:
	ACombatUnitActor();

	/** bRanged picks the body shape: RangedBodyMesh for units with a ranged attack, MeleeBodyMesh otherwise. */
	virtual void InitUnit(int32 InUnitId, int32 InTeam, float InRadius, const FLinearColor& InTeamColor, bool bRanged);

	/** Called every frame with the interpolated location and the direction to face (may be zero). */
	virtual void UpdatePresentation(const FVector& InLocation, const FVector& FacingDirection);

	virtual void OnAttack(const FVector& TargetLocation);
	virtual void OnHit(float Damage);
	virtual void OnDeath();
	/** Health as a fraction of max HP (0..1), shown by the health bar. */
	virtual void SetHealth(float Fraction);

	/** The unit's active effects as labels, shown on its center. */
	virtual void SetStatusEffects(const TArray<FCombatStatusDisplay>& Icons);

	/** Shows the selection ring around the unit's feet. */
	virtual void SetSelected(bool bSelected);

	/** Shows a disc on the move target and a line to it while the unit has a move order. */
	virtual void SetMoveTarget(bool bActive, const FVector& Target);

	/** One of this unit's area attacks went off (the subsystem draws the area); Radius = its reach in cm. */
	virtual void OnAreaAttack(float Radius);

	int32 GetUnitId() const { return UnitId; }
	int32 GetTeam() const { return Team; }

protected:
	UFUNCTION(BlueprintImplementableEvent, Category = "Combat", meta = (DisplayName = "On Unit Attack"))
	void ReceiveUnitAttack(FVector TargetLocation);

	UFUNCTION(BlueprintImplementableEvent, Category = "Combat", meta = (DisplayName = "On Unit Hit"))
	void ReceiveUnitHit(float Damage);

	UFUNCTION(BlueprintImplementableEvent, Category = "Combat", meta = (DisplayName = "On Unit Death"))
	void ReceiveUnitDeath();

	UFUNCTION(BlueprintImplementableEvent, Category = "Combat", meta = (DisplayName = "On Unit Area Attack"))
	void ReceiveUnitAreaAttack(float Radius);

	UPROPERTY(VisibleAnywhere, Category = "Combat")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, Category = "Combat")
	TObjectPtr<UStaticMeshComponent> NoseMesh;

	UPROPERTY(VisibleAnywhere, Category = "Combat")
	TObjectPtr<UWidgetComponent> HealthBarWidget;

	UPROPERTY(VisibleAnywhere, Category = "Combat")
	TObjectPtr<UWidgetComponent> StatusWidget;

	/** Move order: a flat disc on the target cell and a thin line to it (both placed in world space). */
	UPROPERTY(VisibleAnywhere, Category = "Combat")
	TObjectPtr<UStaticMeshComponent> MoveTargetMarker;

	UPROPERTY(VisibleAnywhere, Category = "Combat")
	TObjectPtr<UStaticMeshComponent> MoveTargetLine;

	/** Selection ring: this many short flat blocks in a circle around the unit. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Presentation", meta = (ClampMin = 4, ClampMax = 64))
	int32 SelectionRingSegments = 16;

	/** Gap between the unit's edge and the selection ring. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Presentation", meta = (Units = "cm"))
	float SelectionRingOffset = 15.f;

	/** Health bar size in screen pixels. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Presentation")
	FVector2D HealthBarSize = FVector2D(60.0, 8.0);

	/** Height of the health bar above the top of the body. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Presentation", meta = (Units = "cm"))
	float HealthBarOffset = 40.f;

	/** Body shape of melee units (and units without attacks). Default: the engine cylinder. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Presentation")
	TObjectPtr<UStaticMesh> MeleeBodyMesh;

	/** Body shape of units with a ranged attack. Default: the engine cube. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Presentation")
	TObjectPtr<UStaticMesh> RangedBodyMesh;

	/** Base material for the body; tinted with the team color through BodyColorParameter. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Presentation")
	TObjectPtr<UMaterialInterface> BodyMaterialBase;

	/** Vector parameter of BodyMaterialBase that receives the team color and the hit flash. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Presentation")
	FName BodyColorParameter = TEXT("Color");

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Presentation", meta = (Units = "cm"))
	float BodyHeight = 180.f;

	/** How far the body lunges towards the target on an attack. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Presentation", meta = (Units = "cm"))
	float LungeDistance = 30.f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Presentation", meta = (Units = "s"))
	float LungeDuration = 0.15f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Presentation", meta = (Units = "s"))
	float HitFlashDuration = 0.1f;

	/** How long damage numbers stay visible. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Presentation", meta = (Units = "s"))
	float DamageTextDuration = 1.f;

private:
	void SetBodyColor(const FLinearColor& Color);

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BodyMaterial;

	int32 UnitId = INDEX_NONE;
	int32 Team = 0;
	FLinearColor TeamColor = FLinearColor::White;

	double LungeStartTime = -1.0;
	FVector LungeDirection = FVector::ZeroVector;
	double HitFlashStartTime = -1.0;
	bool bFlashing = false;
	TSharedPtr<SCombatHealthBar> HealthBar;
	TSharedPtr<SCombatStatusIcons> StatusIcons;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> SelectionRing;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MarkerMaterial;
};
