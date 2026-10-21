// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityNight.h"
#include "Facility/FacilityCharacter.h"
#include "Facility/FacilityContainer.h"
#include "Facility/FacilityDoor.h"
#include "Facility/FacilityMarked.h"
#include "Facility/FacilityClerk.h"
#include "Facility/FacilityGuest.h"
#include "Facility/FacilityPickup.h"
#include "Facility/FacilityResident.h"
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
	const FName TreasureSpotTag(TEXT("TreasureSpot"));
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

TArray<FName> FacilityNight::FPlan::GetOfferings() const
{
	TArray<FName> Ids;
	for (const TPair<int32, FName>& Entry : Contents)
	{
		if (FacilityShift::IsOffering(Entry.Value))
		{
			Ids.AddUnique(Entry.Value);
		}
	}
	for (const TPair<FName, int32>& Entry : TreasureAt)
	{
		Ids.AddUnique(Entry.Key);
	}
	Ids.Sort(FNameLexicalLess());
	return Ids;
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
	TArray<FString> Awake, Bound, Shown;
	for (const EFacilityCreature Creature : Creatures)
	{
		Awake.Add(FacilityShift::CreatureName(Creature));
	}
	for (const FName Offering : GetOfferings())
	{
		const int32* Spot = TreasureAt.Find(Offering);
		Bound.Add(FString::Printf(TEXT("%s%s -> %s"), *Offering.ToString(),
			Spot && Site.TreasureSpots.IsValidIndex(*Spot) ? *FString::Printf(TEXT(" at %s"), *Site.TreasureSpots[*Spot]->GetName()) : TEXT(""),
			*FacilityShift::CreatureName(BoundTo(Offering))));
	}
	for (const FName Group : ResidentsShown)
	{
		Shown.Add(Group.ToString());
	}
	return FString::Printf(TEXT("seed %d night %d: locked [%s], Tenant in %s, Marked at start %d, gate key spot %s, items: %s; awake [%s]; offerings [%s]; residents [%s]%s"),
		Seed, Night, *FString::Join(Locked, TEXT(", ")), *TenantRoom.ToString(), MarkedStart, *GateKeySpot.ToString(), *FString::Join(Items, TEXT("; ")),
		*FString::Join(Awake, TEXT(", ")), *FString::Join(Bound, TEXT("; ")), *FString::Join(Shown, TEXT(", ")),
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
		else if (It->Tags.Contains(TreasureSpotTag))
		{
			Site.TreasureSpots.Add(*It);
		}
	}
	TSet<FName> ResidentGroups;
	for (TActorIterator<AFacilityResident> It(World); It; ++It)
	{
		Site.Residents.Add(*It);
		ResidentGroups.Add(It->GetGroup());
	}
	Site.ResidentGroups = ResidentGroups.Array();
	if (TActorIterator<AFacilityTenant>(World)) { Site.CreaturesHere.Add(EFacilityCreature::Tenant); }
	if (TActorIterator<AFacilityMarked>(World)) { Site.CreaturesHere.Add(EFacilityCreature::Marked); }
	if (TActorIterator<AFacilityClerk>(World)) { Site.CreaturesHere.Add(EFacilityCreature::Clerk); }
	if (TActorIterator<AFacilityGuest>(World)) { Site.CreaturesHere.Add(EFacilityCreature::Guest); }

	// names are the same on every machine and in every session; iteration order is not
	const auto ByName = [](const AActor& A, const AActor& B) { return A.GetName() < B.GetName(); };
	Site.Containers.Sort(ByName);
	Site.TenantStarts.Sort(ByName);
	Site.MarkedStarts.Sort(ByName);
	Site.TreasureSpots.Sort(ByName);
	Site.Residents.Sort(ByName);
	Site.ResidentGroups.Sort(FNameLexicalLess());
	Site.LockGroups.Sort(FNameLexicalLess());
	return Site;
}

