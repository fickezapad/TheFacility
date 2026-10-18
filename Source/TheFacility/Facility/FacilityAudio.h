// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class USoundAttenuation;
class USoundBase;

/**
 *  Local sound playback for the Facility: positional, fading out over a given distance, and muffled by whatever
 *  is between the sound and the listener (walls, closed doors). Purely cosmetic and per machine: gameplay never waits
 *  on a sound, and what creatures hear goes through AFacilityTenant::ReportNoise on the server instead.
 */
namespace FacilityAudio
{
	/** Plays Sound at Location on this machine. Nothing on a dedicated server or with a null sound */
	THEFACILITY_API void PlayAt(const UObject* WorldContext, USoundBase* Sound, const FVector& Location, float FalloffDistance, float Volume = 1.0f, float Pitch = 1.0f);

	/** Plays Sound flat, in every ear the same: for the Facility itself, which is everywhere */
	THEFACILITY_API void Play2D(const UObject* WorldContext, USoundBase* Sound, float Volume = 1.0f);

	/** QA: sounds this machine has played through PlayAt and Play2D, and the last one */
	THEFACILITY_API int32 GetPlayedCount();
	THEFACILITY_API FString GetLastPlayed();

	/** Attenuation with occlusion for a falloff distance, made once and shared. Also for audio components */
	THEFACILITY_API USoundAttenuation* GetAttenuation(float FalloffDistance);
}
