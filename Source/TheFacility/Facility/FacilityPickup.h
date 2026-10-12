// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Facility/FacilityInteractable.h"
#include "FacilityPickup.generated.h"

class UStaticMeshComponent;

/**
 *  Something a player can take: a key, a tool, or an Offering.
 *  If SignatureId is set, taking it is a transgression and is written to the Facility ledger.
 *  Taking is decided on the server; the taken state replicates to everyone, including late joiners.
 */
UCLASS()
class THEFACILITY_API AFacilityPickup : public AActor, public IFacilityInteractable
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> Mesh;

protected:

	/** Name shown in the interaction prompt */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pickup")
	FText DisplayName = NSLOCTEXT("Facility", "DefaultPickupName", "Object");

	/** Key given to the player who takes this. None gives no key */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pickup")
	FName GrantedKeyId;

	/** If set, this is an Offering: taking it signs this id into the Facility ledger */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Offering")
	FName SignatureId;

	/** Zone the signature is recorded in, used by environmental responses */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Offering")
	FName ZoneId;

	UPROPERTY(ReplicatedUsing=OnRep_Taken, BlueprintReadOnly, Category="Pickup")
	bool bTaken = false;

public:

	AFacilityPickup();

	UFUNCTION(BlueprintPure, Category="Pickup")
	bool IsOffering() const { return !SignatureId.IsNone(); }

	// IFacilityInteractable
	virtual bool CanInteract_Implementation(APawn* InstigatorPawn) const override;
	virtual FText GetInteractPrompt_Implementation(APawn* InstigatorPawn) const override;
	virtual void Interact_Implementation(APawn* InstigatorPawn) override;

protected:

	UFUNCTION()
	void OnRep_Taken();

	/** Called on every machine when the pickup is taken. Use for sounds and effects */
	UFUNCTION(BlueprintImplementableEvent, Category="Pickup")
	void BP_OnTaken();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
