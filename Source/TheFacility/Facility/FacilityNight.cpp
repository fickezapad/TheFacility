// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityNight.h"
#include "Facility/FacilityCharacter.h"
#include "Facility/FacilityContainer.h"
#include "Facility/FacilityDoor.h"
#include "Facility/FacilityMarked.h"
#include "Facility/FacilityTenant.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TheFacility.h"

const FName FacilityNight::FuseId(TEXT("Fuse"));
const FName FacilityNight::GateKeyId(TEXT("GateKey"));
const FName FacilityNight::MasterKeyId(TEXT("MasterKey"));
const FName FacilityNight::FlashCameraId(TEXT("FlashCamera"));

namespace
{
	const FName TenantStartTag(TEXT("TenantStart"));
	const FName MarkedStartTag(TEXT("MarkedStart"));
	const FName ManagerArea(TEXT("Manager"));
	const FName LaundryArea(TEXT("Laundry"));
	const FName MotelZone(TEXT("Motel"));

	template <typename T>
	void Shuffle(TArray<T>& Items, FRandomStream& Stream)
	{
		for (int32 Index = Items.Num() - 1; Index > 0; --Index)
		{
			Items.Swap(Index, Stream.RandRange(0, Index));
		}
	}

	/** A random entry of Candidates that isn't taken yet and passes Filter, or INDEX_NONE */
	int32 PickFree(const TArray<int32>& Candidates, const TSet<int32>& Taken, FRandomStream& Stream, TFunctionRef<bool(int32)> Filter)
	{
		TArray<int32> Free;
		for (const int32 Index : Candidates)
		{
			if (!Taken.Contains(Index) && Filter(Index))
			{
				Free.Add(Index);
			}
		}
		return Free.IsEmpty() ? INDEX_NONE : Free[Stream.RandRange(0, Free.Num() - 1)];
	}
}

int32 FacilityNight::FPlan::FindItem(FName ItemId) const
{
	for (const TPair<int32, FName>& Entry : Contents)
	{
		if (Entry.Value == ItemId)
		{
			return Entry.Key;
		}
	}
	return INDEX_NONE;
}

bool FacilityNight::FPlan::IsInLockedRoom(const FSite& Site, int32 ContainerIndex) const
{
	return Site.Containers.IsValidIndex(ContainerIndex) && LockedRooms.Contains(Site.Containers[ContainerIndex]->GetArea());
}

FString FacilityNight::FPlan::Describe(const FSite& Site) const
{
	TArray<FString> Items;
	TArray<int32> Indices;
	Contents.GenerateKeyArray(Indices);
	Indices.Sort();
	for (const int32 Index : Indices)
	{
		const AFacilityContainer* Container = Site.Containers[Index];
		Items.Add(FString::Printf(TEXT("%s in %s (%s%s)"), *Contents[Index].ToString(), *Container->GetName(), *Container->GetArea().ToString(),
			IsInLockedRoom(Site, Index) ? TEXT(", locked") : TEXT("")));
	}
	TArray<FString> Locked;
	for (const FName Room : LockedRooms)
	{
		Locked.Add(Room.ToString());
	}
	return FString::Printf(TEXT("seed %d: locked [%s], Tenant in %s, Marked at start %d, gate key spot %s, items: %s%s"),
		Seed, *FString::Join(Locked, TEXT(", ")), *TenantRoom.ToString(), MarkedStart, *GateKeySpot.ToString(), *FString::Join(Items, TEXT("; ")),
		Problem.IsEmpty() ? TEXT("") : *(TEXT(". PROBLEM: ") + Problem));
}

