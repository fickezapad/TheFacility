// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Facility/FacilityInteractable.h"
#include "FacilityHidingSpot.generated.h"

class AFacilityCharacter;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EFacilityHidingKind : uint8
{
	/** Quick in and out, but the door creaks */
	Closet,
	/** Silent, but crawling back out takes a moment */
	UnderBed
};

/**
 *  Somewhere to hide: a closet, or the space under a bed. One player at a time.
 *
 *  Hidden, a player can't move, makes no sound, and can't be seen. Each threat treats that differently:
 *    - The Tenant can't find a hidden player at all, but it hears a closet door creak.
 *    - The Marked loses its mark unless it saw them go in; then it walks over and drags them out.
 *    - The Facility doesn't allow it for long: after MaxStayTime it forces the spot open and takes notice.
 *
 *  Server-authoritative: who is inside is decided and replicated by the server. The hidden player's own camera
 *  looks out from the spot's eye point.
 */
UCLASS()
class THEFACILITY_API AFacilityHidingSpot : public AActor, public IFacilityInteractable
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> Mesh;

protected:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hiding")
	EFacilityHidingKind Kind = EFacilityHidingKind::Closet;

	/** The Facility forces the spot open after this long */
	UPROPERTY(EditAnywhere, Category="Hiding", meta=(ClampMin=5, Units="s"))
	float MaxStayTime = 45.0f;

	/** Attention the Facility gains when it has to force someone out */
	UPROPERTY(EditAnywhere, Category="Hiding", meta=(ClampMin=0))
	float ForcedOutAttention = 6.0f;

	/** How far a closet door carries, for the Tenant. Under a bed is silent */
	UPROPERTY(EditAnywhere, Category="Hiding", meta=(ClampMin=0, Units="cm"))
	float CreakNoiseRadius = 400.0f;

	/** How far it carries when the Facility forces it open */
	UPROPERTY(EditAnywhere, Category="Hiding", meta=(ClampMin=0, Units="cm"))
	float ForcedNoiseRadius = 1000.0f;

	/** Crawling out from under a bed takes this long; a closet is instant */
	UPROPERTY(EditAnywhere, Category="Hiding", meta=(ClampMin=0, Units="s"))
	float CrawlOutTime = 1.2f;

	/** A closet door, going in or out */
	UPROPERTY(EditAnywhere, Category="Hiding|Sound")
	TObjectPtr<class USoundBase> CreakSound;

	UPROPERTY(ReplicatedUsing=OnRep_Occupant, BlueprintReadOnly, Category="Hiding")
	TObjectPtr<AFacilityCharacter> Occupant;

	/** Server: world time the occupant went in */
	double EnteredTime = 0.0;

	/** Server: crawling out has started and finishes at this time; 0 when not crawling */
	double LeavingAt = 0.0;

	FTimerHandle StayTimer;

	FTimerHandle CrawlTimer;

public:

	AFacilityHidingSpot();

	AFacilityCharacter* GetOccupant() const { return Occupant; }

	EFacilityHidingKind GetKind() const { return Kind; }

	/** The camera transform for whoever hides here */
	FTransform GetEyeTransform() const;

	/** A point InFront cm ahead of its front face and AboveFloor cm above the floor it stands on */
	FVector GetSpotPoint(float InFront, float AboveFloor) const;

	/** Server only. Puts a standing player inside. Returns false if it is taken or they can't */
	bool Enter(AFacilityCharacter* Character);

	/** Server only. The occupant asked to come out: at once from a closet, after a crawl from under a bed */
	void RequestLeave(AFacilityCharacter* Character);

	/** Server only. Puts the occupant back outside now, whatever they want. Bed or closet, it is immediate */
	void Evict(const FString& Why);

	/** Seconds the occupant has left before the Facility forces them out. Any machine, approximate on clients */
	float GetTimeLeft() const;

	// IFacilityInteractable
	virtual bool CanInteract_Implementation(APawn* InstigatorPawn) const override;
	virtual FText GetInteractPrompt_Implementation(APawn* InstigatorPawn) const override;
	virtual void Interact_Implementation(APawn* InstigatorPawn) override;

protected:

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void OnRep_Occupant();

	/** Server: MaxStayTime is up */
	void ForceOut();

	/** Server: the crawl out from under the bed is over */
	void FinishCrawl();

	/** Local: the closet door is heard on this machine */
	void PlayCreak();

	/** Server: the closet door creaks, for the Tenant */
	void Creak(const AActor* Source);

	/** Replicated with the occupant, for the HUD countdown on clients */
	UPROPERTY(Replicated)
	float ReplicatedEnteredTime = 0.0f;
};
