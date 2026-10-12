// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityZoneLights.h"
#include "Facility/FacilityGameState.h"
#include "Components/LightComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/GameStateBase.h"
#include "TimerManager.h"
#include "TheFacility.h"

AFacilityZoneLights::AFacilityZoneLights()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AFacilityZoneLights::BeginPlay()
{
	Super::BeginPlay();

	// static lights are baked and can't be switched off at runtime
	for (AActor* LightActor : LightActors)
	{
		if (!LightActor)
		{
			continue;
		}

		TInlineComponentArray<ULightComponent*> Lights(LightActor);
		for (const ULightComponent* Light : Lights)
		{
			if (Light->Mobility == EComponentMobility::Static)
			{
				UE_LOG(LogTheFacility, Warning, TEXT("%s: light %s is Static and won't turn off. Set it to Movable."), *GetName(), *GetNameSafe(LightActor));
			}
		}
	}

	TryBindToGameState();
}

void AFacilityZoneLights::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(BindTimer);
	GetWorldTimerManager().ClearTimer(FlickerTimer);

	Super::EndPlay(EndPlayReason);
}

void AFacilityZoneLights::TryBindToGameState()
{
	AGameStateBase* AnyGameState = GetWorld()->GetGameState();
	AFacilityGameState* GameState = Cast<AFacilityGameState>(AnyGameState);

	if (AnyGameState && !GameState)
	{
		// a game state exists but it isn't ours: the level runs the wrong game mode, and waiting won't fix that
		UE_LOG(LogTheFacility, Error, TEXT("%s (zone %s) needs AFacilityGameState, but this level uses %s. Set the level's GameMode Override to a FacilityGameMode Blueprint. Zone lights are disabled."),
			*GetName(), *ZoneId.ToString(), *AnyGameState->GetClass()->GetName());
		return;
	}

	if (!GameState)
	{
		// on clients the game state can arrive after this actor begins play; wait for it, but not forever
		if (++BindAttempts > MaxBindAttempts)
		{
			UE_LOG(LogTheFacility, Error, TEXT("%s (zone %s) gave up: no game state after %.0f s. Zone lights are disabled."),
				*GetName(), *ZoneId.ToString(), MaxBindAttempts * BindRetryInterval);
			return;
		}

		GetWorldTimerManager().SetTimer(BindTimer, this, &AFacilityZoneLights::TryBindToGameState, BindRetryInterval, false);
		return;
	}

	GameState->OnSignatureRecorded.AddUniqueDynamic(this, &AFacilityZoneLights::HandleSignatureRecorded);

	// catch up on anything signed before we were listening
	if (GameState->HasSignatureInZone(ZoneId))
	{
		CutPower(false);
	}
}

void AFacilityZoneLights::HandleSignatureRecorded(const FFacilitySignature& Signature)
{
	if (!bPowered || Signature.ZoneId != ZoneId)
	{
		return;
	}

	const AFacilityGameState* GameState = GetWorld()->GetGameState<AFacilityGameState>();
	const bool bFresh = GameState && (GameState->GetServerWorldTimeSeconds() - Signature.ServerTime) <= FreshSignatureWindow;

	CutPower(bFresh);
}

void AFacilityZoneLights::CutPower(bool bAnimate)
{
	if (!bPowered)
	{
		return;
	}

	bPowered = false;

	if (!bAnimate || FlickerDuration <= 0.0f)
	{
		SetLightsVisible(false);
		return;
	}

	if (ChimeSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ChimeSound, GetActorLocation());
	}

	FlickerTimeLeft = FlickerDuration;
	GetWorldTimerManager().SetTimer(FlickerTimer, this, &AFacilityZoneLights::FlickerStep, 0.08f, true);
}

void AFacilityZoneLights::FlickerStep()
{
	FlickerTimeLeft -= 0.08f;

	if (FlickerTimeLeft <= 0.0f)
	{
		GetWorldTimerManager().ClearTimer(FlickerTimer);
		SetLightsVisible(false);
		return;
	}

	// mostly on, dropping out at random, so it reads as failing power rather than strobing
	SetLightsVisible(FMath::FRand() > 0.45f);
}

void AFacilityZoneLights::SetLightsVisible(bool bVisible)
{
	for (AActor* LightActor : LightActors)
	{
		if (!LightActor)
		{
			continue;
		}

		TInlineComponentArray<ULightComponent*> Lights(LightActor);
		for (ULightComponent* Light : Lights)
		{
			Light->SetVisibility(bVisible);
		}
	}
}