FacilityNight::FSite FacilityNight::GatherSite(UWorld* World)
{
	FSite Site;
	if (!World)
	{
		return Site;
	}

	for (TActorIterator<AFacilityContainer> It(World); It; ++It)
	{
		Site.Containers.Add(*It);
	}
	TSet<FName> Groups;
	for (TActorIterator<AFacilityDoor> It(World); It; ++It)
	{
		if (!It->GetLockGroup().IsNone())
		{
			Groups.Add(It->GetLockGroup());
		}
	}
	Site.LockGroups = Groups.Array();
	for (TActorIterator<ATargetPoint> It(World); It; ++It)
	{
		if (It->Tags.Contains(TenantStartTag))
		{
			Site.TenantStarts.Add(*It);
		}
		else if (It->Tags.Contains(MarkedStartTag))
		{
			Site.MarkedStarts.Add(*It);
		}
	}

	// names are the same on every machine and in every session; iteration order is not
	const auto ByName = [](const AActor& A, const AActor& B) { return A.GetName() < B.GetName(); };
	Site.Containers.Sort(ByName);
	Site.TenantStarts.Sort(ByName);
	Site.MarkedStarts.Sort(ByName);
	Site.LockGroups.Sort(FNameLexicalLess());
	return Site;
}

FacilityNight::FPlan FacilityNight::Plan(int32 Seed, const FSite& Site)
{
	FPlan Plan;
	Plan.Seed = Seed;
	if (!Site.IsNight())
	{
		return Plan;
	}

	FRandomStream Stream(Seed);

	// the creatures' starts
	if (!Site.TenantStarts.IsEmpty())
	{
		Plan.TenantStart = Stream.RandRange(0, Site.TenantStarts.Num() - 1);
		const TArray<FName>& Tags = Site.TenantStarts[Plan.TenantStart]->Tags;
		Plan.TenantRoom = Tags.Num() > 1 ? Tags[1] : NAME_None;
	}
	if (!Site.MarkedStarts.IsEmpty())
	{
		Plan.MarkedStart = Stream.RandRange(0, Site.MarkedStarts.Num() - 1);
	}

	// two rooms locked, never the Tenant's own
	TArray<FName> Lockable = Site.LockGroups;
	Lockable.Remove(Plan.TenantRoom);
	Shuffle(Lockable, Stream);
	for (int32 Index = 0; Index < FMath::Min(2, Lockable.Num()); ++Index)
	{
		Plan.LockedRooms.Add(Lockable[Index]);
	}
	Plan.LockedRooms.Sort(FNameLexicalLess());

	TArray<int32> All;
	for (int32 Index = 0; Index < Site.Containers.Num(); ++Index)
	{
		All.Add(Index);
	}
	TSet<int32> Taken;
	const auto AreaOf = [&Site](int32 Index) { return Site.Containers[Index]->GetArea(); };
	const auto Locked = [&Plan, &Site](int32 Index) { return Plan.IsInLockedRoom(Site, Index); };

	// the gate key: in a locked guest room, in the manager's back room, or in a laundry cart
	TArray<FName> Spots;
	if (All.ContainsByPredicate(Locked))
	{
		Spots.Add(TEXT("LockedRoom"));
	}
	if (All.ContainsByPredicate([&](int32 Index) { return AreaOf(Index) == ManagerArea; }))
	{
		Spots.Add(TEXT("Manager"));
	}
	if (All.ContainsByPredicate([&](int32 Index) { return AreaOf(Index) == LaundryArea && Site.Containers[Index]->GetKind() == EFacilityContainerKind::Cart; }))
	{
		Spots.Add(TEXT("Laundry"));
	}
	if (!Spots.IsEmpty())
	{
		Plan.GateKeySpot = Spots[Stream.RandRange(0, Spots.Num() - 1)];
		const int32 KeyAt = PickFree(All, Taken, Stream, [&](int32 Index)
		{
			if (Plan.GateKeySpot == TEXT("LockedRoom")) { return Locked(Index); }
			if (Plan.GateKeySpot == TEXT("Manager")) { return AreaOf(Index) == ManagerArea; }
			return AreaOf(Index) == LaundryArea && Site.Containers[Index]->GetKind() == EFacilityContainerKind::Cart;
		});
		if (KeyAt != INDEX_NONE)
		{
			Plan.Contents.Add(KeyAt, GateKeyId);
			Taken.Add(KeyAt);
		}
	}

	// two fuses, in two different parts of the motel
	const int32 FuseA = PickFree(All, Taken, Stream, [](int32) { return true; });
	if (FuseA != INDEX_NONE)
	{
		Plan.Contents.Add(FuseA, FuseId);
		Taken.Add(FuseA);
		const int32 FuseB = PickFree(All, Taken, Stream, [&](int32 Index) { return AreaOf(Index) != AreaOf(FuseA); });
		if (FuseB != INDEX_NONE)
		{
			Plan.Contents.Add(FuseB, FuseId);
			Taken.Add(FuseB);
		}
	}

	// the master key: always there if something needed is locked away, otherwise most nights. Never itself locked away
	const bool bNeeded = [&]()
	{
		for (const TPair<int32, FName>& Entry : Plan.Contents)
		{
			if (Locked(Entry.Key))
			{
				return true;
			}
		}
		return false;
	}();
	Plan.bMasterKey = bNeeded || Stream.FRand() < 0.6f;
	if (Plan.bMasterKey)
	{
		const int32 MasterAt = PickFree(All, Taken, Stream, [&](int32 Index) { return !Locked(Index); });
		if (MasterAt != INDEX_NONE)
		{
			Plan.Contents.Add(MasterAt, MasterKeyId);
			Taken.Add(MasterAt);
		}
	}

	// the flash camera, some nights
	Plan.bFlashCamera = Stream.FRand() < 0.6f;
	if (Plan.bFlashCamera)
	{
		const int32 CameraAt = PickFree(All, Taken, Stream, [](int32) { return true; });
		if (CameraAt != INDEX_NONE)
		{
			Plan.Contents.Add(CameraAt, FlashCameraId);
			Taken.Add(CameraAt);
		}
	}

	// completable? The fuses and the gate key are all there, and whatever is locked away has an unlocked master key
	int32 NumFuses = 0;
	for (const TPair<int32, FName>& Entry : Plan.Contents)
	{
		NumFuses += Entry.Value == FuseId ? 1 : 0;
	}
	const int32 MasterAt = Plan.FindItem(MasterKeyId);
	const bool bMasterReachable = MasterAt != INDEX_NONE && !Locked(MasterAt);
	if (NumFuses < 2)
	{
		Plan.Problem = TEXT("fewer than two fuses");
	}
	else if (Plan.FindItem(GateKeyId) == INDEX_NONE)
	{
		Plan.Problem = TEXT("no gate key");
	}
	else
	{
		for (const TPair<int32, FName>& Entry : Plan.Contents)
		{
			const bool bRequired = Entry.Value == FuseId || Entry.Value == GateKeyId;
			if (bRequired && Locked(Entry.Key) && !bMasterReachable)
			{
				Plan.Problem = FString::Printf(TEXT("%s is locked away and no master key can reach it"), *Entry.Value.ToString());
				break;
			}
		}
	}
	return Plan;
}

