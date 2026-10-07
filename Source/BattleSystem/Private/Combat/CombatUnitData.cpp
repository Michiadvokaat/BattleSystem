// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatUnitData.h"
#include "Combat/CombatProjectileActor.h"
#include "Combat/CombatSettings.h"
#include "Combat/CombatSimulation.h"
#include "Combat/CombatSubsystem.h"
#include "Combat/CombatTags.h"
#include "Combat/CombatUnitActor.h"
#include "Engine/SkeletalMesh.h"

namespace
{
	/** Converts one skill to simulation stats with the unit's multipliers. False for types the simulation does not use. */
	bool ConvertSkill(const FCombatSkillRow& Skill, const FCombatUnitRow& Unit, int32 Index, int32 TickRate, FCombatAttackStats& Attack)
	{
		const bool bMelee = Skill.Type.MatchesTagExact(CombatTags::Attack_Melee);
		const bool bRanged = Skill.Type.MatchesTagExact(CombatTags::Attack_Ranged);
		const bool bTaunt = Skill.Type.MatchesTagExact(CombatTags::Attack_Taunt);
		const bool bAoE = Skill.Type.MatchesTagExact(CombatTags::Attack_AoE);
		if (!bMelee && !bRanged && !bTaunt && !bAoE)
		{
			return false;
		}

		const float Range = Skill.Range * Unit.SkillRangeMultiplier;
		Attack.Type = Skill.Type;
		Attack.Range = Range;
		Attack.Damage = Skill.Damage * Unit.SkillDamageMultiplier;
		Attack.CooldownTicks = FMath::Max(FMath::RoundToInt32(Skill.Cooldown * Unit.SkillCooldownMultiplier * TickRate), 1);
		Attack.WindupTicks = FMath::Max(FMath::RoundToInt32(Skill.Windup * Unit.SkillCooldownMultiplier * TickRate), 0);
		Attack.bNeedsWalkableLine = bMelee || (bAoE && !Skill.bRequiresLineOfSight);
		Attack.bNeedsLineOfSight = (bRanged || bAoE) && Skill.bRequiresLineOfSight;
		Attack.ProjectileSpeed = bRanged ? Skill.ProjectileSpeed : 0.f;
		Attack.SourceIndex = Index;
		Attack.ThreatMultiplier = Skill.ThreatMultiplier;
		Attack.ImpactCue = Skill.ImpactCue;
		Attack.bAffectsEnemies = Skill.bAffectsEnemies;
		Attack.bAffectsAllies = Skill.bAffectsAllies;
		if (bTaunt)
		{
			Attack.AreaShape = ECombatAreaShape::CircleAroundSelf;
			Attack.AreaRadius = Range;
		}
		else if (bAoE)
		{
			Attack.AreaShape = Skill.AreaShape != ECombatAreaShape::None ? Skill.AreaShape : ECombatAreaShape::CircleAtTarget;
			Attack.AreaRadius = Skill.AreaRadius * Unit.SkillRangeMultiplier;
			Attack.ConeCosHalfAngle = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(Skill.ConeAngle, 1.f, 360.f) * 0.5f));
			Attack.TelegraphTicks = FMath::Max(FMath::RoundToInt32(Skill.TelegraphDelay * TickRate), 0);
		}

		for (const FCombatEffectDefinition& EffectDefinition : Skill.Effects)
		{
			FCombatEffectStats& Effect = Attack.Effects.AddDefaulted_GetRef();
			Effect.EffectTag = EffectDefinition.EffectTag;
			Effect.DurationTicks = FMath::Max(FMath::RoundToInt32(EffectDefinition.Duration * Unit.EffectDurationMultiplier * TickRate), 1);
			Effect.Stacking = EffectDefinition.Stacking;
			Effect.MaxStacks = FMath::Max(EffectDefinition.MaxStacks, 1);
			Effect.GrantedTags = EffectDefinition.GrantedTags;
			Effect.BlockedByTags = EffectDefinition.BlockedByTags;
			Effect.MoveSpeedMultiplier = EffectDefinition.MoveSpeedMultiplier;
			Effect.DamageDealtMultiplier = EffectDefinition.DamageDealtMultiplier;
			Effect.DamageTakenMultiplier = EffectDefinition.DamageTakenMultiplier;
		}
		return true;
	}
}

UClass* FCombatSkillRow::LoadProjectileActorClass() const
{
	UClass* Class = ProjectileActorClass.LoadSynchronous();
	return Class ? Class : ACombatProjectileActor::StaticClass();
}

