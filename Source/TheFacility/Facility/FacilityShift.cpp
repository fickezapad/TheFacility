// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityShift.h"

int32 FacilityShift::NightSeed(int32 ShiftSeed, int32 Night)
{
	if (Night <= 1)
	{
		return ShiftSeed;
	}
	// a fixed mix, never 0 (0 means "pick one")
	const uint32 Mixed = HashCombine(GetTypeHash(ShiftSeed), GetTypeHash(Night * 7919));
	return static_cast<int32>(Mixed % 999983u) + 1;
}

const TArray<FName>& FacilityShift::AllOfferings()
{
	static const TArray<FName> Ids = { TEXT("MasterKey"), TEXT("FlashCamera"), TEXT("MusicBox"), TEXT("Lighter"), TEXT("Photo"), TEXT("CashBox") };
	return Ids;
}

const TArray<FName>& FacilityShift::Treasures()
{
	static const TArray<FName> Ids = { TEXT("MusicBox"), TEXT("Lighter"), TEXT("Photo"), TEXT("CashBox") };
	return Ids;
}

bool FacilityShift::IsOffering(FName ItemId)
{
	return AllOfferings().Contains(ItemId);
}

bool FacilityShift::IsTreasure(FName ItemId)
{
	return Treasures().Contains(ItemId);
}

int32 FacilityShift::ValueOf(FName ItemId)
{
	static const TMap<FName, int32> Values = {
		{ TEXT("MasterKey"), 150 }, { TEXT("FlashCamera"), 150 },
		{ TEXT("MusicBox"), 300 }, { TEXT("Lighter"), 200 }, { TEXT("Photo"), 250 }, { TEXT("CashBox"), 450 } };
	const int32* Value = Values.Find(ItemId);
	return Value ? *Value : 0;
}

FText FacilityShift::NameOf(FName ItemId)
{
	if (ItemId == TEXT("MusicBox")) { return NSLOCTEXT("Facility", "ItemMusicBox", "Music box"); }
	if (ItemId == TEXT("Lighter")) { return NSLOCTEXT("Facility", "ItemLighter", "Silver lighter"); }
	if (ItemId == TEXT("Photo")) { return NSLOCTEXT("Facility", "ItemPhoto", "Wedding photo"); }
	if (ItemId == TEXT("CashBox")) { return NSLOCTEXT("Facility", "ItemCashBox", "Cash box"); }
	if (ItemId == TEXT("MasterKey")) { return NSLOCTEXT("Facility", "ItemMasterKey", "Master key"); }
	if (ItemId == TEXT("FlashCamera")) { return NSLOCTEXT("Facility", "ItemFlashCamera", "Flash camera"); }
	return FText::FromName(ItemId);
}

FString FacilityShift::CreatureName(EFacilityCreature Creature)
{
	switch (Creature)
	{
	case EFacilityCreature::Tenant: return TEXT("The Tenant");
	case EFacilityCreature::Marked: return TEXT("The Marked");
	case EFacilityCreature::Clerk: return TEXT("The Night Clerk");
	case EFacilityCreature::Guest: return TEXT("The Guest in Room 0");
	default: return TEXT("nothing");
	}
}

FString FacilityShift::HintFor(EFacilityCreature Creature, int32 Variant)
{
	// each creature's lines share an image, so a careful reader can tell which one an Offering calls: the Tenant
	// listens, the Marked follows its signer, the Clerk rings and keeps the keys, the Guest waits in the dark
	static const TArray<FString> Tenant = {
		TEXT("Room 7 never checked out. It hears the walls."),
		TEXT("Keep your voice down. The walls are thin and he is not asleep."),
		TEXT("Something heavy listens at the doors. Walk soft.") };
	static const TArray<FString> Marked = {
		TEXT("Whoever signs for this is followed home."),
		TEXT("Sign here. It remembers the hand that took it."),
		TEXT("Taken, it chooses one of you. Only one.") };
	static const TArray<FString> Clerk = {
		TEXT("The bell rings for late arrivals. Do not stand around."),
		TEXT("The clerk keeps every key, and locks what he passes."),
		TEXT("Do not wait in the lamplight. Keep moving.") };
	static const TArray<FString> Guest = {
		TEXT("Room 0 is never empty in the dark."),
		TEXT("Keep a light on it. Never look away together."),
		TEXT("It only comes when nobody is watching.") };
	const TArray<FString>* Lines = Creature == EFacilityCreature::Tenant ? &Tenant : Creature == EFacilityCreature::Marked ? &Marked
		: Creature == EFacilityCreature::Clerk ? &Clerk : Creature == EFacilityCreature::Guest ? &Guest : nullptr;
	return Lines ? (*Lines)[FMath::Abs(Variant) % Lines->Num()] : FString(TEXT("Nothing is written here."));
}

int32 FacilityShift::CreaturesForNight(int32 Night)
{
	return Night <= 1 ? 2 : 3;
}

namespace { bool bQAPassive = false; }

bool FacilityShift::AreCreaturesPassive()
{
#if UE_BUILD_SHIPPING
	return false;
#else
	return bQAPassive;
#endif
}

void FacilityShift::SetCreaturesPassive(bool bPassive)
{
	bQAPassive = bPassive;
}

float FacilityShift::HarshnessForNight(int32 Night)
{
	return 1.0f + 0.4f * FMath::Clamp(Night - 1, 0, NightsPerShift - 1);
}
