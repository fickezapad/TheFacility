// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityVoiceComponent.h"
#include "Facility/FacilityAudio.h"
#include "Facility/FacilityCharacter.h"
#include "Facility/FacilityDirector.h"
#include "Facility/FacilityHidingSpot.h"
#include "Facility/FacilityMovementComponent.h"
#include "Facility/FacilityTenant.h"
#include "Components/CapsuleComponent.h"
#include "DSP/MultithreadedPatching.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Interfaces/VoiceInterface.h"
#include "Net/UnrealNetwork.h"
#include "Net/VoiceConfig.h"
#include "OnlineSubsystemUtils.h"
#include "TheFacility.h"

UFacilityVoiceComponent::UFacilityVoiceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f;
	SetIsReplicatedByDefault(true);
}

void UFacilityVoiceComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UFacilityVoiceComponent, VoiceMode);
	DOREPLIFETIME(UFacilityVoiceComponent, bHasMicrophone);
}

void UFacilityVoiceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Talker)
	{
		Talker->DestroyComponent();
		Talker = nullptr;
	}
	MicrophoneTap.Reset();

	Super::EndPlay(EndPlayReason);
}

AFacilityCharacter* UFacilityVoiceComponent::GetCharacter() const
{
	return Cast<AFacilityCharacter>(GetOwner());
}

void UFacilityVoiceComponent::Server_SetVoiceState_Implementation(EFacilityVoiceMode Mode, bool bMicrophone)
{
	ForceVoiceState(Mode, bMicrophone);
}

void UFacilityVoiceComponent::ForceVoiceState(EFacilityVoiceMode Mode, bool bMicrophone)
{
	if (VoiceMode != Mode || bHasMicrophone != bMicrophone)
	{
		UE_LOG(LogTheFacility, Log, TEXT("Voice: %s is on %s%s"), *GetNameSafe(GetOwner()),
			*StaticEnum<EFacilityVoiceMode>()->GetNameStringByValue((int64)Mode), bMicrophone ? TEXT("") : TEXT(", no microphone"));
	}
	VoiceMode = Mode;
	bHasMicrophone = bMicrophone;
}

void UFacilityVoiceComponent::ForceAmplitude(float Amplitude, float Seconds)
{
	ForcedAmplitude = FMath::Clamp(Amplitude, 0.0f, 1.0f);
	ForcedUntil = GetWorld()->GetTimeSeconds() + Seconds;
}

float UFacilityVoiceComponent::RadiusForAmplitude(float Amplitude) const
{
	if (Amplitude < SilenceAmplitude)
	{
		return 0.0f;
	}

	// eased, so murmuring stays close and only real shouting reaches across the motel
	const float Alpha = FMath::Clamp((Amplitude - SilenceAmplitude) / FMath::Max(ShoutAmplitude - SilenceAmplitude, 0.01f), 0.0f, 1.0f);
	return FMath::Lerp(WhisperRadius, ShoutRadius, Alpha * Alpha);
}

void UFacilityVoiceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (GetNetMode() != NM_DedicatedServer)
	{
		EnsureTalker();
	}

	if (!GetOwner()->HasAuthority())
	{
		return;
	}

	LastAmplitude = MeasureAmplitude();
	ReportIn -= DeltaTime;
	if (ReportIn <= 0.0f && LastAmplitude >= SilenceAmplitude)
	{
		ReportIn = ReportInterval;
		ReportVoice(LastAmplitude);
	}

	UpdateBreath(DeltaTime);
}

void UFacilityVoiceComponent::EnsureTalker()
{
	const AFacilityCharacter* Character = GetCharacter();
	APlayerState* PlayerState = Character ? Character->GetPlayerState() : nullptr;
	// a player never hears their own voice back
	if (Talker || !PlayerState || Character->IsLocallyControlled() || !PlayerState->GetUniqueId().IsValid())
	{
		return;
	}

	Talker = UVOIPTalker::CreateTalkerForPlayer(PlayerState);
	if (Talker)
	{
		Talker->Settings.ComponentToAttachTo = Character->GetCapsuleComponent();
		Talker->Settings.AttenuationSettings = FacilityAudio::GetAttenuation(VoiceFalloff);
		// joined by IP, without an online session: nothing else registers remote talkers
		if (IOnlineVoicePtr Voice = Online::GetVoiceInterface(GetWorld()); Voice.IsValid())
		{
			Voice->RegisterRemoteTalker(*PlayerState->GetUniqueId().GetUniqueNetId());
		}
		UE_LOG(LogTheFacility, Log, TEXT("Voice: %s's voice will come from their body on the %s"), *PlayerState->GetPlayerName(),
			GetNetMode() == NM_Client ? TEXT("client") : TEXT("host"));
	}
}