void FacilityNight::Apply(UWorld* World, const FPlan& Plan, const FSite& Site)
{
	if (!World || !Site.IsNight())
	{
		return;
	}

	for (TActorIterator<AFacilityDoor> It(World); It; ++It)
	{
		if (Plan.LockedRooms.Contains(It->GetLockGroup()))
		{
			It->LockForNight();
		}
	}

	if (Site.TenantStarts.IsValidIndex(Plan.TenantStart))
	{
		for (TActorIterator<AFacilityTenant> It(World); It; ++It)
		{
			It->MoveHome(Site.TenantStarts[Plan.TenantStart]->GetActorTransform());
		}
	}
	if (Site.MarkedStarts.IsValidIndex(Plan.MarkedStart))
	{
		for (TActorIterator<AFacilityMarked> It(World); It; ++It)
		{
			It->MoveHome(Site.MarkedStarts[Plan.MarkedStart]->GetActorTransform());
		}
	}

	for (const TPair<int32, FName>& Entry : Plan.Contents)
	{
		// the fuse, the master key and the flash camera are Offerings: taking one signs the register. The gate key isn't
		const FName Signature = Entry.Value == GateKeyId ? NAME_None : Entry.Value;
		Site.Containers[Entry.Key]->SetContent(Entry.Value, AFacilityCharacter::GetItemName(Entry.Value), Signature, MotelZone);
	}

	UE_LOG(LogTheFacility, Log, TEXT("Night plan, %s"), *Plan.Describe(Site));
	if (!Plan.Problem.IsEmpty())
	{
		UE_LOG(LogTheFacility, Error, TEXT("Night plan for seed %d can't be completed: %s"), Plan.Seed, *Plan.Problem);
	}
}
