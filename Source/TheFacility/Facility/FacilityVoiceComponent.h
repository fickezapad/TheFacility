// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FacilityVoiceComponent.generated.h"

class AFacilityCharacter;
class UVOIPTalker;

namespace Audio { struct FPatchOutput; }

/** How a player has chosen to use their microphone */
UENUM(BlueprintType)
enum class EFacilityVoiceMode : uint8
{
	/** Always transmitting (above the noise gate) */
	OpenMic,
	/** Transmitting only while the talk key is held */
	PushToTalk,
	/** Never transmitting */
	Off
};

/**
 *  One player's voice, on every machine. Built on the engine's own VoIP (Null online subsystem over the game connection).
 *
 *  Everywhere: this player's voice comes from their body, fading with distance and muffled through walls and doors.
 *  Players the Facility has taken are heard only by each other.
 *
 *  On the server (the listen-server host, which receives everyone's voice): how loud this player is speaking becomes a
 *  sound the Tenant hears, from a whisper (a couple of metres) to a shout (across the motel). The Marked doesn't listen.
 *  Talking adds a little to the Facility's attention, on its own small cap. Talking while hidden, with the Tenant
 *  right outside, gives the hider away. And a player who can't be heard (push-to-talk, voice off, or no microphone)
 *  breathes audibly when sprinting, out of breath or down, so silence isn't free.
 */
UCLASS(ClassGroup=Facility, meta=(BlueprintSpawnableComponent))
class THEFACILITY_API UFacilityVoiceComponent : public UActorComponent
{
	GENERATED_BODY()

protected:

	/** Voice at the noise gate carries this far for the Tenant */
	UPROPERTY(EditAnywhere, Category="Voice|Noise", meta=(ClampMin=0, Units="cm"))
	float WhisperRadius = 250.0f;

	/** A shout carries this far */
	UPROPERTY(EditAnywhere, Category="Voice|Noise", meta=(ClampMin=0, Units="cm"))
	float ShoutRadius = 2200.0f;

	/** Below this amplitude (0..1) the server treats the voice as silence */
	UPROPERTY(EditAnywhere, Category="Voice|Noise", meta=(ClampMin=0, ClampMax=1))
	float SilenceAmplitude = 0.03f;

	/** At or above this amplitude it is a shout */
	UPROPERTY(EditAnywhere, Category="Voice|Noise", meta=(ClampMin=0, ClampMax=1))
	float ShoutAmplitude = 0.5f;

	/** How often a talking player is reported to the Tenant */
	UPROPERTY(EditAnywhere, Category="Voice|Noise", meta=(ClampMin=0.1, Units="s"))
	float ReportInterval = 0.4f;

	/** Within this distance of a hiding spot, a listening Tenant hears who is talking inside and pulls them out */
	UPROPERTY(EditAnywhere, Category="Voice|Noise", meta=(ClampMin=0, Units="cm"))
	float HiddenVoiceFindRange = 300.0f;

	/** Breathing of a player who can't be heard: how far it carries, and how often */
	UPROPERTY(EditAnywhere, Category="Voice|Breath", meta=(ClampMin=0, Units="cm"))
	float BreathRadius = 500.0f;

	UPROPERTY(EditAnywhere, Category="Voice|Breath", meta=(ClampMin=0.2, Units="s"))
	float BreathInterval = 1.5f;

	/** How far other players hear this voice */
	UPROPERTY(EditAnywhere, Category="Voice|Playback", meta=(ClampMin=0, Units="cm"))
	float VoiceFalloff = 2500.0f;

	/** Reported by the owning client, so the server knows who gets the breathing */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Voice")
	EFacilityVoiceMode VoiceMode = EFacilityVoiceMode::OpenMic;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Voice")
	bool bHasMicrophone = true;

	/** Local: plays this player's voice from their body, on every machine but their own */
	UPROPERTY(Transient)
	TObjectPtr<UVOIPTalker> Talker;

	/** Server */
	float ReportIn = 0.0f;
	float BreathIn = 0.0f;
	float LastAmplitude = 0.0f;

	/** Server, QA: voice reports and breaths so far */
	int32 NumVoiceReports = 0;
	int32 NumBreaths = 0;

	/** Server, QA: a forced amplitude and until when */
	float ForcedAmplitude = -1.0f;
	double ForcedUntil = 0.0;

	/** Server, host's own voice: reads the host's microphone */
	TSharedPtr<Audio::FPatchOutput, ESPMode::ThreadSafe> MicrophoneTap;

public:

	UFacilityVoiceComponent();

	EFacilityVoiceMode GetVoiceMode() const { return VoiceMode; }

	bool HasMicrophone() const { return bHasMicrophone; }

	/** True if this player can't be heard: push-to-talk, voice off, or no microphone */
	bool IsSilent() const { return VoiceMode != EFacilityVoiceMode::OpenMic || !bHasMicrophone; }

	/** Server: the last amplitude measured for this player, 0..1 */
	float GetLastAmplitude() const { return LastAmplitude; }

	int32 GetNumVoiceReports() const { return NumVoiceReports; }
	int32 GetNumBreaths() const { return NumBreaths; }

	/** Local: is this player's voice set up on this machine */
	bool HasTalker() const { return Talker != nullptr; }

	/** Owning client: tells the server how this player uses their microphone */
	UFUNCTION(Server, Reliable)
	void Server_SetVoiceState(EFacilityVoiceMode Mode, bool bMicrophone);

	/** Server, QA: pretends this player speaks at Amplitude for Seconds */
	void ForceAmplitude(float Amplitude, float Seconds);

	/** Server, QA: sets the voice state as if the client had reported it */
	void ForceVoiceState(EFacilityVoiceMode Mode, bool bMicrophone);

	/** Radius a voice of this amplitude carries for the Tenant; 0 for silence */
	float RadiusForAmplitude(float Amplitude) const;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	AFacilityCharacter* GetCharacter() const;

	/** Local: creates the talker once the player state and its id have arrived */
	void EnsureTalker();

	/** Server: how loud this player is speaking right now, 0..1 */
	float MeasureAmplitude();

	/** Server: turns this player's voice into sound for the Tenant and attention for the Facility */
	void ReportVoice(float Amplitude);

	/** Server: breathing for players who can't be heard */
	void UpdateBreath(float DeltaTime);
};
