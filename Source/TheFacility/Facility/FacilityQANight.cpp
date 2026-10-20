// Copyright Epic Games, Inc. All Rights Reserved.

// QA console commands for the night: its plan, the seed validator, a route estimate of how long a night takes, and
// searching containers. Not in shipping builds. Results are logged as FACILITY_QA_CHECK / FACILITY_QA_NIGHT lines.

#if !UE_BUILD_SHIPPING

#include "Facility/FacilityCharacter.h"
#include "Facility/FacilityContainer.h"
#include "Facility/FacilityDoor.h"
#include "Facility/FacilityExit.h"
#include "Facility/FacilityTenant.h"
#include "Facility/FacilityFuseBox.h"
#include "Facility/FacilityGameInstance.h"
#include "Facility/FacilityGameState.h"
#include "Facility/FacilityInteractionComponent.h"
#include "Facility/FacilityNight.h"
#include "GameFramework/PlayerStart.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TheFacility.h"

namespace FacilityQANight
{
	static bool IsHost(UWorld* World, const TCHAR* Command)
	{
		if (World && World->GetNetMode() != NM_Client)
		{
			return true;
		}
		UE_LOG(LogTheFacility, Warning, TEXT("%s: run it on the host"), Command);
		return false;
	}

	static AFacilityCharacter* GetPlayer(UWorld* World, int32 Index)
	{
		int32 Current = 0;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It, ++Current)
		{
			if (Current == Index)
			{
				return It->Get() ? Cast<AFacilityCharacter>(It->Get()->GetPawn()) : nullptr;
			}
		}
		return nullptr;
	}

	/** A container by name, or "Item:Fuse" for the first unsearched container hiding that item (host only) */
	static AFacilityContainer* FindContainer(UWorld* World, const FString& Name)
	{
		const bool bByItem = Name.StartsWith(TEXT("Item:"));
		const FName Item(*Name.RightChop(5));
		for (TActorIterator<AFacilityContainer> It(World); It; ++It)
		{
			bool bMatch = bByItem ? (!It->IsSearched() && It->GetContentKeyId() == Item) : It->GetName().Contains(Name);
#if WITH_EDITOR
			bMatch |= !bByItem && It->GetActorLabel().Equals(Name, ESearchCase::IgnoreCase);
#endif
			if (bMatch)
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** The usable actor nearest the pawn whose class name contains ClassPart ("Container", "Pickup"), skipping searched containers */
	static AActor* FindNearestUsable(UWorld* World, const APawn* Pawn, const FString& ClassPart)
	{
		AActor* Best = nullptr;
		float BestDistance = TNumericLimits<float>::Max();
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			const AFacilityContainer* Container = Cast<AFacilityContainer>(*It);
			if (!It->Implements<UFacilityInteractable>() || !It->GetClass()->GetName().Contains(ClassPart) || (Container && Container->IsSearched()))
			{
				continue;
			}
			const float Distance = FVector::Dist(It->GetActorLocation(), Pawn->GetActorLocation());
			if (Distance < BestDistance)
			{
				Best = *It;
				BestDistance = Distance;
			}
		}
		return Best;
	}

	/** Navmesh path length between two floor points, or a negative number if there is none */
	static float PathLength(UWorld* World, const FVector& A, const FVector& B)
	{
		const UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, FVector(A.X, A.Y, 100.0f), FVector(B.X, B.Y, 100.0f));
		return Path && Path->IsValid() && !Path->IsPartial() ? Path->GetPathLength() : -1.0f;
	}

	/** The places a night is played between: where the crew starts, every container's front, the fuse box, the gate and past it */
	struct FPoints
	{
		FVector Start = FVector::ZeroVector;
		TArray<FVector> Containers;
		FVector FuseBox = FVector::ZeroVector;
		FVector Gate = FVector::ZeroVector;
		FVector Out = FVector::ZeroVector;
		TMap<uint64, float> Cache;

		float Between(UWorld* World, int32 A, int32 B)
		{
			const uint64 Key = (uint64(uint32(FMath::Min(A, B))) << 32) | uint32(FMath::Max(A, B));
			if (const float* Known = Cache.Find(Key))
			{
				return *Known;
			}
			return Cache.Add(Key, PathLength(World, Get(A), Get(B)));
		}

		/** -1 start, -2 fuse box, -3 gate, -4 out, 0.. containers */
		FVector Get(int32 Index) const
		{
			switch (Index)
			{
			case -1: return Start;
			case -2: return FuseBox;
			case -3: return Gate;
			case -4: return Out;
			default: return Containers[Index];
			}
		}
	};

	static bool GatherPoints(UWorld* World, const FacilityNight::FSite& Site, FPoints& Points)
	{
		int32 NumStarts = 0;
		for (TActorIterator<APlayerStart> It(World); It; ++It)
		{
			Points.Start += It->GetActorLocation();
			++NumStarts;
		}
		TActorIterator<AFacilityFuseBox> Box(World);
		TActorIterator<AFacilityExit> Exit(World);
		if (NumStarts == 0 || !Box || !Exit)
		{
			return false;
		}
		Points.Start /= NumStarts;
		Points.FuseBox = Box->GetActorLocation() + Box->GetActorForwardVector() * 90.0f;
		// the gate is crossed along its X: the lot is behind it, the road (and the exit zone) in front
		Points.Gate = Exit->GetActorLocation() - Exit->GetActorForwardVector() * 120.0f;
		Points.Out = Exit->GetActorLocation() + Exit->GetActorForwardVector() * 250.0f;
		for (const AFacilityContainer* Container : Site.Containers)
		{
			Points.Containers.Add(Container->GetFrontPoint());
		}
		return true;
	}

	struct FRoute
	{
		float Length = 0.0f;
		int32 Searches = 0;
		bool bOk = true;
	};

	/**
	 *  A crew that doesn't know where anything is: from the start it searches the nearest container it can open,
	 *  again and again, until it holds everything the night needs (a locked room's containers only once it has the
	 *  master key), then takes the fuses to the box, the key to the gate, and walks out.
	 */
	static FRoute BlindRoute(UWorld* World, const FacilityNight::FSite& Site, const FacilityNight::FPlan& Plan, FPoints& Points)
	{
		FRoute Route;
		TSet<int32> Needed;
		for (const TPair<int32, FName>& Entry : Plan.Contents)
		{
			if (Entry.Value == FacilityNight::FuseId || Entry.Value == FacilityNight::GateKeyId)
			{
				Needed.Add(Entry.Key);
			}
		}
		const int32 MasterAt = Plan.FindItem(FacilityNight::MasterKeyId);
		const bool bLockedNeeded = Needed.Array().ContainsByPredicate([&](int32 Index) { return Plan.IsInLockedRoom(Site, Index); });
		if (bLockedNeeded && MasterAt != INDEX_NONE)
		{
			Needed.Add(MasterAt);
		}

		bool bHasMaster = false;
		TSet<int32> Searched;
		int32 Here = -1;
		while (!Needed.IsEmpty())
		{
			int32 Best = INDEX_NONE;
			float BestLength = TNumericLimits<float>::Max();
			for (int32 Index = 0; Index < Site.Containers.Num(); ++Index)
			{
				if (Searched.Contains(Index) || (Plan.IsInLockedRoom(Site, Index) && !bHasMaster))
				{
					continue;
				}
				const float Length = Points.Between(World, Here, Index);
				if (Length >= 0.0f && Length < BestLength)
				{
					Best = Index;
					BestLength = Length;
				}
			}
			if (Best == INDEX_NONE)
			{
				Route.bOk = false;
				return Route;
			}
			Route.Length += BestLength;
			++Route.Searches;
			Searched.Add(Best);
			Needed.Remove(Best);
			bHasMaster |= Best == MasterAt;
			Here = Best;
		}

		const float ToBox = Points.Between(World, Here, -2);
		const float ToGate = Points.Between(World, -2, -3);
		const float ToOut = Points.Between(World, -3, -4);
		Route.bOk = ToBox >= 0.0f && ToGate >= 0.0f && ToOut >= 0.0f;
		Route.Length += FMath::Max(ToBox, 0.0f) + FMath::Max(ToGate, 0.0f) + FMath::Max(ToOut, 0.0f);
		return Route;
	}

	/** A crew that knows where everything is: the shortest greedy tour through what it needs, then box, gate, out */
	static FRoute KnownRoute(UWorld* World, const FacilityNight::FSite& Site, const FacilityNight::FPlan& Plan, FPoints& Points)
	{
		FRoute Route;
		TArray<int32> Todo;
		for (const TPair<int32, FName>& Entry : Plan.Contents)
		{
			if (Entry.Value == FacilityNight::FuseId || Entry.Value == FacilityNight::GateKeyId)
			{
				Todo.Add(Entry.Key);
			}
		}
		const int32 MasterAt = Plan.FindItem(FacilityNight::MasterKeyId);
		int32 Here = -1;
		if (Todo.ContainsByPredicate([&](int32 Index) { return Plan.IsInLockedRoom(Site, Index); }) && MasterAt != INDEX_NONE)
		{
			Route.Length += FMath::Max(Points.Between(World, Here, MasterAt), 0.0f);
			++Route.Searches;
			Here = MasterAt;
		}
		while (!Todo.IsEmpty())
		{
			Todo.Sort([&](int32 A, int32 B) { return Points.Between(World, Here, A) < Points.Between(World, Here, B); });
			Route.Length += FMath::Max(Points.Between(World, Here, Todo[0]), 0.0f);
			++Route.Searches;
			Here = Todo[0];
			Todo.RemoveAt(0);
		}
		Route.Length += FMath::Max(Points.Between(World, Here, -2), 0.0f) + FMath::Max(Points.Between(World, -2, -3), 0.0f) + FMath::Max(Points.Between(World, -3, -4), 0.0f);
		return Route;
	}
}

