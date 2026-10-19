// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityFlicker.h"
#include "Facility/FacilityGameState.h"
#include "Components/LightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"

AFacilityFlicker::AFacilityFlicker()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.03f;
	SetHidden(true);
}

void AFacilityFlicker::BeginPlay()
{
	Super::BeginPlay();

	// a dedicated server shows nothing, so it has nothing to flicker
	if (GetNetMode() == NM_DedicatedServer)
	{
		SetActorTickEnabled(false);
		return;
	}

	TimeToNext = FMath::FRandRange(0.5f, MaxCalm);
}

bool AFacilityFlicker::HasPower() const
{
	if (!bNeedsMainPower)
	{
		return true;
	}
	const AFacilityGameState* GameState = GetWorld()->GetGameState<AFacilityGameState>();
	return GameState && GameState->IsPowerRestored();
}

void AFacilityFlicker::SetLit(bool bInLit)
{
	bLit = bInLit;
	for (AActor* Target : Targets)
	{
		if (!Target)
		{
			continue;
		}

		TInlineComponentArray<ULightComponent*> Lights(Target);
		for (ULightComponent* Light : Lights)
		{
			Light->SetVisibility(bLit);
		}
		TInlineComponentArray<UTextRenderComponent*> Texts(Target);
		for (UTextRenderComponent* Text : Texts)
		{
			Text->SetVisibility(bLit);
		}
		TInlineComponentArray<UStaticMeshComponent*> Meshes(Target);
		for (UStaticMeshComponent* Mesh : Meshes)
		{
			Mesh->SetScalarParameterValueOnMaterials(TEXT("Emissive"), bLit ? LitEmissive : LitEmissive * 0.05f);
		}
	}
}

void AFacilityFlicker::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasPower())
	{
		if (bLit)
		{
			SetLit(false);
		}
		return;
	}

	TimeToNext -= DeltaSeconds;
	if (TimeToNext > 0.0f)
	{
		return;
	}

	if (StuttersLeft > 0)
	{
		// a quick stutter: off and on a few times
		--StuttersLeft;
		SetLit(!bLit);
		TimeToNext = FMath::FRandRange(0.04f, 0.15f);
		if (StuttersLeft == 0)
		{
			const bool bOutage = FMath::FRand() < OutageChance;
			SetLit(!bOutage);
			TimeToNext = bOutage ? OutageTime * FMath::FRandRange(0.6f, 1.4f) : FMath::FRandRange(MinCalm, MaxCalm);
		}
		return;
	}

	if (!bLit)
	{
		// back from an outage
		SetLit(true);
		TimeToNext = FMath::FRandRange(MinCalm, MaxCalm);
		return;
	}

	StuttersLeft = FMath::RandRange(2, 6) * 2;
}
