// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "SkillSystemTestAttributeSet.generated.h"

UCLASS(Blueprintable, BlueprintType)
class USkillSystemTestAttributeSet : public UAttributeSet
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AttributeTest", meta = (HideFromModifiers))			// You can't make a GameplayEffect modifiy Health Directly (go through)
	float	MaxHealth;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AttributeTest", meta = (HideFromModifiers))			// You can't make a GameplayEffect modifiy Health Directly (go through)
	float	Health;

	/** This Damage is just used for applying negative health mods. Its not a 'persistent' attribute. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AttributeTest", meta = (HideFromLevelInfos))		// You can't make a GameplayEffect 'powered' by Damage (Its transient)
	float	Damage;

	/** This Attribute is the actual spell damage for this actor. It will power spell based GameplayEffects */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AttributeTest")
	float	SpellDamage;

	/** This Attribute is the actual physical damage for this actor. It will power spell based GameplayEffects */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AttributeTest")
	float	PhysicalDamage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AttributeTest")
	float	CritChance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AttributeTest")
	float	CritMultiplier;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AttributeTest")
	float	ArmorDamageReduction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AttributeTest")
	float	DodgeChance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AttributeTest")
	float	LifeSteal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AttributeTest")
	float	Strength;

	virtual void PreAttributeModify(struct FGameplayEffectModCallbackData &Data) OVERRIDE;
	virtual void PostAttributeModify(const struct FGameplayEffectModCallbackData &Data) OVERRIDE;
};