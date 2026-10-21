// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityDirector.h"
#include "Facility/FacilityAudio.h"
#include "Facility/FacilityCharacter.h"
#include "Facility/FacilityDoor.h"
#include "Facility/FacilityGameState.h"
#include "Facility/FacilityMarked.h"
#include "Facility/FacilityTenant.h"
#include "Components/LocalLightComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"
#include "UObject/UObjectIterator.h"
#include "TheFacility.h"

AFacilityDirector::AFacilityDirector()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	// ticks only to bring darkened lights back on each machine
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f;
	SetHidden(true);

	static ConstructorHelpers::FObjectFinder<USoundBase> WhisperAsset(TEXT("/Game/Facility/Audio/S_FacilityWhisper.S_FacilityWhisper"));
	static ConstructorHelpers::FObjectFinder<USoundBase> BlackoutAsset(TEXT("/Game/Facility/Audio/S_Blackout.S_Blackout"));
	WhisperSound = WhisperAsset.Object;
	BlackoutSound = BlackoutAsset.Object;
}

void AFacilityDirector::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFacilityDirector, ReplicatedAttention);
	DOREPLIFETIME(AFacilityDirector, Tier);
}

AFacilityDirector* AFacilityDirector::Get(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	TActorIterator<AFacilityDirector> It(const_cast<UWorld*>(World));
	return It ? *It : nullptr;
}

void AFacilityDirector::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		BindToLedger();
		GetWorldTimerManager().SetTimer(ThinkTimer, this, &AFacilityDirector::Think, ThinkInterval, true);
	}
}

void AFacilityDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(ThinkTimer);
	RestoreLights(true);

	Super::EndPlay(EndPlayReason);
}

void AFacilityDirector::BindToLedger()
{
	if (AFacilityGameState* GameState = GetWorld()->GetGameState<AFacilityGameState>())
	{
		GameState->OnSignatureRecorded.AddUniqueDynamic(this, &AFacilityDirector::HandleSignatureRecorded);
	}
	else
	{
		UE_LOG(LogTheFacility, Error, TEXT("%s needs AFacilityGameState. The Facility won't answer."), *GetName());
	}
}

void AFacilityDirector::HandleSignatureRecorded(const FFacilitySignature& Signature)
{
	if (!HasAuthority())
	{
		return;
	}

	const float* Weight = SignatureWeights.Find(Signature.SignatureId);
	AddAttention(Weight ? *Weight : SignatureAttention, FString::Printf(TEXT("%s signed %s"), *Signature.SignerName, *Signature.SignatureId.ToString()));
}

void AFacilityDirector::HearNoise(float Radius)
{
	if (!HasAuthority() || bShutDown || NoiseBudget <= 0.0f)
	{
		return;
	}

	const float Amount = FMath::Min(Radius / 1000.0f * NoiseAttentionPer1000cm, NoiseBudget);
	NoiseBudget -= Amount;
	Attention += Amount * Harshness;
}

void AFacilityDirector::HearVoice(float Radius)
{
	if (!HasAuthority() || bShutDown || VoiceBudget <= 0.0f)
	{
		return;
	}

	const float Amount = FMath::Min(Radius / 1000.0f * VoiceAttentionPer1000cm, VoiceBudget);
	VoiceBudget -= Amount;
	Attention += Amount * Harshness;
}

void AFacilityDirector::AddAttention(float Amount, const FString& Why)
{
	if (!HasAuthority() || bShutDown)
	{
		return;
	}

	Attention += Amount * Harshness;
	UE_LOG(LogTheFacility, Log, TEXT("The Facility: +%.0f attention (%s), now %.0f"), Amount, *Why, Attention);
	// checked at once, so an answer follows the signature that caused it
	CheckThresholds();
}

void AFacilityDirector::Think()
{
	if (!HasAuthority() || bShutDown)
	{
		return;
	}

	const AFacilityGameState* GameState = GetWorld()->GetGameState<AFacilityGameState>();
	if (GameState && !GameState->IsRunInProgress())
	{
		ShutDown();
		return;
	}

	Attention += AttentionPerMinute * Harshness / 60.0f * ThinkInterval;
	NoiseBudget = FMath::Min(NoiseBudget + NoiseAttentionPerSecondCap * ThinkInterval, NoiseAttentionPerSecondCap);
	VoiceBudget = FMath::Min(VoiceBudget + VoiceAttentionPerSecondCap * ThinkInterval, VoiceAttentionPerSecondCap);
	if (Tier >= Thresholds.Num() && Thresholds.Num() > 0)
	{
		RecurringIn -= ThinkInterval;
		if (RecurringIn <= 0.0f)
		{
			RecurringIn = RecurringAnswerInterval;
			Answer(Thresholds.Num() + RecurringCount++);
		}
	}

	CheckThresholds();
}