float UFacilityVoiceComponent::MeasureAmplitude()
{
	if (ForcedAmplitude >= 0.0f)
	{
		if (GetWorld()->GetTimeSeconds() < ForcedUntil)
		{
			return ForcedAmplitude;
		}
		ForcedAmplitude = -1.0f;
	}

	const AFacilityCharacter* Character = GetCharacter();
	const APlayerState* PlayerState = Character ? Character->GetPlayerState() : nullptr;
	if (!PlayerState || VoiceMode == EFacilityVoiceMode::Off)
	{
		return 0.0f;
	}

	IOnlineVoicePtr Voice = Online::GetVoiceInterface(GetWorld());
	if (!Voice.IsValid())
	{
		return 0.0f;
	}

	// the host's own voice: read straight from its microphone
	if (Character->IsLocallyControlled())
	{
		if (!MicrophoneTap.IsValid())
		{
			MicrophoneTap = Voice->GetMicrophoneOutput();
		}
		if (!MicrophoneTap.IsValid() || !Voice->IsLocalPlayerTalking(0))
		{
			return 0.0f;
		}

		float Samples[2048];
		const int32 NumRead = MicrophoneTap->PopAudio(Samples, UE_ARRAY_COUNT(Samples), true);
		float Peak = 0.0f;
		for (int32 Index = 0; Index < NumRead; ++Index)
		{
			Peak = FMath::Max(Peak, FMath::Abs(Samples[Index]));
		}
		return FMath::Clamp(Peak, 0.0f, 1.0f);
	}

	// everyone else: the voice the host is receiving from them
	if (!PlayerState->GetUniqueId().IsValid())
	{
		return 0.0f;
	}
	return FMath::Clamp(Voice->GetAmplitudeOfRemoteTalker(*PlayerState->GetUniqueId().GetUniqueNetId()), 0.0f, 1.0f);
}

void UFacilityVoiceComponent::ReportVoice(float Amplitude)
{
	AFacilityCharacter* Character = GetCharacter();
	const float Radius = RadiusForAmplitude(Amplitude);
	if (!Character || Radius <= 0.0f || Character->IsRetained())
	{
		return;
	}

	++NumVoiceReports;
	UE_LOG(LogTheFacility, Verbose, TEXT("Voice: %s at %.2f carries %.0f cm"), *GetNameSafe(Character), Amplitude, Radius);

	// the Tenant hears it; the Facility counts it on its own, smaller budget
	AFacilityTenant::ReportNoise(GetWorld(), Character->GetPawnViewLocation(), Radius, Character, false);
	if (AFacilityDirector* Director = AFacilityDirector::Get(GetWorld()))
	{
		Director->HearVoice(Radius);
	}

	// talking from a hiding place gives it away to a Tenant right outside
	if (AFacilityHidingSpot* Spot = Character->GetHidingSpot())
	{
		for (TActorIterator<AFacilityTenant> It(GetWorld()); It; ++It)
		{
			It->HearVoiceFromHiding(Character, Spot, HiddenVoiceFindRange);
		}
	}
}

void UFacilityVoiceComponent::UpdateBreath(float DeltaTime)
{
	AFacilityCharacter* Character = GetCharacter();
	if (!Character || Character->IsRetained() || Character->IsHiding() || !IsSilent())
	{
		BreathIn = 0.0f;
		return;
	}

	const UFacilityMovementComponent* Movement = Character->GetFacilityMovement();
	const bool bLabouring = Character->IsDowned() || (Movement && (Movement->IsSprinting() || Movement->IsExhausted()));
	BreathIn -= DeltaTime;
	if (!bLabouring || BreathIn > 0.0f)
	{
		return;
	}

	BreathIn = BreathInterval;
	++NumBreaths;
	UE_LOG(LogTheFacility, Verbose, TEXT("Voice: %s breathes hard (can't be heard otherwise)"), *GetNameSafe(Character));
	AFacilityTenant::ReportNoise(GetWorld(), Character->GetPawnViewLocation(), BreathRadius, Character, false);
}