FacilityNight::FPlan FacilityNight::Plan(int32 Seed, const FSite& Site, int32 Night)
{
	FPlan Plan;
	Plan.Seed = Seed;
	Plan.Night = FMath::Clamp(Night, 1, FacilityShift::NightsPerShift);
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

	// two rooms locked, never the Tenant's own, and only rooms with something in them to search: a locked room
	// should be worth the master key (or worth waiting for the Tenant to break it)
	TArray<FName> Lockable;
	for (const FName Group : Site.LockGroups)
	{
		const bool bHasContainer = Site.Containers.ContainsByPredicate([Group](const AFacilityContainer* Container) { return Container->GetArea() == Group; });
		if (bHasContainer && Group != Plan.TenantRoom)
		{
			Lockable.Add(Group);
		}
	}
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

	// ---- the shift's layer, drawn after everything above so a seed's items stay where they were ----

	// which creatures are awake to Offerings tonight: two on the first night, then three (sometimes two on the second)
	{
		TArray<EFacilityCreature> Pool = Site.CreaturesHere;
		Shuffle(Pool, Stream);
		int32 Count = FacilityShift::CreaturesForNight(Plan.Night);
		if (Plan.Night == 2 && Stream.FRand() < 0.4f)
		{
			Count = 2;
		}
		for (int32 Index = 0; Index < FMath::Min(Count, Pool.Num()); ++Index)
		{
			Plan.Creatures.Add(Pool[Index]);
		}
		Plan.Creatures.Sort([](EFacilityCreature A, EFacilityCreature B) { return (uint8)A < (uint8)B; });
	}

	// the treasures lying out: enough for three or four Offerings with the tools already hidden, in different places
	{
		const int32 Tools = (Plan.FindItem(MasterKeyId) != INDEX_NONE ? 1 : 0) + (Plan.FindItem(FlashCameraId) != INDEX_NONE ? 1 : 0);
		const int32 Wanted = FMath::Clamp(Stream.RandRange(3, 4) - Tools, 1, FacilityShift::Treasures().Num());
		TArray<FName> Kinds = FacilityShift::Treasures();
		Shuffle(Kinds, Stream);
		TArray<int32> SpotOrder;
		for (int32 Index = 0; Index < Site.TreasureSpots.Num(); ++Index)
		{
			SpotOrder.Add(Index);
		}
		Shuffle(SpotOrder, Stream);
		TSet<FName> UsedAreas;
		for (int32 Index = 0; Index < SpotOrder.Num() && Plan.TreasureAt.Num() < FMath::Min(Wanted, Kinds.Num()); ++Index)
		{
			const TArray<FName>& Tags = Site.TreasureSpots[SpotOrder[Index]]->Tags;
			const FName Area = Tags.Num() > 1 ? Tags[1] : NAME_None;
			if (!UsedAreas.Contains(Area))
			{
				UsedAreas.Add(Area);
				Plan.TreasureAt.Add(Kinds[Plan.TreasureAt.Num()], SpotOrder[Index]);
			}
		}
	}

	// each Offering wakes one of tonight's creatures; every creature gets at least one while there are enough
	{
		TArray<FName> Offerings = Plan.GetOfferings();
		Shuffle(Offerings, Stream);
		TArray<EFacilityCreature> Order = Plan.Creatures;
		Shuffle(Order, Stream);
		for (int32 Index = 0; Index < Offerings.Num() && !Order.IsEmpty(); ++Index)
		{
			const EFacilityCreature Creature = Index < Order.Num() ? Order[Index] : Order[Stream.RandRange(0, Order.Num() - 1)];
			Plan.Bindings.Add(Offerings[Index], Creature);
			Plan.HintVariant.Add(Offerings[Index], Stream.RandRange(0, 2));
		}
	}

	// three or four residents, each from a different group
	{
		TArray<FName> Groups = Site.ResidentGroups;
		Shuffle(Groups, Stream);
		const int32 Shown = FMath::Min(Stream.RandRange(3, 4), Groups.Num());
		for (int32 Index = 0; Index < Shown; ++Index)
		{
			Plan.ResidentsShown.Add(Groups[Index]);
		}
		Plan.ResidentsShown.Sort(FNameLexicalLess());
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
		// the master key and the flash camera are Offerings: taking one signs the register and wakes what it is bound
		// to. The fuses and the gate key are the work of the night: they sign nothing
		const FName Signature = FacilityShift::IsOffering(Entry.Value) ? Entry.Value : NAME_None;
		Site.Containers[Entry.Key]->SetContent(Entry.Value, AFacilityCharacter::GetItemName(Entry.Value), Signature, MotelZone);
	}

	// the treasures, lying out where they can be seen: taking one signs the register like any Offering
	for (const TPair<FName, int32>& Entry : Plan.TreasureAt)
	{
		const AActor* Spot = Site.TreasureSpots[Entry.Value];
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AFacilityPickup* Treasure = World->SpawnActor<AFacilityPickup>(AFacilityPickup::StaticClass(), Spot->GetActorLocation(), Spot->GetActorRotation(), SpawnParams))
		{
			Treasure->InitDropped(Entry.Key, FacilityShift::NameOf(Entry.Key), Entry.Key, MotelZone);
		}
	}

	// what each creature answers to tonight; a creature not awake tonight never comes
	const auto BoundTo = [&Plan](EFacilityCreature Creature)
	{
		TArray<FName> Ids;
		for (const TPair<FName, EFacilityCreature>& Binding : Plan.Bindings)
		{
			if (Binding.Value == Creature)
			{
				Ids.Add(Binding.Key);
			}
		}
		return Ids;
	};
	for (TActorIterator<AFacilityTenant> It(World); It; ++It)
	{
		It->SetNightBinding(BoundTo(EFacilityCreature::Tenant), Plan.Creatures.Contains(EFacilityCreature::Tenant));
	}
	for (TActorIterator<AFacilityMarked> It(World); It; ++It)
	{
		It->SetNightBinding(BoundTo(EFacilityCreature::Marked), Plan.Creatures.Contains(EFacilityCreature::Marked));
	}
	for (TActorIterator<AFacilityClerk> It(World); It; ++It)
	{
		It->SetNightBinding(BoundTo(EFacilityCreature::Clerk), Plan.Creatures.Contains(EFacilityCreature::Clerk));
	}
	for (TActorIterator<AFacilityGuest> It(World); It; ++It)
	{
		It->SetNightBinding(BoundTo(EFacilityCreature::Guest), Plan.Creatures.Contains(EFacilityCreature::Guest));
	}

	// tonight's residents, each with its own way of reacting, decided by the seed
	for (int32 Index = 0; Index < Site.Residents.Num(); ++Index)
	{
		Site.Residents[Index]->SetShownTonight(Plan.ResidentsShown.Contains(Site.Residents[Index]->GetGroup()), HashCombine(GetTypeHash(Plan.Seed), GetTypeHash(Index)));
	}

	UE_LOG(LogTheFacility, Log, TEXT("Night plan, %s"), *Plan.Describe(Site));
	if (!Plan.Problem.IsEmpty())
	{
		UE_LOG(LogTheFacility, Error, TEXT("Night plan for seed %d can't be completed: %s"), Plan.Seed, *Plan.Problem);
	}
}