using namespace FacilityQANight;

static FAutoConsoleCommandWithWorld CmdQANightPlan(
	TEXT("facility.QA.NightPlan"),
	TEXT("QA, host: logs this night's plan: locked rooms, creature starts, and what each container hides"),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		const AFacilityGameState* GameState = IsHost(World, TEXT("facility.QA.NightPlan")) ? World->GetGameState<AFacilityGameState>() : nullptr;
		if (!GameState)
		{
			return;
		}
		const FacilityNight::FSite Site = FacilityNight::GatherSite(World);
		UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_NIGHT %s"), *FacilityNight::Plan(GameState->GetRunSeed(), Site).Describe(Site));
	}));

static FAutoConsoleCommandWithWorldAndArgs CmdQAValidateNights(
	TEXT("facility.QA.ValidateNights"),
	TEXT("QA, host: plans Count nights from seed First and checks each is completable: the item chain, and navmesh paths to everything. facility.QA.ValidateNights <First> <Count>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!IsHost(World, TEXT("facility.QA.ValidateNights")))
		{
			return;
		}
		const int32 First = Args.IsValidIndex(0) ? FCString::Atoi(*Args[0]) : 1;
		const int32 Count = Args.IsValidIndex(1) ? FCString::Atoi(*Args[1]) : 200;

		const FacilityNight::FSite Site = FacilityNight::GatherSite(World);
		FPoints Points;
		if (!Site.IsNight() || !GatherPoints(World, Site, Points))
		{
			UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_CHECK FAIL ValidateNights expected a night map got no containers, fuse box, gate or player start"));
			return;
		}

		// reachability doesn't depend on the seed: every container front, the box, the gate and out, from the start
		TArray<FString> Unreachable;
		for (int32 Index = 0; Index < Site.Containers.Num(); ++Index)
		{
			if (Points.Between(World, -1, Index) < 0.0f)
			{
				Unreachable.Add(Site.Containers[Index]->GetName());
			}
		}
		for (const TPair<int32, const TCHAR*>& Place : TArray<TPair<int32, const TCHAR*>>{ { -2, TEXT("fuse box") }, { -3, TEXT("gate") }, { -4, TEXT("road") } })
		{
			if (Points.Between(World, -1, Place.Key) < 0.0f)
			{
				Unreachable.Add(Place.Value);
			}
		}
		for (const AActor* Start : Site.TenantStarts)
		{
			if (PathLength(World, Start->GetActorLocation(), Points.Start) < 0.0f)
			{
				Unreachable.Add(Start->GetName());
			}
		}
		for (const AActor* Start : Site.MarkedStarts)
		{
			if (PathLength(World, Start->GetActorLocation(), Points.Start) < 0.0f)
			{
				Unreachable.Add(Start->GetName());
			}
		}
		UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_CHECK %s NightReach expected nothing unreachable got [%s]"),
			Unreachable.IsEmpty() ? TEXT("PASS") : TEXT("FAIL"), *FString::Join(Unreachable, TEXT(", ")));

		int32 Good = 0;
		TMap<FName, int32> KeySpots;
		TMap<FName, int32> LockedCount;
		int32 Masters = 0, Cameras = 0;
		for (int32 Seed = First; Seed < First + Count; ++Seed)
		{
			const FacilityNight::FPlan Plan = FacilityNight::Plan(Seed, Site);
			bool bOk = Plan.Problem.IsEmpty();
			for (const TPair<int32, FName>& Entry : Plan.Contents)
			{
				bOk &= Points.Between(World, -1, Entry.Key) >= 0.0f;
			}
			// the plan has to be the same every time it is made from the seed
			bOk &= FacilityNight::Plan(Seed, Site).Describe(Site) == Plan.Describe(Site);
			if (bOk)
			{
				++Good;
				if (Args.IsValidIndex(2) && Args[2] == TEXT("list"))
				{
					UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_NIGHT ok %s"), *Plan.Describe(Site));
				}
			}
			else
			{
				UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_NIGHT bad %s"), *Plan.Describe(Site));
			}
			KeySpots.FindOrAdd(Plan.GateKeySpot)++;
			for (const FName Room : Plan.LockedRooms)
			{
				LockedCount.FindOrAdd(Room)++;
			}
			Masters += Plan.bMasterKey ? 1 : 0;
			Cameras += Plan.bFlashCamera ? 1 : 0;
		}

		TArray<FString> Spots, Rooms;
		KeySpots.KeySort(FNameLexicalLess());
		LockedCount.KeySort(FNameLexicalLess());
		for (const TPair<FName, int32>& Spot : KeySpots) { Spots.Add(FString::Printf(TEXT("%s %d"), *Spot.Key.ToString(), Spot.Value)); }
		for (const TPair<FName, int32>& Room : LockedCount) { Rooms.Add(FString::Printf(TEXT("%s %d"), *Room.Key.ToString(), Room.Value)); }
		UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_NIGHT variety over %d seeds: gate key [%s], locked [%s], master key %d, flash camera %d"),
			Count, *FString::Join(Spots, TEXT(", ")), *FString::Join(Rooms, TEXT(", ")), Masters, Cameras);
		UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_CHECK %s ValidateNights expected %d got %d completable"), Good == Count ? TEXT("PASS") : TEXT("FAIL"), Count, Good);
	}));

