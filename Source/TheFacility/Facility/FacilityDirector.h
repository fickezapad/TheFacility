// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FacilityDirector.generated.h"

class AFacilityCharacter;
class ULightComponent;
struct FFacilitySignature;

/**
 *  The Facility itself. It pays attention to the crew and answers.
 *
 *  Attention grows with every Offering signed into the ledger (some weigh more than others), with noise, and with
 *  time. Each threshold it crosses brings an answer, and past the last one it keeps answering:
 *    - the lights around someone die for a while,
 *    - doors near the crew slam and are held shut (the master key or the Tenant still opens them),
 *    - the creatures are sent back early (The Marked returns sooner, a sleeping Tenant wakes),
 *    - the Tenant is told where the most indebted player is: whoever signed the most.
 *  So every run escalates differently, depending on what the crew took and how loud they were.
 *
 *  Server-authoritative: attention, thresholds and answers are decided on the server only. Clients get the tier for the HUD,
 *  the whispered line, and the blackouts (cosmetic, by multicast). Spawned by the game mode on maps with an exit.
 */
UCLASS()
class THEFACILITY_API AFacilityDirector : public AActor
{
	GENERATED_BODY()

protected:

	/** Attention added by a signature, unless SignatureWeights says otherwise */
	UPROPERTY(EditAnywhere, Category="Director|Attention", meta=(ClampMin=0))
	float SignatureAttention = 15.0f;

	/** Attention per signature id. The fuse is needed, so it costs little; optional Offerings cost more */
	UPROPERTY(EditAnywhere, Category="Director|Attention")
	TMap<FName, float> SignatureWeights = { { FName(TEXT("Fuse")), 8.0f }, { FName(TEXT("MasterKey")), 22.0f }, { FName(TEXT("FlashCamera")), 22.0f } };

	/** Attention a sound adds per 1000 cm it carries. Sprinting steps carry 1500 cm, the fuse box 3000 cm */
	UPROPERTY(EditAnywhere, Category="Director|Attention", meta=(ClampMin=0))
	float NoiseAttentionPer1000cm = 0.5f;

	/** Noise can't add more than this per second, however loud the crew is */
	UPROPERTY(EditAnywhere, Category="Director|Attention", meta=(ClampMin=0))
	float NoiseAttentionPerSecondCap = 1.5f;

	/** Attention voice adds per 1000 cm it carries: less than other noise, it is the crew talking to each other */
	UPROPERTY(EditAnywhere, Category="Director|Attention", meta=(ClampMin=0))
	float VoiceAttentionPer1000cm = 0.15f;

	/** Voice can't add more than this per second, on its own budget */
	UPROPERTY(EditAnywhere, Category="Director|Attention", meta=(ClampMin=0))
	float VoiceAttentionPerSecondCap = 0.3f;

	/** Attention that simply builds up while the night goes on */
	UPROPERTY(EditAnywhere, Category="Director|Attention", meta=(ClampMin=0))
	float AttentionPerMinute = 6.0f;

	/** Each threshold crossed brings an answer. The last one starts the recurring answers */
	UPROPERTY(EditAnywhere, Category="Director|Answers")
	TArray<float> Thresholds = { 25.0f, 50.0f, 75.0f, 100.0f };

	/** Past the last threshold, the Facility answers again this often */
	UPROPERTY(EditAnywhere, Category="Director|Answers", meta=(ClampMin=5, Units="s"))
	float RecurringAnswerInterval = 40.0f;

	UPROPERTY(EditAnywhere, Category="Director|Answers", meta=(ClampMin=0, Units="cm"))
	float BlackoutRadius = 900.0f;

	UPROPERTY(EditAnywhere, Category="Director|Answers", meta=(ClampMin=0, Units="s"))
	float BlackoutTime = 20.0f;

	UPROPERTY(EditAnywhere, Category="Director|Answers", meta=(ClampMin=0, Units="s"))
	float DoorHoldTime = 25.0f;

	/** How far the Tenant hears it when the Facility points at someone */
	UPROPERTY(EditAnywhere, Category="Director|Answers", meta=(ClampMin=0, Units="cm"))
	float PointNoiseRadius = 5000.0f;

	/** Heard by everyone, everywhere, when the Facility answers */
	UPROPERTY(EditAnywhere, Category="Director|Sound")
	TObjectPtr<class USoundBase> WhisperSound;

	/** Where the lights die */
	UPROPERTY(EditAnywhere, Category="Director|Sound")
	TObjectPtr<class USoundBase> BlackoutSound;

	/** Server: the real value. Clients see it rounded, for the debug panel */
	float Attention = 0.0f;

	UPROPERTY(Replicated)
	int32 ReplicatedAttention = 0;

	/** Thresholds crossed so far */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Director")
	int32 Tier = 0;

	/** Server: noise attention left for this second */
	float NoiseBudget = 0.0f;

	/** Server: voice attention left for this second */
	float VoiceBudget = 0.0f;

	/** Server: time until the next recurring answer, once past the last threshold */
	float RecurringIn = 0.0f;

	/** Server: answers given so far, which picks the next recurring one */
	int32 RecurringCount = 0;

	bool bShutDown = false;

	FTimerHandle ThinkTimer;

	static constexpr float ThinkInterval = 0.5f;

	/** Local: the last line the Facility whispered, and when */
	FString WhisperText;
	double WhisperTime = -100.0;

	/** Local: lights this machine darkened, and when each comes back */
	struct FDarkenedLight
	{
		TWeakObjectPtr<ULightComponent> Light;
		double Until = 0.0;
	};
	TArray<FDarkenedLight> Darkened;

public:

	AFacilityDirector();

	/** The run's director, if the map has one */
	static AFacilityDirector* Get(const UWorld* World);

	/** Server only. A sound was made; louder sounds draw more attention, up to a cap per second */
	void HearNoise(float Radius);

	/** Server only. Someone is talking; counted on a smaller budget than other noise */
	void HearVoice(float Radius);

	/** Server only. QA and scripted events */
	void AddAttention(float Amount, const FString& Why);

	/** Server only. The run is over: no more answers, and darkened lights come back */
	void ShutDown();

	int32 GetTier() const { return Tier; }

	/** Rounded on clients */
	int32 GetAttention() const { return ReplicatedAttention; }

	/** Local: the last whisper and how long ago it came, for the HUD */
	const FString& GetWhisper(float& OutAge) const;

	/** Local: lights darkened right now on this machine, for QA */
	int32 GetNumDarkened() const { return Darkened.Num(); }

protected:

	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual void Tick(float DeltaSeconds) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void BindToLedger();

	UFUNCTION()
	void HandleSignatureRecorded(const FFacilitySignature& Signature);

	/** Server: time passes, thresholds are checked, recurring answers come due */
	void Think();

	/** Server: answers every threshold attention has crossed */
	void CheckThresholds();

	/** Server: the answer for crossing threshold Index, or a recurring one past the last */
	void Answer(int32 Index);

	/** Server: the standing player who signed the most, the most recent signer breaking ties. Null if nobody stands */
	AFacilityCharacter* FindMostIndebted() const;

	/** Server: a standing player, the most indebted first */
	AFacilityCharacter* PickTarget() const;

	void AnswerBlackout(AFacilityCharacter* Target, float RadiusScale);

	void AnswerHoldDoors(int32 Count);

	void AnswerSendThemBack();

	void AnswerPointAtDebtor();

	void Whisper(const FString& Text);

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_Whisper(const FString& Text);

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_Blackout(FVector_NetQuantize Center, float Radius, float Duration);

	/** Local: brings back every darkened light whose time is up, or all of them */
	void RestoreLights(bool bAll);
};
