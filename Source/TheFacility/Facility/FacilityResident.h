// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Facility/FacilityInteractable.h"
#include "FacilityResident.generated.h"

class UStaticMeshComponent;
class USoundBase;

UENUM(BlueprintType)
enum class EFacilityResidentKind : uint8
{
	/** An old woman in a long dress, standing with her face to the wall */
	OldWoman,
	/** A man in a vest sitting on the edge of a bed in the dark, head down */
	BedSitter,
	/** Someone sitting very still in a parked car */
	CarFigure,
	/** A figure standing in the drained pool's deep end, facing the wall */
	PoolFigure,
	/** A small figure at the end of a corridor */
	HallChild
};

/** What a resident does when someone comes close, decided by the seed */
UENUM(BlueprintType)
enum class EFacilityResidentReaction : uint8
{
	Nothing,
	/** Turns to look at you */
	Turn,
	/** A giggle */
	Giggle,
	/** A whisper of nothing much */
	Whisper,
	/** Gone the next time nobody looks at it */
	Vanish,
	/** Snaps toward you with a shriek: once, harmless */
	Jumpscare,
	/** Whispers where something the night needs is */
	Hint
};

/**
 *  A resident of the motel: a figure in a scene that never hurts anyone. A night shows three or four, chosen by its
 *  seed (by Group), the same on every machine. Come close (or look closely: E) and it reacts once, in the way the
 *  seed picked: nothing, a turn of the head, a giggle, a whisper, vanishing when no one is looking, a rare jumpscare,
 *  or a whisper of where something is.
 *
 *  Server-authoritative: the server shows it, triggers it and decides the reaction; what it does replicates.
 */
UCLASS()
class THEFACILITY_API AFacilityResident : public AActor, public IFacilityInteractable
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<USceneComponent> Root;

	/** What it turns: the whole figure pivots here */
	UPROPERTY(VisibleAnywhere, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<USceneComponent> Pivot;

	/** An invisible body to aim at and bump into */
	UPROPERTY(VisibleAnywhere, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Pieces;

	UPROPERTY()
	TMap<FName, TObjectPtr<UObject>> LookAssets;

protected:

	UPROPERTY(EditAnywhere, Category="Resident")
	EFacilityResidentKind Kind = EFacilityResidentKind::OldWoman;

	/** A night shows one resident per chosen group */
	UPROPERTY(EditAnywhere, Category="Resident")
	FName Group;

	/** How close someone comes before it reacts */
	UPROPERTY(EditAnywhere, Category="Resident", meta=(Units="cm"))
	float TriggerRange = 230.0f;

	UPROPERTY(ReplicatedUsing=OnRep_Shown)
	bool bShown = false;

	UPROPERTY(Replicated)
	EFacilityResidentReaction Reaction = EFacilityResidentReaction::Nothing;

	UPROPERTY(ReplicatedUsing=OnRep_Turn)
	float TurnYaw = 0.0f;

	UPROPERTY(ReplicatedUsing=OnRep_Shown)
	bool bVanished = false;

	bool bTriggered = false;
	bool bVanishPending = false;
	FTimerHandle ThinkTimer;

public:

	AFacilityResident();

	FName GetGroup() const { return Group; }
	EFacilityResidentKind GetKind() const { return Kind; }
	EFacilityResidentReaction GetReaction() const { return Reaction; }
	bool IsShown() const { return bShown && !bVanished; }
	bool HasReacted() const { return bTriggered; }

	/** Server only, at the start of a night: in the scene tonight or not, and how it reacts */
	void SetShownTonight(bool bInScene, uint32 SeedHash);

	/** Server only: someone came close, or looked closely */
	void Trigger(APawn* Visitor);

	// IFacilityInteractable: looking closely is pressing E on it
	virtual bool CanInteract_Implementation(APawn* InstigatorPawn) const override;
	virtual FText GetInteractPrompt_Implementation(APawn* InstigatorPawn) const override;
	virtual void Interact_Implementation(APawn* InstigatorPawn) override;

protected:

	virtual void OnConstruction(const FTransform& Transform) override;

	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Builds the figure for its kind out of low-poly pieces */
	void ApplyLook();

	void Think();

	/** Server: what it whispers for a Hint: where the nearest thing the night still needs is */
	FString FindHint() const;

	UFUNCTION()
	void OnRep_Shown();

	UFUNCTION()
	void OnRep_Turn();

	/** Every machine: the sound and words of its reaction. Words show to whoever is near */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_React(EFacilityResidentReaction What, const FString& Words);
};
