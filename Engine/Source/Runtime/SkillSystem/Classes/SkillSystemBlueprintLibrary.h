// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Abilities/GameplayAbility.h"
#include "SkillSystemTypes.h"
#include "SkillSystemBlueprintLibrary.generated.h"

class UAnimMontage;
class UAbilityTask_PlayMontageAndWait;
class UAbilityTask_WaitMovementModeChange;
class UAbilityTask_WaitOverlap;

UCLASS(MinimalAPI)
class USkillSystemBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()

	UFUNCTION(BlueprintPure, Category = Ability)
	static UAttributeComponent* GetAttributeComponent(AActor *Actor);

	UFUNCTION(BlueprintCallable, Category=Ability)
	static void ApplyGameplayEffectToTargetData(FGameplayAbilityTargetDataHandle Target, UGameplayEffect *GameplayEffect, const FGameplayAbilityActorInfo InstigatorInfo);

	// -------------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, meta=(HidePin="WorldContextObject", DefaultToSelf="WorldContextObject", BlueprintInternalUseOnly = "TRUE"))
	static UAbilityTask_PlayMontageAndWait* CreatePlayMontageAndWaitProxy(UObject* WorldContextObject, UAnimMontage *MontageToPlay);

	UFUNCTION(BlueprintCallable, meta = (HidePin = "WorldContextObject", DefaultToSelf = "WorldContextObject", BlueprintInternalUseOnly = "TRUE"))
	static UAbilityTask_WaitMovementModeChange* CreateWaitMovementModeChange(UObject* WorldContextObject, EMovementMode NewMode);

	UFUNCTION(BlueprintCallable, meta = (HidePin = "WorldContextObject", DefaultToSelf = "WorldContextObject", BlueprintInternalUseOnly = "TRUE"))
	static UAbilityTask_WaitOverlap* CreateWaitOverlap(UObject* WorldContextObject, EMovementMode NewMode);
};