void AFacilityDirector::CheckThresholds()
{
	while (Tier < Thresholds.Num() && Attention >= Thresholds[Tier])
	{
		UE_LOG(LogTheFacility, Log, TEXT("The Facility: attention %.0f crosses %.0f, tier %d"), Attention, Thresholds[Tier], Tier + 1);
		Answer(Tier);
		++Tier;
		if (Tier == Thresholds.Num())
		{
			RecurringIn = RecurringAnswerInterval;
		}
	}

	ReplicatedAttention = FMath::RoundToInt(Attention);
}

void AFacilityDirector::Answer(int32 Index)
{
	switch (Index)
	{
	case 0:
		Whisper(TEXT("Something in the walls is paying attention."));
		AnswerBlackout(PickTarget(), 1.0f);
		break;
	case 1:
		Whisper(TEXT("Doors close behind you."));
		AnswerHoldDoors(2);
		break;
	case 2:
		Whisper(TEXT("It sends them back early."));
		AnswerSendThemBack();
		AnswerBlackout(PickTarget(), 1.6f);
		break;
	case 3:
		Whisper(TEXT("The Facility wants someone."));
		AnswerPointAtDebtor();
		break;
	default:
	{
		// past the last threshold the answers come round again
		switch ((Index - Thresholds.Num()) % 3)
		{
		case 0:
			Whisper(TEXT("Doors close behind you."));
			AnswerHoldDoors(2);
			break;
		case 1:
			Whisper(TEXT("The lights go first."));
			AnswerBlackout(PickTarget(), 1.3f);
			break;
		default:
			Whisper(TEXT("It knows who owes the most."));
			AnswerPointAtDebtor();
			break;
		}
		break;
	}
	}
}

AFacilityCharacter* AFacilityDirector::FindMostIndebted() const
{
	const AFacilityGameState* GameState = GetWorld()->GetGameState<AFacilityGameState>();
	if (!GameState)
	{
		return nullptr;
	}

	AFacilityCharacter* Best = nullptr;
	int32 BestCount = 0;
	double BestLast = -1.0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		AFacilityCharacter* Character = It->Get() ? Cast<AFacilityCharacter>(It->Get()->GetPawn()) : nullptr;
		const APlayerState* PlayerState = It->Get() ? It->Get()->PlayerState : nullptr;
		if (!Character || !Character->IsStanding() || !PlayerState)
		{
			continue;
		}

		int32 Count = 0;
		double Last = -1.0;
		for (const FFacilitySignature& Entry : GameState->GetLedger())
		{
			if (Entry.Signer == PlayerState)
			{
				++Count;
				Last = Entry.ServerTime;
			}
		}

		if (Count > BestCount || (Count == BestCount && Count > 0 && Last > BestLast))
		{
			Best = Character;
			BestCount = Count;
			BestLast = Last;
		}
	}
	return Best;
}

AFacilityCharacter* AFacilityDirector::PickTarget() const
{
	if (AFacilityCharacter* Indebted = FindMostIndebted())
	{
		return Indebted;
	}

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		AFacilityCharacter* Character = It->Get() ? Cast<AFacilityCharacter>(It->Get()->GetPawn()) : nullptr;
		if (Character && Character->IsStanding())
		{
			return Character;
		}
	}
	return nullptr;
}

void AFacilityDirector::AnswerBlackout(AFacilityCharacter* Target, float RadiusScale)
{
	if (!Target)
	{
		return;
	}

	const float Radius = BlackoutRadius * RadiusScale;
	UE_LOG(LogTheFacility, Log, TEXT("The Facility: blackout %.0f cm around %s for %.0f s"), Radius,
		Target->GetPlayerState() ? *Target->GetPlayerState()->GetPlayerName() : *Target->GetName(), BlackoutTime);
	Multicast_Blackout(Target->GetActorLocation(), Radius, BlackoutTime);
}

void AFacilityDirector::AnswerHoldDoors(int32 Count)
{
	// the doors closest to anyone standing, open or not; broken ones are past holding
	TArray<TPair<float, AFacilityDoor*>> Candidates;
	for (TActorIterator<AFacilityDoor> DoorIt(GetWorld()); DoorIt; ++DoorIt)
	{
		if (DoorIt->IsBroken() || DoorIt->IsLocked())
		{
			continue;
		}

		float Nearest = TNumericLimits<float>::Max();
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			const AFacilityCharacter* Character = It->Get() ? Cast<AFacilityCharacter>(It->Get()->GetPawn()) : nullptr;
			if (Character && Character->IsStanding())
			{
				Nearest = FMath::Min(Nearest, FVector::DistSquared(Character->GetActorLocation(), DoorIt->GetActorLocation()));
			}
		}
		Candidates.Emplace(Nearest, *DoorIt);
	}

	Candidates.Sort([](const TPair<float, AFacilityDoor*>& A, const TPair<float, AFacilityDoor*>& B) { return A.Key < B.Key; });
	for (int32 Index = 0; Index < FMath::Min(Count, Candidates.Num()); ++Index)
	{
		UE_LOG(LogTheFacility, Log, TEXT("The Facility holds %s shut for %.0f s"), *Candidates[Index].Value->GetName(), DoorHoldTime);
		Candidates[Index].Value->HoldShut(DoorHoldTime);
	}
}

