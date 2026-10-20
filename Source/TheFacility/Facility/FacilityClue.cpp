// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityClue.h"
#include "Facility/FacilityAudio.h"
#include "Components/AudioComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/World.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const FName FuseClue(TEXT("Fuse"));
	const FName GateKeyClue(TEXT("GateKey"));
}

UFacilityClueComponent::UFacilityClueComponent()
{
	static ConstructorHelpers::FObjectFinder<USoundBase> HumAsset(TEXT("/Game/Facility/Audio/S_FuseHum.S_FuseHum"));
	static ConstructorHelpers::FObjectFinder<USoundBase> SparkAsset(TEXT("/Game/Facility/Audio/S_Spark.S_Spark"));
	static ConstructorHelpers::FObjectFinder<USoundBase> JingleAsset(TEXT("/Game/Facility/Audio/S_KeyJingle.S_KeyJingle"));
	HumSound = HumAsset.Object;
	SparkSound = SparkAsset.Object;
	JingleSound = JingleAsset.Object;
}

void UFacilityClueComponent::SetItem(FName ItemId)
{
	const FName NewItem = (ItemId == FuseClue || ItemId == GateKeyClue) ? ItemId : NAME_None;
	if (NewItem == Item && (bActive || NewItem.IsNone()))
	{
		return;
	}
	Stop();
	Item = NewItem;

	UWorld* World = GetWorld();
	if (Item.IsNone() || !World || World->GetNetMode() == NM_DedicatedServer || !World->IsGameWorld())
	{
		return;
	}
	bActive = true;

	if (Item == FuseClue)
	{
		// the hum runs all the time; it only reaches someone close
		Hum = NewObject<UAudioComponent>(GetOwner());
		Hum->SetupAttachment(this);
		Hum->SetSound(HumSound);
		Hum->bAutoActivate = false;
		Hum->bOverrideAttenuation = false;
		Hum->AttenuationSettings = FacilityAudio::GetAttenuation(Range);
		Hum->SetVolumeMultiplier(0.35f);
		Hum->RegisterComponent();
		Hum->Play(FMath::FRand());

		// a spark lights up the inside of a drawer for a blink; a small light, only seen from nearby
		SparkLight = NewObject<UPointLightComponent>(GetOwner());
		SparkLight->SetupAttachment(this);
		SparkLight->SetMobility(EComponentMobility::Movable);
		SparkLight->SetIntensityUnits(ELightUnits::Candelas);
		SparkLight->SetIntensity(0.0f);
		SparkLight->SetAttenuationRadius(160.0f);
		SparkLight->SetLightColor(FLinearColor(0.6f, 0.75f, 1.0f));
		SparkLight->SetCastShadows(false);
		SparkLight->RegisterComponent();
	}

	World->GetTimerManager().SetTimer(ClueTimer, this, &UFacilityClueComponent::Pulse, FMath::FRandRange(1.0f, 3.0f), false);
}

void UFacilityClueComponent::Pulse()
{
	UWorld* World = GetWorld();
	if (!bActive || !World)
	{
		return;
	}

	if (Item == FuseClue)
	{
		FacilityAudio::PlayAt(this, SparkSound, GetComponentLocation(), Range, 0.5f, FMath::FRandRange(0.9f, 1.15f));
		if (SparkLight)
		{
			SparkLight->SetIntensity(6.0f);
			TWeakObjectPtr<UPointLightComponent> WeakLight = SparkLight;
			FTimerHandle Off;
			World->GetTimerManager().SetTimer(Off, FTimerDelegate::CreateLambda([WeakLight]()
			{
				if (WeakLight.IsValid())
				{
					WeakLight->SetIntensity(0.0f);
				}
			}), 0.09f, false);
		}
		World->GetTimerManager().SetTimer(ClueTimer, this, &UFacilityClueComponent::Pulse, FMath::FRandRange(2.5f, 5.0f), false);
	}
	else
	{
		FacilityAudio::PlayAt(this, JingleSound, GetComponentLocation(), Range, 0.35f, FMath::FRandRange(0.92f, 1.08f));
		World->GetTimerManager().SetTimer(ClueTimer, this, &UFacilityClueComponent::Pulse, FMath::FRandRange(3.0f, 6.0f), false);
	}
}

void UFacilityClueComponent::Stop()
{
	bActive = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ClueTimer);
	}
	if (Hum)
	{
		Hum->Stop();
		Hum->DestroyComponent();
		Hum = nullptr;
	}
	if (SparkLight)
	{
		SparkLight->DestroyComponent();
		SparkLight = nullptr;
	}
}

void UFacilityClueComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Stop();
	Super::EndPlay(EndPlayReason);
}
