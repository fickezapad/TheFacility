// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityMovementComponent.h"
#include "GameFramework/Character.h"

/** A saved move that also remembers whether the player wanted to sprint, so replays and the server agree */
class FSavedMove_Facility : public FSavedMove_Character
{
	typedef FSavedMove_Character Super;

public:

	uint8 bSavedWantsToSprint : 1;

	FSavedMove_Facility()
		: bSavedWantsToSprint(0)
	{
	}

	virtual void Clear() override
	{
		Super::Clear();
		bSavedWantsToSprint = 0;
	}

	virtual uint8 GetCompressedFlags() const override
	{
		uint8 Result = Super::GetCompressedFlags();
		if (bSavedWantsToSprint)
		{
			Result |= FLAG_Custom_0;
		}
		return Result;
	}

	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override
	{
		if (bSavedWantsToSprint != static_cast<const FSavedMove_Facility*>(NewMove.Get())->bSavedWantsToSprint)
		{
			return false;
		}
		return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
	}

	virtual void SetMoveFor(ACharacter* InCharacter, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override
	{
		Super::SetMoveFor(InCharacter, InDeltaTime, NewAccel, ClientData);
		if (const UFacilityMovementComponent* Movement = Cast<UFacilityMovementComponent>(InCharacter->GetCharacterMovement()))
		{
			bSavedWantsToSprint = Movement->bWantsToSprint;
		}
	}

	virtual void PrepMoveFor(ACharacter* InCharacter) override
	{
		Super::PrepMoveFor(InCharacter);
		if (UFacilityMovementComponent* Movement = Cast<UFacilityMovementComponent>(InCharacter->GetCharacterMovement()))
		{
			Movement->bWantsToSprint = bSavedWantsToSprint;
		}
	}
};

class FNetworkPredictionData_Client_Facility : public FNetworkPredictionData_Client_Character
{
public:

	explicit FNetworkPredictionData_Client_Facility(const UCharacterMovementComponent& ClientMovement)
		: FNetworkPredictionData_Client_Character(ClientMovement)
	{
	}

	virtual FSavedMovePtr AllocateNewMove() override
	{
		return FSavedMovePtr(new FSavedMove_Facility());
	}
};

UFacilityMovementComponent::UFacilityMovementComponent()
{
	MaxWalkSpeed = WalkSpeed;
}

void UFacilityMovementComponent::BeginPlay()
{
	Super::BeginPlay();

	Stamina = StaminaSeconds;
}

FNetworkPredictionData_Client* UFacilityMovementComponent::GetPredictionData_Client() const
{
	if (!ClientPredictionData)
	{
		UFacilityMovementComponent* MutableThis = const_cast<UFacilityMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_Facility(*this);
	}

	return ClientPredictionData;
}

void UFacilityMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);

	bWantsToSprint = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
}

float UFacilityMovementComponent::GetMaxSpeed() const
{
	if (MovementMode != MOVE_Walking && MovementMode != MOVE_NavWalking)
	{
		return Super::GetMaxSpeed();
	}

	if (bExhausted)
	{
		return ExhaustedSpeed;
	}

	return (bWantsToSprint && Stamina > 0.0f) ? SprintSpeed : WalkSpeed;
}

bool UFacilityMovementComponent::IsSprinting() const
{
	return bWantsToSprint && !bExhausted && Stamina > 0.0f && IsMovingOnGround() && Velocity.SizeSquared2D() > FMath::Square(WalkSpeed + 10.0f);
}

float UFacilityMovementComponent::GetStaminaFraction() const
{
	return StaminaSeconds > 0.0f ? FMath::Clamp(Stamina / StaminaSeconds, 0.0f, 1.0f) : 1.0f;
}

void UFacilityMovementComponent::OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity)
{
	Super::OnMovementUpdated(DeltaSeconds, OldLocation, OldVelocity);

	// replayed moves after a server correction would drain stamina a second time
	if (CharacterOwner && CharacterOwner->bClientUpdating)
	{
		return;
	}

	if (IsSprinting())
	{
		Stamina = FMath::Max(Stamina - DeltaSeconds, 0.0f);
		if (Stamina <= 0.0f)
		{
			bExhausted = true;
		}
		return;
	}

	Stamina = FMath::Min(Stamina + DeltaSeconds * StaminaSeconds / StaminaRefillSeconds, StaminaSeconds);
	if (bExhausted && Stamina >= StaminaSeconds * RecoverThreshold)
	{
		bExhausted = false;
	}
}
