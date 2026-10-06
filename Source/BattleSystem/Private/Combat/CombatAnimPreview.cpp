// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatAnimPreview.h"
#include "Combat/CombatSettings.h"
#include "Combat/CombatSubsystem.h"
#include "Combat/CombatTags.h"
#include "Combat/CombatUnitActor.h"
#include "Combat/CombatUnitDefinition.h"
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

TArray<const UCombatUnitDefinition*> ACombatAnimPreview::GetShownDefinitions() const
{
	TArray<const UCombatUnitDefinition*> Shown;
	for (const UCombatUnitDefinition* Definition : Definitions)
	{
		if (Definition)
		{
			Shown.Add(Definition);
		}
	}
	if (Definitions.IsEmpty())
	{
		for (const FString& Name : UCombatSubsystem::GetAllUnitDefinitionNames())
		{
			if (const UCombatUnitDefinition* Definition = UCombatSubsystem::FindUnitDefinition(Name))
			{
				Shown.Add(Definition);
			}
		}
	}
	return Shown;
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

	const TArray<const UCombatUnitDefinition*> Shown = GetShownDefinitions();
	for (int32 Index = 0; Index < Shown.Num(); ++Index)
	{
		const UCombatUnitDefinition* Definition = Shown[Index];
		UClass* ActorClass = Definition->ActorClass ? Definition->ActorClass.Get() : ACombatUnitActor::StaticClass();

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

		const bool bRanged = Definition->Attacks.ContainsByPredicate([](const FCombatAttackDefinition& Attack)
		{
			return Attack.Type.MatchesTagExact(CombatTags::Attack_Ranged);
		});
		Actor->InitUnit(Index, 0, Definition->Radius, GetDefault<UCombatSettings>()->GetTeamColor(0), bRanged);
		if (Definition->Appearance)
		{
			Actor->InitAppearance(Definition->Appearance, Seed + Index);
		}

		FPreviewUnit& Unit = Units.AddDefaulted_GetRef();
		Unit.Actor = Actor;
		Unit.Definition = Definition;
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
	if (!bBuilt || Units.ContainsByPredicate([](const FPreviewUnit& Unit) { return !Unit.Actor.IsValid() || !Unit.Definition.IsValid(); }))
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
		const UCombatUnitDefinition* Definition = Unit.Definition.Get();

		const float WalkSpeed = SpeedOverride > 0.f ? SpeedOverride : Definition->MoveSpeed;
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

		// Read every frame, so tuning in the definition shows at once.
		Actor->SetLocomotionTuning(Definition->MoveSpeed, Definition->LocomotionRate);
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
	if (Name == GET_MEMBER_NAME_CHECKED(ACombatAnimPreview, Definitions) || Name == GET_MEMBER_NAME_CHECKED(ACombatAnimPreview, Seed))
	{
		Rebuild();
	}
	for (FPreviewUnit& Unit : Units)
	{
		Unit.Distance = FMath::Clamp(Unit.Distance, 0.f, PathLength);
	}
}
#endif