FString FCombatSkillRow::GetDisplayName() const
{
	if (!DisplayName.IsEmpty())
	{
		return DisplayName;
	}
	FString Name = Type.GetTagName().ToString();
	Name.Split(TEXT("."), nullptr, &Name, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	return Name;
}

UClass* FCombatUnitRow::LoadActorClass() const
{
	UClass* Class = ActorClass.LoadSynchronous();
	return Class ? Class : ACombatUnitActor::StaticClass();
}

FVector FCombatLook::GetMeshScale() const
{
	return FVector(UniformScale * WidthScale, UniformScale * WidthScale, UniformScale * HeightScale);
}

TArray<USkeletalMesh*> FCombatLook::PickMeshes(int32 Seed) const
{
	// One draw for the empty chance and one for the option per slot, in slot order, so adding options to one slot
	// does not change the picks of the slots before it.
	FRandomStream Stream(Seed);
	TArray<USkeletalMesh*> Picks;
	Picks.Reserve(Slots.Num());
	for (const FCombatLookSlot& Slot : Slots)
	{
		const float EmptyRoll = Stream.GetFraction();
		const int32 OptionRoll = Stream.RandHelper(FMath::Max(Slot.Options.Num(), 1));
		const bool bEmpty = Slot.Options.IsEmpty() || EmptyRoll < Slot.EmptyChance;
		Picks.Add(bEmpty ? nullptr : Slot.Options[OptionRoll].LoadSynchronous());
	}
	return Picks;
}

FCombatUnitStats FCombatUnitType::ToSimStats(int32 TickRate) const
{
	FCombatUnitStats Stats;
	Stats.MaxHP = Unit.MaxHP;
	Stats.MoveSpeed = Unit.MoveSpeed;
	Stats.Radius = Unit.Radius;
	Stats.Tags = Unit.Tags;

	for (int32 Index = 0; Index < Attacks.Num(); ++Index)
	{
		FCombatAttackStats Attack;
		if (ConvertSkill(Attacks[Index], Unit, Index, TickRate, Attack))
		{
			Stats.Attacks.Add(MoveTemp(Attack));
		}
	}

	// Player abilities keep their index (a command names it), so unusable ones still take a slot.
	for (int32 Index = 0; Index < PlayerAbilities.Num(); ++Index)
	{
		FCombatAttackStats& Ability = Stats.PlayerAbilities.AddDefaulted_GetRef();
		ConvertSkill(PlayerAbilities[Index], Unit, Index, TickRate, Ability);
	}
	return Stats;
}

FString FCombatUnitType::GetDisplayName() const
{
	return Unit.DisplayName.IsEmpty() ? Name.ToString() : Unit.DisplayName;
}

bool FCombatUnitCatalog::AddFromTables(FName UnitName, const UDataTable& UnitTable, const UDataTable& SkillTable, FString& OutError)
{
	if (Units.Contains(UnitName))
	{
		return true;
	}
	const FCombatUnitRow* Unit = UnitTable.FindRow<FCombatUnitRow>(UnitName, TEXT("FCombatUnitCatalog"), false);
	if (!Unit)
	{
		OutError = FString::Printf(TEXT("Unit '%s' is not in %s."), *UnitName.ToString(), *UnitTable.GetName());
		return false;
	}
	for (const FName& SkillName : Unit->Skills)
	{
		const FCombatSkillRow* Skill = SkillTable.FindRow<FCombatSkillRow>(SkillName, TEXT("FCombatUnitCatalog"), false);
		if (!Skill)
		{
			OutError = FString::Printf(TEXT("Skill '%s' of unit '%s' is not in %s."), *SkillName.ToString(), *UnitName.ToString(), *SkillTable.GetName());
			return false;
		}
		Skills.Add(SkillName, *Skill);
	}
	Units.Add(UnitName, *Unit);
	return true;
}

TSharedPtr<const FCombatUnitType> FCombatUnitCatalog::Resolve(FName UnitName, FString* OutError) const
{
	const FCombatUnitRow* Unit = Units.Find(UnitName);
	if (!Unit)
	{
		if (OutError)
		{
			*OutError = FString::Printf(TEXT("Unit '%s' is not in the catalog."), *UnitName.ToString());
		}
		return nullptr;
	}

	TSharedRef<FCombatUnitType> Type = MakeShared<FCombatUnitType>();
	Type->Name = UnitName;
	Type->Unit = *Unit;
	for (const FName& SkillName : Unit->Skills)
	{
		const FCombatSkillRow* Skill = Skills.Find(SkillName);
		if (!Skill)
		{
			if (OutError)
			{
				*OutError = FString::Printf(TEXT("Skill '%s' of unit '%s' is not in the catalog."), *SkillName.ToString(), *UnitName.ToString());
			}
			return nullptr;
		}
		(Skill->bPlayerActivated ? Type->PlayerAbilities : Type->Attacks).Add(*Skill);
	}
	return Type;
}

const UDataTable* CombatUnits::GetUnitTable()
{
	return GetDefault<UCombatSettings>()->UnitTable.LoadSynchronous();
}

const UDataTable* CombatUnits::GetSkillTable()
{
	return GetDefault<UCombatSettings>()->SkillTable.LoadSynchronous();
}

TSharedPtr<const FCombatUnitType> CombatUnits::FindType(const FString& Name, FString* OutError)
{
	const UDataTable* UnitTable = GetUnitTable();
	const UDataTable* SkillTable = GetSkillTable();
	if (!UnitTable || !SkillTable)
	{
		if (OutError)
		{
			*OutError = TEXT("The unit and skill tables are not set (Project Settings > Game > Combat).");
		}
		return nullptr;
	}

	FCombatUnitCatalog Catalog;
	FString Error;
	if (!Catalog.AddFromTables(FName(*Name), *UnitTable, *SkillTable, Error))
	{
		if (OutError)
		{
			*OutError = Error;
		}
		return nullptr;
	}
	return Catalog.Resolve(FName(*Name), OutError);
}

TArray<FString> CombatUnits::GetAllTypeNames()
{
	TArray<FString> Names;
	if (const UDataTable* UnitTable = GetUnitTable())
	{
		for (const FName& RowName : UnitTable->GetRowNames())
		{
			Names.Add(RowName.ToString());
		}
	}
	Names.Sort();
	return Names;
}
