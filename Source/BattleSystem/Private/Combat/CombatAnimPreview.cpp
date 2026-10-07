// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatAnimPreview.h"
#include "Combat/CombatSettings.h"
#include "Combat/CombatSubsystem.h"
#include "Combat/CombatTags.h"
#include "Combat/CombatUnitActor.h"
#include "Combat/CombatUnitData.h"
#include "Components/ArrowComponent.h"
#include "Engine/World.h"

ACombatAnimPreview::ACombatAnimPreview()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

#if WITH_EDITORONLY_DATA
	// Shows the walking direction (X) in the editor.
	if (UArrowComponent* Arrow = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("Direction")))
	{
		Arrow->SetupAttachment(RootComponent);
		Arrow->ArrowSize = 2.f;
	}
#endif
}

TArray<FName> ACombatAnimPreview::GetShownTypes() const
{
	TArray<FName> Shown;
	for (const FName& Type : UnitTypes)
	{
		if (FindRow(Type))
		{
			Shown.Add(Type);
		}
	}
	if (UnitTypes.IsEmpty())
	{
		for (const FString& Name : CombatUnits::GetAllTypeNames())
		{
			Shown.Add(FName(*Name));
		}
	}
	return Shown;
}

const FCombatUnitRow* ACombatAnimPreview::FindRow(FName Type)
{
	const UDataTable* Table = CombatUnits::GetUnitTable();
	return Table ? Table->FindRow<FCombatUnitRow>(Type, TEXT("ACombatAnimPreview"), false) : nullptr;
}

void ACombatAnimPreview::Rebuild()
{
	ClearUnits();
	bBuilt = true;
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const TArray<FName> Shown = GetShownTypes();
	for (int32 Index = 0; Index < Shown.Num(); ++Index)
	{
		const TSharedPtr<const FCombatUnitType> Type = CombatUnits::FindType(Shown[Index].ToString());
		if (!Type)
		{
			continue;
		}
		UClass* ActorClass = Type->Unit.LoadActorClass();

		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		// Never saved with the map, and not cluttering the outliner.
		SpawnParams.ObjectFlags |= RF_Transient;
#if WITH_EDITOR
		SpawnParams.bHideFromSceneOutliner = true;
#endif
		const FVector Location = GetActorLocation() + GetActorRightVector() * (Index * Spacing);
		ACombatUnitActor* Actor = World->SpawnActor<ACombatUnitActor>(ActorClass, Location, GetActorRotation(), SpawnParams);
		if (!Actor)
		{
			continue;
		}

		const bool bRanged = Type->Attacks.ContainsByPredicate([](const FCombatSkillRow& Skill)
		{
			return Skill.Type.MatchesTagExact(CombatTags::Attack_Ranged);
		});
		Actor->InitUnit(Index, 0, Type->Unit.Radius, GetDefault<UCombatSettings>()->GetTeamColor(0), bRanged);
		Actor->InitLook(Type->Unit.Look, Seed + Index);

		FPreviewUnit& Unit = Units.AddDefaulted_GetRef();
		Unit.Actor = Actor;
		Unit.Type = Type->Name;
	}
}

void ACombatAnimPreview::ClearUnits()
{
	for (const FPreviewUnit& Unit : Units)
	{
		if (ACombatUnitActor* Actor = Unit.Actor.Get())
		{
			Actor->Destroy();
		}
	}
	Units.Reset();
}

void ACombatAnimPreview::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// The figures are transient: after loading the map (or an undo) they are gone and come back here.
	if (!bBuilt || Units.ContainsByPredicate([](const FPreviewUnit& Unit) { return !Unit.Actor.IsValid() || !FindRow(Unit.Type); }))
	{
		Rebuild();
	}

	const float Rate = bPaused ? 0.f : TimeScale;
	const float Delta = DeltaSeconds * Rate;
	const FVector Forward = GetActorForwardVector();
	const FVector Right = GetActorRightVector();
	for (int32 Index = 0; Index < Units.Num(); ++Index)
	{
		FPreviewUnit& Unit = Units[Index];
		ACombatUnitActor* Actor = Unit.Actor.Get();
		const FCombatUnitRow* Row = FindRow(Unit.Type);

		const float WalkSpeed = SpeedOverride > 0.f ? SpeedOverride : Row->MoveSpeed;
		const FVector WalkDirection = Forward * Unit.Direction;
		float Speed = 0.f;
		if (Unit.StopLeft > 0.f)
		{
			Unit.StopLeft -= Delta;
		}
		else if (WalkSpeed > 0.f && PathLength > 0.f)
		{
			Speed = WalkSpeed;
			Unit.Distance += Unit.Direction * WalkSpeed * Delta;
			if (Unit.Distance >= PathLength || Unit.Distance <= 0.f)
			{
				// At the end: stand still, already in the facing of the next leg, so it turns while standing.
				Unit.Distance = FMath::Clamp(Unit.Distance, 0.f, PathLength);
				Unit.Direction = -Unit.Direction;
				++Unit.Leg;
				Unit.StopLeft = StopTime;
			}
		}

		// Read every frame, so tuning in the unit table shows at once.
		Actor->SetLocomotionTuning(Row->MoveSpeed, Row->LocomotionRate);
		Actor->SetAnimationState(WalkDirection * Speed, Rate);
		const FVector Location = GetActorLocation() + Right * (Index * Spacing) + Forward * Unit.Distance;
		// Facing turned the other way than the offset, so the walk is the offset off the figure's forward.
		Actor->UpdatePresentation(Location, (Forward * Unit.Direction).RotateAngleAxis(-GetFacingOffset(Unit), FVector::UpVector));
	}
}

float ACombatAnimPreview::GetFacingOffset(const FPreviewUnit& Unit) const
{
	// Out forward, back facing the same way (backwards), out to the right, back facing the same way (to the left).
	static const float Cycle[] = { 0.f, 180.f, 90.f, -90.f };
	return bCycleDirections ? Cycle[Unit.Leg % UE_ARRAY_COUNT(Cycle)] : FacingOffset;
}

void ACombatAnimPreview::Destroyed()
{
	ClearUnits();
	Super::Destroyed();
}

#if WITH_EDITOR
void ACombatAnimPreview::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName Name = PropertyChangedEvent.GetMemberPropertyName();
	if (Name == GET_MEMBER_NAME_CHECKED(ACombatAnimPreview, UnitTypes) || Name == GET_MEMBER_NAME_CHECKED(ACombatAnimPreview, Seed))
	{
		Rebuild();
	}
	for (FPreviewUnit& Unit : Units)
	{
		Unit.Distance = FMath::Clamp(Unit.Distance, 0.f, PathLength);
	}
}
#endif