static FAutoConsoleCommandWithWorldAndArgs CmdQANightRoute(
	TEXT("facility.QA.NightRoute"),
	TEXT("QA, host: estimates how long nights take by navmesh route length and searches, for Count seeds from First. facility.QA.NightRoute <First> <Count>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!IsHost(World, TEXT("facility.QA.NightRoute")))
		{
			return;
		}
		const int32 First = Args.IsValidIndex(0) ? FCString::Atoi(*Args[0]) : 1;
		const int32 Count = Args.IsValidIndex(1) ? FCString::Atoi(*Args[1]) : 20;
		const FacilityNight::FSite Site = FacilityNight::GatherSite(World);
		FPoints Points;
		if (!Site.IsNight() || !GatherPoints(World, Site, Points))
		{
			return;
		}

		// walking 320 cm/s, sneaking 150 cm/s, 1.5 s a search
		constexpr float Walk = 320.0f, Sneak = 150.0f, Search = 1.5f;
		float SumBlind = 0.0f, SumKnown = 0.0f, MinBlind = TNumericLimits<float>::Max(), MaxBlind = 0.0f;
		int32 SumSearches = 0;
		for (int32 Seed = First; Seed < First + Count; ++Seed)
		{
			const FacilityNight::FPlan Plan = FacilityNight::Plan(Seed, Site);
			const FRoute Blind = BlindRoute(World, Site, Plan, Points);
			const FRoute Known = KnownRoute(World, Site, Plan, Points);
			UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_NIGHT route seed %d: blind %.0f m, %d searches, %.1f min walking / %.1f min sneaking; knowing where: %.0f m, %.1f min walking%s"),
				Seed, Blind.Length / 100.0f, Blind.Searches, (Blind.Length / Walk + Blind.Searches * Search) / 60.0f, (Blind.Length / Sneak + Blind.Searches * Search) / 60.0f,
				Known.Length / 100.0f, (Known.Length / Walk + Known.Searches * Search) / 60.0f, Blind.bOk ? TEXT("") : TEXT(" (NO ROUTE)"));
			SumBlind += Blind.Length;
			SumKnown += Known.Length;
			SumSearches += Blind.Searches;
			MinBlind = FMath::Min(MinBlind, Blind.Length);
			MaxBlind = FMath::Max(MaxBlind, Blind.Length);
		}
		const float AvgBlind = SumBlind / FMath::Max(Count, 1);
		const float AvgSearches = float(SumSearches) / FMath::Max(Count, 1);
		UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_NIGHT route average over %d seeds: blind %.0f m (%.0f..%.0f), %.1f searches, %.1f min walking, %.1f min sneaking; knowing where %.0f m"),
			Count, AvgBlind / 100.0f, MinBlind / 100.0f, MaxBlind / 100.0f, AvgSearches,
			(AvgBlind / Walk + AvgSearches * Search) / 60.0f, (AvgBlind / Sneak + AvgSearches * Search) / 60.0f, SumKnown / FMath::Max(Count, 1) / 100.0f);
	}));

