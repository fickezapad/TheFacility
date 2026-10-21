// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "FacilityGuest.generated.h"

class AFacilityCharacter;
class USoundBase;
struct FFacilitySignature;

UENUM(BlueprintType)
enum class EGuestState : uint8
{
	/** Not awake tonight, or not yet: hidden in Room 0 */
	Dormant,
	/** Something was signed for it; it stirs */
	Manifesting,
	/** Out, coming for whoever is nearest, but only while nobody's light is on it */
	Hunting,
	/** Took someone: back in Room 0 for a while */
	Resting
};

/**
 *  The Guest in Room 0: a figure under a stained motel sheet, too tall, with a brass key tag that says 0.
 *  Woken by the Offerings bound to it tonight, it comes out of Room 0 and makes for the nearest player, but it only
 *  moves while no flashlight is on it. Lit, it freezes mid-step. It takes whoever it reaches (downed), then goes back
 *  to its room for a while. Everyone sees it.
 *
 *  One rule, one counterplay: keep a light on it, as a team, while the others work.
 *  Server-authoritative: the server decides who sees it (flashlights on and aimed, in line of sight) and moves it;
 *  being frozen replicates, so every machine stops its animation together.
 */
UCLASS()
class THEFACILITY_API AFacilityGuest : public ACharacter
{
	GENERATED_BODY()

protected:

	UPROPERTY(ReplicatedUsing=OnRep_State)
	EGuestState State = EGuestState::Dormant;

	UPROPERTY(ReplicatedUsing=OnRep_Frozen)
	bool bFrozen = false;

	/** A flashlight holds it this far away */
	UPROPERTY(EditAnywhere, Category="Guest", meta=(Units="cm"))
	float LightRange = 1600.0f;

	/** Within this angle of where the light points */
	UPROPERTY(EditAnywhere, Category="Guest", meta=(Units="deg"))
	float LightHalfAngle = 22.0f;

	UPROPERTY(EditAnywhere, Category="Guest", meta=(Units="cm/s"))
	float Speed = 480.0f;

	UPROPERTY(EditAnywhere, Category="Guest", meta=(Units="cm"))
	float TakeRange = 115.0f;

	UPROPERTY(EditAnywhere, Category="Guest", meta=(Units="s"))
	float ManifestDelay = 5.0f;

	UPROPERTY(EditAnywhere, Category="Guest", meta=(Units="s"))
	float RestTime = 25.0f;

	UPROPERTY()
	TObjectPtr<USoundBase> ScrapeSound;

	TArray<FName> ArmedBy;
	bool bAwakeTonight = false;
	bool bArmed = false;
	bool bShutDown = false;

	FTransform HomeTransform;
	float StateTime = 0.0f;
	float ScrapeTimer = 0.0f;
	FTimerHandle ThinkTimer;
	FTimerHandle StateTimer;

	static constexpr float ThinkInterval = 0.1f;

public:

	AFacilityGuest();

	EGuestState GetGuestState() const { return State; }

	bool IsPresent() const { return State == EGuestState::Hunting; }

	bool IsFrozen() const { return bFrozen; }

	/** Server only, at the start of a night */
	void SetNightBinding(const TArray<FName>& SignatureIds, bool bAwake);

	const TArray<FName>& GetArmedBy() const { return ArmedBy; }

	/** Server only: wakes it now (QA, or a bound Offering) */
	void Wake(const FString& Reason);

	void ShutDown();

	/** Server: is any player's lit flashlight on it right now? */
	bool IsLit() const;

	virtual void Tick(float DeltaSeconds) override;

protected:

	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void OnRep_State();

	UFUNCTION()
	void OnRep_Frozen();

	UFUNCTION()
	void HandleSignatureRecorded(const FFacilitySignature& Signature);

	void BindToLedger();

	void SetGuestState(EGuestState NewState);

	void Appear();

	void Think();
};
