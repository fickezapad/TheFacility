// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityGameState.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "TheFacility.h"

void AFacilityGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFacilityGameState, Ledger);
}

void AFacilityGameState::RecordSignature(FName SignatureId, FName ZoneId, APlayerState* Signer)
{
	if (!HasAuthority())
	{
		return;
	}

	FFacilitySignature& Entry = Ledger.AddDefaulted_GetRef();
	Entry.SignatureId = SignatureId;
	Entry.ZoneId = ZoneId;
	Entry.Signer = Signer;
	Entry.SignerName = MakeSignerName(Signer);
	Entry.ServerTime = GetServerWorldTimeSeconds();

	UE_LOG(LogTheFacility, Log, TEXT("Facility ledger: %s signed %s in zone %s"), *Entry.SignerName, *SignatureId.ToString(), *ZoneId.ToString());

	// OnRep doesn't run on the server, so broadcast here too
	BroadcastNewEntries();
}

FString AFacilityGameState::MakeSignerName(const APlayerState* Signer) const
{
	if (!Signer)
	{
		return TEXT("<nobody>");
	}

	const FString Name = Signer->GetPlayerName();
	const bool bNameShared = PlayerArray.ContainsByPredicate([Signer, &Name](const APlayerState* Other)
	{
		return Other && Other != Signer && Other->GetPlayerName() == Name;
	});

	return bNameShared ? FString::Printf(TEXT("%s #%d"), *Name, Signer->GetPlayerId()) : Name;
}

bool AFacilityGameState::HasSignatureInZone(FName ZoneId) const
{
	return Ledger.ContainsByPredicate([ZoneId](const FFacilitySignature& Entry) { return Entry.ZoneId == ZoneId; });
}

void AFacilityGameState::OnRep_Ledger()
{
	BroadcastNewEntries();
}

void AFacilityGameState::BroadcastNewEntries()
{
	// the ledger only grows, so anything past NumBroadcast is new on this machine
	while (NumBroadcast < Ledger.Num())
	{
		OnSignatureRecorded.Broadcast(Ledger[NumBroadcast]);
		++NumBroadcast;
	}
}