static FAutoConsoleCommandWithWorldAndArgs CmdQASearch(
	TEXT("facility.QA.Search"),
	TEXT("QA, host: a player walks up to a container and holds a search on it, through the real search. facility.QA.Search <Name|Item:Id> [Player] [now|goto]. goto only puts the player in front of it, for a client to search itself"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!IsHost(World, TEXT("facility.QA.Search")) || Args.IsEmpty())
		{
			return;
		}
		AFacilityCharacter* Player = GetPlayer(World, Args.IsValidIndex(1) ? FCString::Atoi(*Args[1]) : 0);
		AFacilityContainer* Container = FindContainer(World, Args[0]);
		if (!Player || !Container)
		{
			UE_LOG(LogTheFacility, Warning, TEXT("facility.QA.Search: no player or no container %s"), *Args[0]);
			return;
		}

		// stand in front of it, facing it
		const FVector Front = Container->GetFrontPoint();
		Player->TeleportTo(FVector(Front.X, Front.Y, Player->GetActorLocation().Z), (-Container->GetActorForwardVector()).Rotation());
		if (Args.IsValidIndex(2) && Args[2] == TEXT("goto"))
		{
			UE_LOG(LogTheFacility, Display, TEXT("facility.QA.Search: %s stands at %s"), *Player->GetName(), *Container->GetName());
			return;
		}
		if (Args.IsValidIndex(2) && Args[2] == TEXT("now"))
		{
			Container->CompleteSearchNow(Player);
		}
		else
		{
			IFacilityInteractable::Execute_Interact(Container, Player);
		}
		UE_LOG(LogTheFacility, Display, TEXT("facility.QA.Search: %s searches %s"), *Player->GetName(), *Container->GetName());
	}));

