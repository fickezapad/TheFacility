// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityAudio.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "UObject/StrongObjectPtr.h"

namespace FacilityAudio
{
	static int32 PlayedCount = 0;
	static FString LastPlayed;

	int32 GetPlayedCount() { return PlayedCount; }

	FString GetLastPlayed() { return LastPlayed; }

	USoundAttenuation* GetAttenuation(float FalloffDistance)
	{
		// one per 100 cm step is plenty, and they live for the whole process
		static TMap<int32, TStrongObjectPtr<USoundAttenuation>> Cache;
		const int32 Key = FMath::Max(1, FMath::RoundToInt(FalloffDistance / 100.0f));
		if (TStrongObjectPtr<USoundAttenuation>* Found = Cache.Find(Key))
		{
			return Found->Get();
		}

		USoundAttenuation* Attenuation = NewObject<USoundAttenuation>(GetTransientPackage());
		FSoundAttenuationSettings& Settings = Attenuation->Attenuation;
		Settings.bAttenuate = true;
		Settings.bSpatialize = true;
		Settings.AttenuationShape = EAttenuationShape::Sphere;
		Settings.AttenuationShapeExtents = FVector(150.0f);
		Settings.FalloffDistance = Key * 100.0f;
		// through a wall or a closed door it comes out dull and quieter
		Settings.bEnableOcclusion = true;
		Settings.OcclusionTraceChannel = ECC_Visibility;
		Settings.OcclusionLowPassFilterFrequency = 700.0f;
		Settings.OcclusionVolumeAttenuation = 0.45f;
		Settings.OcclusionInterpolationTime = 0.15f;

		Cache.Add(Key, TStrongObjectPtr<USoundAttenuation>(Attenuation));
		return Attenuation;
	}

	void PlayAt(const UObject* WorldContext, USoundBase* Sound, const FVector& Location, float FalloffDistance, float Volume, float Pitch)
	{
		const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
		if (!Sound || !World || World->GetNetMode() == NM_DedicatedServer)
		{
			return;
		}
		++PlayedCount;
		LastPlayed = Sound->GetName();
		UGameplayStatics::PlaySoundAtLocation(WorldContext, Sound, Location, FRotator::ZeroRotator, Volume, Pitch, 0.0f, GetAttenuation(FalloffDistance));
	}

	void Play2D(const UObject* WorldContext, USoundBase* Sound, float Volume)
	{
		const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
		if (!Sound || !World || World->GetNetMode() == NM_DedicatedServer)
		{
			return;
		}
		++PlayedCount;
		LastPlayed = Sound->GetName();
		UGameplayStatics::PlaySound2D(WorldContext, Sound, Volume);
	}
}
