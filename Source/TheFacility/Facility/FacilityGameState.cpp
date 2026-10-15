// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityGameState.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "TheFacility.h"

void AFacilityGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFacilityGameState, Ledger);
	DOREPLIFETIME(AFacilityGameState, bPowerRestored);
	DOREPLIFETIME(AFacilityGameState, PowerRestoredBy);
	DOREPLIFETIME(AFacilityGameState, RunState);
	DOREPLIFETIME(AFacilityGameState, RunSeed);
	DOREPLIFETIME(AFacilityGameState, RunStartTime);
	DOREPLIFETIME(AFacilityGameState, RunEndTime);
	DOREPLIFETIME(AFacilityGameState, EscapedNames);
	DOREPLIFETIME(AFacilityGameState, LeftBehindNames);
}

void AFacilityGameState::StartRun(int32 Seed)
{
	if (!HasAuthority())
	{
		return;
	}

	RunSeed = Seed;
	RunStartTime = GetServerWorldTimeSeconds();
	RunState = EFacilityRunState::InProgress;
}

float AFacilityGameState::GetRunTime() const
{
	const double End = RunState == EFacilityRunState::InProgress ? GetServerWorldTimeSeconds() : RunEndTime;
	return static_cast<float>(FMath::Max(End - RunStartTime, 0.0));
}

void AFacilityGameState::RestorePower(APlayerState* By)
{
	if (!HasAuthority() || bPowerRestored)
	{
		return;
	}

	bPowerRestored = true;
	PowerRestoredBy = MakeSignerName(By);
	UE_LOG(LogTheFacility, Log, TEXT("Run: power restored by %s"), *PowerRestoredBy);

	// OnRep doesn't run on the server
	BroadcastPower();
}

void AFacilityGameState::OnRep_PowerRestored()
{
	BroadcastPower();
}

void AFacilityGameState::BroadcastPower()
{
	if (bPowerRestored && !bPowerBroadcast)
	{
		bPowerBroadcast = true;
		OnPowerRestored.Broadcast();
	}
}

void AFacilityGameState::EndRun(EFacilityRunState Outcome, const TArray<FString>& Escaped, const TArray<FString>& LeftBehind)
{
	if (!HasAuthority() || RunState != EFacilityRunState::InProgress || Outcome == EFacilityRunState::InProgress)
	{
		return;
	}

	RunState = Outcome;
	RunEndTime = GetServerWorldTimeSeconds();
	EscapedNames = Escaped;
	LeftBehindNames = LeftBehind;

	UE_LOG(LogTheFacility, Log, TEXT("Run over: %s after %.1f s. Escaped: [%s]. Left behind: [%s]. Seed %d, %d signatures"),
		*StaticEnum<EFacilityRunState>()->GetNameStringByValue((int64)Outcome), GetRunTime(),
		*FString::Join(EscapedNames, TEXT(", ")), *FString::Join(LeftBehindNames, TEXT(", ")), RunSeed, Ledger.Num());
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