void AFacilityDirector::AnswerSendThemBack()
{
	for (TActorIterator<AFacilityMarked> It(GetWorld()); It; ++It)
	{
		It->Hasten(TEXT("the Facility sends it back"));
	}
	for (TActorIterator<AFacilityTenant> It(GetWorld()); It; ++It)
	{
		It->WakeNow(TEXT("the Facility sent it"));
	}
}

void AFacilityDirector::AnswerPointAtDebtor()
{
	AFacilityCharacter* Debtor = PickTarget();
	if (!Debtor)
	{
		return;
	}

	UE_LOG(LogTheFacility, Log, TEXT("The Facility points at %s"), Debtor->GetPlayerState() ? *Debtor->GetPlayerState()->GetPlayerName() : *Debtor->GetName());
	// heard as a sound where they stand, so the Tenant comes to look; a sleeping Tenant is woken first
	for (TActorIterator<AFacilityTenant> It(GetWorld()); It; ++It)
	{
		It->WakeNow(TEXT("the Facility pointed"));
	}
	AFacilityTenant::ReportNoise(GetWorld(), Debtor->GetActorLocation(), PointNoiseRadius, this);
	AnswerBlackout(Debtor, 0.7f);
}

void AFacilityDirector::Whisper(const FString& Text)
{
	UE_LOG(LogTheFacility, Log, TEXT("The Facility whispers: %s"), *Text);
	Multicast_Whisper(Text);
}

void AFacilityDirector::Multicast_Whisper_Implementation(const FString& Text)
{
	WhisperText = Text;
	WhisperTime = GetWorld()->GetTimeSeconds();
	FacilityAudio::Play2D(this, WhisperSound, 0.8f);
}

const FString& AFacilityDirector::GetWhisper(float& OutAge) const
{
	OutAge = static_cast<float>(GetWorld()->GetTimeSeconds() - WhisperTime);
	return WhisperText;
}

void AFacilityDirector::Multicast_Blackout_Implementation(FVector_NetQuantize Center, float Radius, float Duration)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	FacilityAudio::PlayAt(this, BlackoutSound, Center, Radius + 1000.0f);

	// building lights only: anything carried by a player (their flashlight) or a creature keeps working
	const double Until = GetWorld()->GetTimeSeconds() + Duration;
	for (TObjectIterator<ULocalLightComponent> It; It; ++It)
	{
		ULocalLightComponent* Light = *It;
		if (Light->GetWorld() != GetWorld() || !Light->IsVisible() || Cast<APawn>(Light->GetOwner())
			|| FVector::DistSquared(Light->GetComponentLocation(), Center) > FMath::Square(Radius))
		{
			continue;
		}

		Light->SetVisibility(false);
		Darkened.Add({ Light, Until });
	}
	SetActorTickEnabled(true);
}

void AFacilityDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	RestoreLights(false);
	if (Darkened.IsEmpty())
	{
		SetActorTickEnabled(false);
	}
}

void AFacilityDirector::RestoreLights(bool bAll)
{
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	for (int32 Index = Darkened.Num() - 1; Index >= 0; --Index)
	{
		if (bAll || Now >= Darkened[Index].Until)
		{
			if (ULightComponent* Light = Darkened[Index].Light.Get())
			{
				Light->SetVisibility(true);
			}
			Darkened.RemoveAtSwap(Index);
		}
	}
}

void AFacilityDirector::SetHarshness(float Multiplier)
{
	Harshness = FMath::Max(Multiplier, 0.1f);
	RecurringAnswerInterval /= Harshness;
	UE_LOG(LogTheFacility, Log, TEXT("The Facility: harshness x%.2f (answers every %.0f s)"), Harshness, RecurringAnswerInterval);
}

void AFacilityDirector::ShutDown()
{
	if (!HasAuthority() || bShutDown)
	{
		return;
	}

	bShutDown = true;
	GetWorldTimerManager().ClearTimer(ThinkTimer);
	UE_LOG(LogTheFacility, Log, TEXT("The Facility: the run is over at attention %.0f, tier %d"), Attention, Tier);
}