static FAutoConsoleCommandWithWorldAndArgs CmdQAUse(
	TEXT("facility.QA.Use"),
	TEXT("QA, any machine: the local player asks the server to use an actor, through the real interaction RPC (range and sight are checked). facility.QA.Use <ActorName|nearest:ClassPart|release>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		const APlayerController* PC = World && !Args.IsEmpty() ? World->GetFirstPlayerController() : nullptr;
		AFacilityCharacter* Character = PC ? Cast<AFacilityCharacter>(PC->GetPawn()) : nullptr;
		if (!Character || !Character->GetInteractionComponent())
		{
			return;
		}
		if (Args[0] == TEXT("release"))
		{
			Character->GetInteractionComponent()->StopInteract();
			return;
		}
		if (Args[0].StartsWith(TEXT("nearest:")))
		{
			// names in a packaged build carry no editor labels: the nearest container or pickup is how a client script picks one
			if (AActor* Nearest = FindNearestUsable(World, Character, Args[0].RightChop(8)))
			{
				UE_LOG(LogTheFacility, Display, TEXT("facility.QA.Use: %s asks to use %s"), *Character->GetName(), *Nearest->GetName());
				Character->GetInteractionComponent()->InteractWith(Nearest);
			}
			else
			{
				UE_LOG(LogTheFacility, Warning, TEXT("facility.QA.Use: nothing usable of %s nearby"), *Args[0]);
			}
			return;
		}
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetName().Contains(Args[0]) && It->Implements<UFacilityInteractable>())
			{
				UE_LOG(LogTheFacility, Display, TEXT("facility.QA.Use: %s asks to use %s"), *Character->GetName(), *It->GetName());
				Character->GetInteractionComponent()->InteractWith(*It);
				return;
			}
		}
		UE_LOG(LogTheFacility, Warning, TEXT("facility.QA.Use: nothing usable called %s"), *Args[0]);
	}));

