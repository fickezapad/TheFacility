// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "FacilityTenant.generated.h"

class AFacilityCharacter;
class UAudioComponent;
class USoundBase;
struct FFacilitySignature;

UENUM(BlueprintType)
enum class ETenantState : uint8
{
	/** Not awake yet, or the run is over. Hidden and without collision for everyone */
	Dormant,
	/** Woken and about to come out of its room */
	Manifesting,
	/** Walks the building slowly, going nowhere in particular */
	Wandering,
	/** Heard something far off and walks over to see */
	Investigating,
	/** Heard something close and runs at it */
	Rushing,
	/** Stands where the noise was, waiting for the next sound */
	Listening,
	/** Beats down a locked door in its way */
	Breaking
};

/**
 *  The Tenant: the second Facility creature, built as the opposite of The Marked.
 *  Everyone can see it, and it hunts everyone, but it is blind. It goes where it hears noise:
 *  running, doors, the fuse box, a camera flash, someone going down. A player who keeps still next to it
 *  is safe unless it walks into them. Locked doors don't stop it, it beats them down, loudly.
 *
 *  It wakes when the master key is signed for, or a while after the power comes back, whichever is first.
 *
 *  Server-authoritative like The Marked: waking, hearing, state and catching are decided on the server only.
 *  Its body is shown or hidden for everyone, so plain actor hiding (which replicates) is fine here.
 */
UCLASS()
class THEFACILITY_API AFacilityTenant : public ACharacter
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAudioComponent> FootstepAudio;

	/** A short cry when it starts running at a sound, so players learn what set it off */
	UPROPERTY(VisibleAnywhere, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAudioComponent> CryAudio;

	/** How far the body leans forward over its feet, in degrees */
	static constexpr float HunchDegrees = 16.0f;

protected:

	UPROPERTY(EditAnywhere, Category="Tenant|Sound")
	TObjectPtr<USoundBase> StepSound;

	/** Beating on a locked door */
	UPROPERTY(EditAnywhere, Category="Tenant|Sound")
	TObjectPtr<USoundBase> KnockSound;

	UPROPERTY(EditAnywhere, Category="Tenant|Sound")
	TObjectPtr<USoundBase> CrySound;

	/** Signing any of these Offerings wakes it. Matches SignatureId on a FacilityPickup */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tenant")
	TArray<FName> ArmedBySignatureIds = { FName(TEXT("MasterKey")) };

	/** Also wakes once the power is back, if nothing woke it before */
	UPROPERTY(EditAnywhere, Category="Tenant")
	bool bWakeOnPowerRestored = true;

	/** Time between being woken by a signature and coming out */
	UPROPERTY(EditAnywhere, Category="Tenant", meta=(ClampMin=0, Units="s"))
	float ManifestDelay = 6.0f;

	/** Time between the power coming back and coming out */
	UPROPERTY(EditAnywhere, Category="Tenant", meta=(ClampMin=0, Units="s"))
	float PowerWakeDelay = 12.0f;

	/** After catching someone it ignores sounds this long and heads back toward its room */
	UPROPERTY(EditAnywhere, Category="Tenant", meta=(ClampMin=0, Units="s"))
	float SatedTime = 10.0f;

	/** A noise this much quieter than the one it is already following doesn't distract it, unless that one is old */
	UPROPERTY(EditAnywhere, Category="Tenant|Hearing", meta=(ClampMin=0, ClampMax=1))
	float DistractionRatio = 0.7f;

	/** A remembered noise older than this can be replaced by any new one */
	UPROPERTY(EditAnywhere, Category="Tenant|Hearing", meta=(ClampMin=0, Units="s"))
	float NoiseMemory = 2.0f;

	/** Distance between footsteps */
	UPROPERTY(EditAnywhere, Category="Tenant|Evidence", meta=(ClampMin=10, Units="cm"))
	float StrideLength = 150.0f;

	UPROPERTY(ReplicatedUsing=OnRep_State, BlueprintReadOnly, Category="Tenant")
	ETenantState State = ETenantState::Dormant;

	/** Woken once per run */
	bool bArmed = false;

	/** The run is over: it stays dormant for good */
	bool bShutDown = false;

	/** Where it was placed; it comes out here */
	FTransform HomeTransform;

	FTimerHandle StateTimer;

	/** Server: the sound it is following */
	bool bHasNoise = false;
	bool bNoiseIsNew = false;
	FVector NoiseLocation = FVector::ZeroVector;
	float NoiseLoudness = 0.0f;
	double NoiseTime = 0.0;

	/** Server: world time until which it ignores sounds */
	double SatedUntil = 0.0;

	/** Local cosmetic state */
	ETenantState CosmeticState = ETenantState::Dormant;
	float DistanceSinceStep = 0.0f;
	float StepStopTime = 0.0f;
	float KnockCooldown = 0.0f;
	float CryStopTime = 0.0f;

public:

	AFacilityTenant();

	/**
	 *  Server only. Something made a sound at Location that carries Radius cm.
	 *  Every awake Tenant within Radius hears it; the closer, the louder. Source is never heard by itself.
	 */
	static void ReportNoise(UWorld* World, const FVector& Location, float Radius, const AActor* Source, bool bCountForDirector = true);

	/** Server only. Someone is talking inside a hiding spot. If it is listening this close, it pulls them out */
	void HearVoiceFromHiding(AFacilityCharacter* Talker, class AFacilityHidingSpot* Spot, float Range);

	UFUNCTION(BlueprintPure, Category="Tenant")
	ETenantState GetTenantState() const { return State; }

	/** True while it is in the world for gameplay: visible, colliding, moving */
	UFUNCTION(BlueprintPure, Category="Tenant")
	bool IsPresent() const { return State != ETenantState::Dormant && State != ETenantState::Manifesting; }

	/** Server: true while it has just caught someone and ignores sounds */
	bool IsSated() const;

	/** Server only. Hands the AI the newest sound worth following, once. Returns false if there is none */
	bool TakeNewNoise(FVector& OutLocation);

	/** Server only. Forgets the current sound (it got there, or gave up) */
	void ForgetNoise() { bHasNoise = false; bNoiseIsNew = false; }

	const FTransform& GetHomeTransform() const { return HomeTransform; }

	/** Server only. Called by the AI */
	void SetTenantState(ETenantState NewState);

	/** Server only. It got hold of a player: down them, then lose interest for a while */
	void CatchPlayer(AFacilityCharacter* Victim);

	/** Server only. The run is over: back to its room for good */
	void ShutDown();

	/** Server only. Wakes it now if it still sleeps, whatever was signed */
	void WakeNow(const FString& Reason);

protected:

	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual void Tick(float DeltaSeconds) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: one sound reached it */
	void HearNoise(const FVector& Location, float Radius, const AActor* Source);

	UFUNCTION()
	void HandleSignatureRecorded(const FFacilitySignature& Signature);

	UFUNCTION()
	void HandlePowerRestored();

	/** Server: wakes it, coming out after Delay */
	void Wake(float Delay, const FString& Reason);

	/** Server: the manifest delay is over */
	void Appear();

	UFUNCTION()
	void OnRep_State();

	/** Applies visibility, collision and movement for the current state */
	void ApplyPresence();

	/** Local cosmetics: footsteps, knocking on doors, the cry */
	void UpdateEvidence(float DeltaSeconds);
};
