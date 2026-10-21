// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FacilityShift.generated.h"

/** The things that live in the motel. A night wakes two or three of them, each through the Offerings bound to it */
UENUM(BlueprintType)
enum class EFacilityCreature : uint8
{
	Tenant,
	Marked,
	/** The Night Clerk: rings the bell, walks the rooms with a lamp, locks doors near it, takes whoever stands still in its light */
	Clerk,
	/** The Guest in Room 0: only moves while no light is on it */
	Guest,
	None UMETA(Hidden)
};

/** One night of a shift, as it ended: kept by the server across the nights and replicated for the end screens */
USTRUCT(BlueprintType)
struct FFacilityNightRecord
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Facility")
	int32 Night = 0;

	UPROPERTY(BlueprintReadOnly, Category="Facility")
	int32 Seed = 0;

	/** True if anyone got out */
	UPROPERTY(BlueprintReadOnly, Category="Facility")
	bool bEscaped = false;

	UPROPERTY(BlueprintReadOnly, Category="Facility")
	int32 Score = 0;

	UPROPERTY(BlueprintReadOnly, Category="Facility")
	float Seconds = 0.0f;

	/** "Music box: Guest, 300" lines: what was taken, by whom, and whether it counted */
	UPROPERTY(BlueprintReadOnly, Category="Facility")
	TArray<FString> Offerings;

	UPROPERTY(BlueprintReadOnly, Category="Facility")
	TArray<FString> Escaped;

	/** Which creatures were awake this night, for the end screen */
	UPROPERTY(BlueprintReadOnly, Category="Facility")
	TArray<FString> Creatures;
};

/**
 *  A shift: three nights in a row at the motel, each harsher than the last.
 *  The catalog of Offerings (the valuable, optional things a crew can be greedy for) and of the creatures they wake,
 *  with the register's cryptic hints. Pure data and pure functions: every machine reads the same.
 */
namespace FacilityShift
{
	constexpr int32 NightsPerShift = 3;

	/** Points for each player who gets out of a night */
	constexpr int32 EscapeScore = 100;

	/** The night's seed within a shift. Night 1 plays the shift's own seed */
	int32 NightSeed(int32 ShiftSeed, int32 Night);

	/** Every Offering: tools (the master key, the flash camera) and treasures, which are only worth points */
	const TArray<FName>& AllOfferings();
	const TArray<FName>& Treasures();
	bool IsOffering(FName ItemId);
	bool IsTreasure(FName ItemId);

	/** Points it is worth to the crew if whoever signed for it gets out */
	int32 ValueOf(FName ItemId);

	FText NameOf(FName ItemId);

	FString CreatureName(EFacilityCreature Creature);

	/** A line of the register for an Offering bound to Creature: cryptic, but each creature's lines read alike */
	FString HintFor(EFacilityCreature Creature, int32 Variant);

	/** How many creatures a night wakes at most, by night of the shift */
	int32 CreaturesForNight(int32 Night);

	/** How much faster the Facility's attention grows, by night of the shift */
	float HarshnessForNight(int32 Night);

	/** QA (facility.QA.Passive): creatures wake, walk, lock and freeze as ever but take nobody, so a scripted shift tests its flow. Always false in shipping */
	bool AreCreaturesPassive();
	void SetCreaturesPassive(bool bPassive);
}
