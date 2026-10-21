// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Facility/FacilityShift.h"

class AActor;
class AFacilityContainer;
class AFacilityResident;
class UWorld;

/**
 *  A night at the motel, planned from its seed: which rooms are locked, where the creatures start, which Offerings
 *  turn up, and which containers hide what. The plan is a pure function of the seed and the map, so every machine
 *  (and the seed validator) gets the same night from the same seed; only the server applies it.
 *
 *  Fairness: the fuses and the gate key are always there. Anything needed that sits behind a locked door means the
 *  master key is there too, somewhere that isn't locked. The validator checks that, and that every place an item can
 *  be is reachable on the navmesh.
 */
namespace FacilityNight
{
	/** What the map offers the planner. Gathered once from the level, sorted by name so it is the same on every machine */
	struct FSite
	{
		TArray<AFacilityContainer*> Containers;
		/** Rooms whose doors can be locked for a night (the doors' LockGroup). Only those with a container get locked */
		TArray<FName> LockGroups;
		/** Target points tagged TenantStart or MarkedStart; a TenantStart's second tag is the room it is in */
		TArray<AActor*> TenantStarts;
		TArray<AActor*> MarkedStarts;
		/** Target points tagged TreasureSpot, where a night's treasures lie in the open; the second tag is the area */
		TArray<AActor*> TreasureSpots;
		/** The residents placed in the map; a night shows a few of them, chosen by group */
		TArray<AFacilityResident*> Residents;
		TArray<FName> ResidentGroups;
		/** Which creatures the map has at all */
		TArray<EFacilityCreature> CreaturesHere;

		bool IsNight() const { return !Containers.IsEmpty(); }
	};

	struct FPlan
	{
		int32 Seed = 0;
		TArray<FName> LockedRooms;
		int32 TenantStart = INDEX_NONE;
		FName TenantRoom;
		int32 MarkedStart = INDEX_NONE;
		/** Where the gate key went: LockedRoom, Manager or Laundry */
		FName GateKeySpot;
		/** Container index -> item id */
		TMap<int32, FName> Contents;
		bool bMasterKey = false;
		bool bFlashCamera = false;
		/** Night of the shift, 1..3: later nights wake more */
		int32 Night = 1;
		/** The creatures awake to Offerings tonight */
		TArray<EFacilityCreature> Creatures;
		/** Treasure id -> index into TreasureSpots */
		TMap<FName, int32> TreasureAt;
		/** Offering id -> the creature taking it wakes, and which register line describes it */
		TMap<FName, EFacilityCreature> Bindings;
		TMap<FName, int32> HintVariant;
		/** Resident groups shown tonight */
		TArray<FName> ResidentsShown;
		/** Empty when the night is completable */
		FString Problem;

		int32 FindItem(FName ItemId) const;
		bool IsInLockedRoom(const FSite& Site, int32 ContainerIndex) const;
		FString Describe(const FSite& Site) const;

		/** Every Offering present tonight: the tools the containers hide and the treasures lying out, sorted */
		TArray<FName> GetOfferings() const;

		/** The creatures bound to anything, by Offering, for the register */
		EFacilityCreature BoundTo(FName ItemId) const { const EFacilityCreature* Found = Bindings.Find(ItemId); return Found ? *Found : EFacilityCreature::None; }
	};

	/** Item ids the night hides */
	extern const FName FuseId;
	extern const FName GateKeyId;
	extern const FName MasterKeyId;
	extern const FName FlashCameraId;

	FSite GatherSite(UWorld* World);

	/** The night for this seed, checked for completability (Problem says why not) */
	FPlan Plan(int32 Seed, const FSite& Site, int32 Night = 1);

	/** Server only: locks the rooms, moves the creatures to their starts and fills the containers */
	void Apply(UWorld* World, const FPlan& Plan, const FSite& Site);
}