static FAutoConsoleCommandWithWorldAndArgs CmdQALockedRooms(
	TEXT("facility.QA.TryLockedRooms"),
	TEXT("QA, host: a player tries every door the night locked, through the door's own server path. With the master key each must open, without it each must hold. facility.QA.TryLockedRooms [Player]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		AFacilityCharacter* Player = IsHost(World, TEXT("facility.QA.TryLockedRooms")) ? GetPlayer(World, Args.IsValidIndex(0) ? FCString::Atoi(*Args[0]) : 0) : nullptr;
		if (!Player)
		{
			return;
		}
		TArray<AFacilityDoor*> Doors;
		for (TActorIterator<AFacilityDoor> It(World); It; ++It)
		{
			if (It->IsLocked() && !It->IsHeldShut() && !It->IsBroken() && !It->GetLockGroup().IsNone())
			{
				Doors.Add(*It);
			}
		}
		Doors.Sort([](const AFacilityDoor& A, const AFacilityDoor& B) { return A.GetName() < B.GetName(); });
		const bool bKey = Player->HasKey(AFacilityDoor::MasterKeyId);
		for (AFacilityDoor* Door : Doors)
		{
			if (IFacilityInteractable::Execute_CanInteract(Door, Player))
			{
				IFacilityInteractable::Execute_Interact(Door, Player);
			}
			const bool bOpened = !Door->IsLocked();
			UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_CHECK %s LockedRoom %s (%s) expected %s got %s"), bOpened == bKey ? TEXT("PASS") : TEXT("FAIL"),
				*Door->GetLockGroup().ToString(), *Door->GetName(), bKey ? TEXT("opens") : TEXT("holds"), bOpened ? TEXT("opens") : TEXT("holds"));
		}
		if (Doors.IsEmpty())
		{
			UE_LOG(LogTheFacility, Display, TEXT("facility.QA.TryLockedRooms: no locked rooms left"));
		}
	}));

static FAutoConsoleCommandWithWorld CmdQANoiseInLockedRoom(
	TEXT("facility.QA.NoiseInLockedRoom"),
	TEXT("QA, host: a loud sound inside the first room that is still locked (at its first container), so the Tenant has to break in to reach it"),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		if (!IsHost(World, TEXT("facility.QA.NoiseInLockedRoom")))
		{
			return;
		}
		TSet<FName> Locked;
		for (TActorIterator<AFacilityDoor> It(World); It; ++It)
		{
			if (It->IsLocked() && !It->IsHeldShut() && !It->IsBroken())
			{
				Locked.Add(It->GetLockGroup());
			}
		}
		const FacilityNight::FSite Site = FacilityNight::GatherSite(World);
		for (const AFacilityContainer* Container : Site.Containers)
		{
			if (Locked.Contains(Container->GetArea()))
			{
				const FVector Where = Container->GetFrontPoint() + FVector(0.0f, 0.0f, 100.0f);
				AFacilityTenant::ReportNoise(World, Where, 3000.0f, nullptr);
				UE_LOG(LogTheFacility, Display, TEXT("facility.QA.NoiseInLockedRoom: noise in %s at %s"), *Container->GetArea().ToString(), *Where.ToCompactString());
				return;
			}
		}
		UE_LOG(LogTheFacility, Warning, TEXT("facility.QA.NoiseInLockedRoom: no locked room with a container"));
	}));

static FAutoConsoleCommandWithWorld CmdQAResetHints(
	TEXT("facility.QA.ResetHints"),
	TEXT("QA: forgets which first-night hints this machine has shown"),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		if (UFacilityGameInstance* GameInstance = World ? World->GetGameInstance<UFacilityGameInstance>() : nullptr)
		{
			GameInstance->ResetHints();
		}
	}));

#endif
