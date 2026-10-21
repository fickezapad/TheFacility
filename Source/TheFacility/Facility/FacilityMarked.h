// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "FacilityMarked.generated.h"

class AFacilityCharacter;
class APlayerState;
class UAudioComponent;
class ULightComponent;
struct FFacilitySignature;

UENUM(BlueprintType)
enum class EMarkedState : uint8
{
	/** Not armed yet, or withdrawn. Hidden and without collision for everyone */
	Dormant,
	/** Armed and about to appear. Lights near it start to fail */
	Manifesting,
	/** Drifting toward roughly where its mark is */
	Hunting,
	/** Has its mark in sight and goes straight for it */
	Chasing,
	/** Lost its mark and checks where it was last seen */
	Searching,
	/** Caught in a camera flash: present but frozen for a few seconds */
	Stunned
};

/**
 *  The Marked: the first Facility creature.
 *  It wakes when its Offering is signed and hunts only the player who signed it, its mark.
 *  The mark can't see it; everyone else can. The mark only gets indirect evidence: footsteps,
 *  lights failing near it, doors opening, its body blocking the way, and a dread pulse on the HUD.
 *
 *  If its mark is retained by the Facility, the bond passes to whoever next picks up the Offering they dropped.
 *  A camera flash stuns it briefly; signing the camera itself makes it return faster after a catch.
 *
 *  Everything that matters is decided on the server (activation, mark, state, AI, catching).
 *  Visibility is decided on each machine from the replicated mark, by hiding components locally,
 *  never by bHidden, because that replicates and would hide it for everyone.
 */
UCLASS()
class THEFACILITY_API AFacilityMarked : public ACharacter
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAudioComponent> FootstepAudio;

protected:

	/** Signing this Offering arms the creature. Matches SignatureId on a FacilityPickup */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Marked")
	FName ArmedBySignatureId = TEXT("MusicBox");

	/** Set by the night: what arms it tonight, instead of ArmedBySignatureId */
	TArray<FName> NightSignatureIds;
	bool bNightBound = false;

	/** Time between the signature and the creature appearing */
	UPROPERTY(EditAnywhere, Category="Marked", meta=(ClampMin=0, Units="s"))
	float ManifestDelay = 4.0f;

	/** After catching its mark it waits until they are back up, then stays away this long before manifesting again */
	UPROPERTY(EditAnywhere, Category="Marked", meta=(ClampMin=0, Units="s"))
	float WithdrawTime = 15.0f;

	/** Each of these signatures halves WithdrawTime: the Facility answers by sending it back sooner */
	UPROPERTY(EditAnywhere, Category="Marked")
	TArray<FName> HastenedBySignatureIds = { FName(TEXT("FlashCamera")) };

	/** WithdrawTime is scaled by this, after hastening signatures */
	float WithdrawScale = 1.0f;

	/** Lights within this distance flicker on every machine while the creature is present */
	UPROPERTY(EditAnywhere, Category="Marked|Evidence", meta=(ClampMin=0, Units="cm"))
	float LightDisturbRadius = 600.0f;

	/** Distance between footsteps */
	UPROPERTY(EditAnywhere, Category="Marked|Evidence", meta=(ClampMin=10, Units="cm"))
	float StrideLength = 120.0f;

	/** Within this distance the mark's HUD shows the dread pulse */
	UPROPERTY(EditAnywhere, Category="Marked|Evidence", meta=(ClampMin=0, Units="cm"))
	float DreadRadius = 900.0f;

	/** The player it hunts. Server sets it from the ledger; every machine uses it to decide visibility */
	UPROPERTY(ReplicatedUsing=OnRep_Presence, BlueprintReadOnly, Category="Marked")
	TObjectPtr<APlayerState> Mark;

	UPROPERTY(ReplicatedUsing=OnRep_Presence, BlueprintReadOnly, Category="Marked")
	EMarkedState State = EMarkedState::Dormant;

	/** True once its Offering has been signed. It arms only once per run */
	bool bArmed = false;

	/** Where it was placed; it appears here and withdraws back here */
	FTransform HomeTransform;

	FTimerHandle StateTimer;

	FTimerHandle StunTimer;

	/** The run is over: it stays dormant for good */
	bool bShutDown = false;

	/** Local cosmetic state */
	bool bLocallyVisible = true;
	float DistanceSinceStep = 0.0f;
	float LightFlickerCooldown = 0.0f;
	float StepStopTime = 0.0f;

	struct FDisturbedLight
	{
		TWeakObjectPtr<ULightComponent> Light;
		float BaseIntensity = 0.0f;
		bool bDisturbed = false;
	};
	TArray<FDisturbedLight> NearbyLights;

public:

	AFacilityMarked();

	UFUNCTION(BlueprintPure, Category="Marked")
	EMarkedState GetMarkedState() const { return State; }

	UFUNCTION(BlueprintPure, Category="Marked")
	APlayerState* GetMark() const { return Mark; }

	/** True while it is in the world for gameplay: visible to others, colliding (moving unless stunned) */
	UFUNCTION(BlueprintPure, Category="Marked")
	bool IsPresent() const { return State != EMarkedState::Dormant && State != EMarkedState::Manifesting; }

	/** Mark's pawn if it can currently be hunted (alive and not downed). Server and clients */
	AFacilityCharacter* GetHuntablePawn() const;

	/** 0 when far, 1 when on top of the given location. Used for the mark's HUD */
	float GetDreadAt(const FVector& Location) const;

	/** True if the local player on this machine is the mark */
	bool IsLocalPlayerMarked() const;

	/** Server only. Called by the AI */
	void SetMarkedState(EMarkedState NewState);

	/** Server only. The AI caught the mark: down them and withdraw for a while */
	void CatchMark();

	/** Server only. Freezes it where it stands, then it searches for its mark again */
	void Stun(float Duration);

	/** Server only. The run is over: withdraw and never come back */
	void ShutDown();

	/** Server only, at the start of a night: where it waits this time, appears and withdraws to */
	void MoveHome(const FTransform& Home);

	/** Server only, at the start of a night: the Offerings that arm it tonight (their signer becomes its mark). Not awake tonight, it never arms */
	void SetNightBinding(const TArray<FName>& SignatureIds, bool bAwakeTonight);

	const TArray<FName>& GetArmedBy() const { return NightSignatureIds; }

	/** Server only. Halves how long it stays away after a catch, like a hastening signature */
	void Hasten(const FString& Why);

protected:

	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual void Tick(float DeltaSeconds) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: binds to the ledger */
	void BindToLedger();

	UFUNCTION()
	void HandleSignatureRecorded(const FFacilitySignature& Signature);

	/** Server: starts manifesting; appears after ManifestDelay */
	void Manifest();

	/** Server: called when the manifest delay is over */
	void Appear();

	/** Server: the stun wore off */
	void EndStun();

	/** Server: the mark is gone (left the game). Returns false if it's still a valid player */
	bool HasValidMark() const;

	/** Server: after a catch, waits for the mark to get back up, then for WithdrawTime */
	void TryReturn();

	UFUNCTION()
	void OnRep_Presence();

	/** Applies collision, movement and local visibility for the current state */
	void ApplyPresence();

	/** Shows or hides the body on this machine only */
	void SetLocallyVisible(bool bVisible);

	/** Local cosmetics: footsteps and failing lights */
	void UpdateEvidence(float DeltaSeconds);

	void RestoreLights();
};
