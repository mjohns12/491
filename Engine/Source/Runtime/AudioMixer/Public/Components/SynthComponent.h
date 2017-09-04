// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/AudioComponent.h"
#include "UObject/ObjectMacros.h"
#include "Engine/EngineTypes.h"
#include "Sound/SoundWaveProcedural.h"
#include "AudioMixerTypes.h"

#include "SynthComponent.generated.h"

#define SYNTH_GENERATOR_TEST_TONE 0

#if SYNTH_GENERATOR_TEST_TONE
#include "DSP/SinOsc.h"
#endif

class USynthComponent;

UCLASS()
class AUDIOMIXER_API USynthSound : public USoundWaveProcedural
{
	GENERATED_UCLASS_BODY()

	void Init(USynthComponent* INSynthComponent, const int32 InNumChannels);

	/** Begin USoundWave */
	virtual bool OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples) override;
	virtual Audio::EAudioMixerStreamDataFormat::Type GetGeneratedPCMDataFormat() const override;
	/** End USoundWave */

protected:
	USynthComponent* OwningSynthComponent;
	TArray<float> FloatBuffer;
	bool bAudioMixer;
};

UCLASS(ClassGroup = Synth, hidecategories = (Object, ActorComponent, Physics, Rendering, Mobility, LOD))
class AUDIOMIXER_API USynthComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	USynthComponent(const FObjectInitializer& ObjectInitializer);

	//~ Begin USceneComponent Interface
	virtual void Activate(bool bReset = false) override;
	virtual void Deactivate() override;
	//~ End USceneComponent Interface

	//~ Begin ActorComponent Interface.
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual bool IsReadyForOwnerToAutoDestroy() const override;
	//~ End ActorComponent Interface.

	//~ Begin UObject Interface.
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR

	// Starts the synth generating audio.
	UFUNCTION(BlueprintCallable, Category = "Synth|Components|Audio")
	void Start();

	// Stops the synth generating audio.
	UFUNCTION(BlueprintCallable, Category = "Synth|Components|Audio")
	void Stop();

	/** Returns true if this component is currently playing. */
	UFUNCTION(BlueprintCallable, Category = "Synth|Components|Audio")
	bool IsPlaying() const;

	/** Sets how much audio the sound should send to the given submix. */
	UFUNCTION(BlueprintCallable, Category = "Audio|Components|Audio")
	void SetSubmixSend(USoundSubmix* Submix, float SendLevel);

	/** Auto destroy this component on completion */
	UPROPERTY()
	uint8 bAutoDestroy : 1;

	/** Stop sound when owner is destroyed */
	UPROPERTY()
	uint8 bStopWhenOwnerDestroyed : 1;

	/** Is this audio component allowed to be spatialized? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attenuation)
	uint8 bAllowSpatialization : 1;

	/** Should the Attenuation Settings asset be used (false) or should the properties set directly on the component be used for attenuation properties */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attenuation)
	uint8 bOverrideAttenuation : 1;

	/** If bOverrideSettings is false, the asset to use to determine attenuation properties for sounds generated by this component */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attenuation, meta = (EditCondition = "!bOverrideAttenuation"))
	class USoundAttenuation* AttenuationSettings;

	/** If bOverrideSettings is true, the attenuation properties to use for sounds generated by this component */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attenuation, meta = (EditCondition = "bOverrideAttenuation"))
	struct FSoundAttenuationSettings AttenuationOverrides;

	/** What sound concurrency to use for sounds generated by this audio component */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Concurrency)
	class USoundConcurrency* ConcurrencySettings;

	/** Sound class this sound belongs to */
	UPROPERTY(EditAnywhere, Category = SoundClass)
	USoundClass* SoundClass;

	/** The source effect chain to use for this sound. */
	UPROPERTY(EditAnywhere, Category = Effects)
	USoundEffectSourcePresetChain* SourceEffectChain;

	/** Submix this sound belongs to */
	UPROPERTY(EditAnywhere, Category = Effects)
	USoundSubmix* SoundSubmix;

	/** An array of submix sends. Audio from this sound will send a portion of its audio to these effects.  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Effects)
	TArray<FSoundSubmixSendInfo> SoundSubmixSends;

	/** Whether or not this sound plays when the game is paused in the UI */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Sound)
	uint8 bIsUISound : 1;

	/** Call if creating this synth component not via an actor component in BP, but in code or some other location. */
	void Initialize();

	/** Retrieves this synth component's audio component. */
	UAudioComponent* GetAudioComponent();

protected:

	// Method to execute parameter changes on game thread in audio render thread
	void SynthCommand(TFunction<void()> Command);

	// Called when synth is created.
	virtual void Init(const int32 SampleRate) {}

	// Called when synth is about to start playing
	virtual void OnStart() {}

	// Called when synth is about to stop playing
	virtual void OnStop() {}

	// Called when more audio is needed to be generated
	virtual void OnGenerateAudio(float* OutAudio, int32 NumSamples) PURE_VIRTUAL(USynthComponent::OnGenerateAudio,);

	// Called by procedural sound wave
	void OnGeneratePCMAudio(float* GeneratedPCMData, int32 NumSamples);

	// Gets the audio device associated with this synth component
	FAudioDevice* GetAudioDevice() { return AudioComponent->GetAudioDevice(); }

	// Can be set by the derived class, defaults to 2
	int32 NumChannels;

private:

	UPROPERTY(Transient)
	USynthSound* Synth;

	UPROPERTY(Transient)
	UAudioComponent* AudioComponent;

	void PumpPendingMessages();

#if SYNTH_GENERATOR_TEST_TONE
	Audio::FSineOsc TestSineLeft;
	Audio::FSineOsc TestSineRight;
#endif

	// Whether or not synth is playing
	bool bIsSynthPlaying;
	bool bIsInitialized;

	TQueue<TFunction<void()>> CommandQueue;

	enum class ESynthEvent : uint8
	{
		None,
		Start,
		Stop
	};

	TQueue<ESynthEvent> PendingSynthEvents;

	friend class USynthSound;
};