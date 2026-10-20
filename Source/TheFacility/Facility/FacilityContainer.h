// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Facility/FacilityInteractable.h"
#include "FacilityContainer.generated.h"

class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EFacilityContainerKind : uint8
{
	Dresser,
	Nightstand,
	Desk,
	Cabinet,
	/** A laundry cart: a canvas bin on a metal frame */
	Cart
};

/**
 *  Something to search: a dresser, a nightstand, a desk drawer, a cabinet, a laundry cart.
 *  At the start of a night the game mode hides the night's items in some of them (fuses, the gate key, Offerings).
 *
 *  Searching is held: Interact starts it, letting go (or walking off, or going down) stops it, and after SearchTime it
 *  is done. It makes a small noise when it starts. A searched container stays open for everyone, and whatever was in it
 *  lies in the open drawer as an ordinary pickup, to be taken (and, for an Offering, signed for) like any other.
 *
 *  Server-authoritative: the server decides contents, who is searching and when it is done; everyone sees it open.
 */
UCLASS()
class THEFACILITY_API AFacilityContainer : public AActor, public IFacilityInteractable
{
	GENERATED_BODY()

	/** The furniture itself; blocks movement like any piece of furniture */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> Body;

	/** The part that opens: a drawer that slides out of the front (+X), or a cart's canvas bin */
	UPROPERTY(VisibleAnywhere, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> Drawer;

	UPROPERTY(VisibleAnywhere, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> Handle;

	/** Shapes and materials by kind, loaded once */
	UPROPERTY()
	TMap<FName, TObjectPtr<UObject>> LookAssets;

protected:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Container")
	EFacilityContainerKind Kind = EFacilityContainerKind::Dresser;

	/** Width (Y), depth (X) and height (Z) of the furniture, in cm. It stands on the floor at the actor's location */
	UPROPERTY(EditAnywhere, Category="Container")
	FVector Size = FVector(60.0f, 110.0f, 90.0f);

	/** Where it is: Room1..Room7, Office, Manager, Laundry, Corridor. A locked room's containers share its doors' LockGroup */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Container")
	FName Area;

	/** Holding Interact this long finds what is inside */
	UPROPERTY(EditAnywhere, Category="Container", meta=(ClampMin=0.1, Units="s"))
	float SearchTime = 1.5f;

	/** How far the sound of a search carries */
	UPROPERTY(EditAnywhere, Category="Container", meta=(ClampMin=0, Units="cm"))
	float SearchNoiseRadius = 400.0f;

	/** A searcher further than this from the front gives up */
	UPROPERTY(EditAnywhere, Category="Container", meta=(ClampMin=0, Units="cm"))
	float MaxSearchDistance = 320.0f;

	/** Open for everyone once searched */
	UPROPERTY(ReplicatedUsing=OnRep_Searched, BlueprintReadOnly, Category="Container")
	bool bSearched = false;

	/** Who is searching it right now, if anyone, and since when (server world time) */
	UPROPERTY(ReplicatedUsing=OnRep_Searcher)
	TObjectPtr<APawn> Searcher;

	UPROPERTY(Replicated)
	double SearchStartTime = 0.0;

	/** Server only: what is inside. None is empty */
	FName ContentKeyId;
	FText ContentName;
	FName ContentSignatureId;
	FName ContentZoneId;

	/** How far the drawer is out, 0..1, animated on every machine */
	float OpenAlpha = 0.0f;

public:

	AFacilityContainer();

	EFacilityContainerKind GetKind() const { return Kind; }

	FName GetArea() const { return Area; }

	bool IsSearched() const { return bSearched; }

	/** Server only: what the night hid here */
	FName GetContentKeyId() const { return ContentKeyId; }

	/** Server only: puts an item inside for this night. An Offering keeps its signature and zone */
	void SetContent(FName KeyId, const FText& Name, FName SignatureId, FName ZoneId);

	/** 0..1 for the pawn searching it right now, else -1. Any machine */
	float GetSearchProgressFor(const APawn* Pawn) const;

	/** A point on the floor in front of it, where a searcher stands */
	FVector GetFrontPoint(float InFront = 70.0f) const;

	/** Server only: the searcher let go */
	void StopSearch(const APawn* Pawn);

	/** QA, server only: the search completes at once for this pawn, wherever it is */
	void CompleteSearchNow(APawn* Pawn);

	// IFacilityInteractable
	virtual bool CanInteract_Implementation(APawn* InstigatorPawn) const override;
	virtual FText GetInteractPrompt_Implementation(APawn* InstigatorPawn) const override;
	virtual void Interact_Implementation(APawn* InstigatorPawn) override;

	virtual void Tick(float DeltaSeconds) override;

protected:

	virtual void OnConstruction(const FTransform& Transform) override;

	/** Lays the look out again, so a placed container never depends on its saved component transforms */
	virtual void BeginPlay() override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Lays out the body, drawer and handle for Kind and Size */
	void ApplyLook();

	/** Server only: the search is done. Opens it and puts what was inside out where it can be taken */
	void FinishSearch(APawn* Pawn);

	double GetServerTime() const;

	UFUNCTION()
	void OnRep_Searched();

	/** Everyone nearby hears the rummaging start */
	UFUNCTION()
	void OnRep_Searcher();
};
