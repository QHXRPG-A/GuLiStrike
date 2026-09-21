#pragma once

#include "Gameplay/Units/GuLiExternalCharacterMovementNetwork.h"
#include "Gameplay/GroundMech/GuLiGroundMechMovementComponent.h"

class FGuLiGroundMechSavedMove final : public FGuLiExternalSavedMove
{
  public:
	FGuLiGroundMassMoveContext Context;
	FGuLiMassSupportState StartSupport, EndSupport;
	bool bContact = false;
	FGuLiRocketMoveState RocketStart, RocketEnd;
	bool bRocketHeld = false, bRocketRequested = false;
	virtual uint8 GetCompressedFlags() const override;
	virtual void Clear() override;
	virtual void SetMoveFor(ACharacter *Character, float Delta, const FVector &Accel,
							FNetworkPredictionData_Client_Character &Data) override;
	virtual void PostUpdate(ACharacter *Character, EPostUpdateMode Mode) override;
	virtual void PrepMoveFor(ACharacter *Character) override;
	virtual bool CanCombineWith(const FSavedMovePtr &Move, ACharacter *Character, float MaxDelta) const override;
	virtual void CombineWith(const FSavedMove_Character *OldMove, ACharacter *Character, APlayerController *PC,
							 const FVector &OldStart) override;
};

struct FGuLiGroundMechMoveData final : FGuLiExternalMoveData
{
	uint32 SupportEpoch = 0u, SupportId = 0u, SupportDisplacement = 0u;
	FGuLiRocketMoveState RocketEnd;
	virtual void ClientFillNetworkMoveData(const FSavedMove_Character &Move, ENetworkMoveType Type) override;
	virtual bool Serialize(UCharacterMovementComponent &Movement, FArchive &Ar, UPackageMap *Map,
						   ENetworkMoveType Type) override;
};
struct FGuLiGroundMechMoveResponse final : FGuLiExternalMoveResponse
{
	FGuLiMassSupportState Support;
	FGuLiRocketMoveState Rocket;
	virtual void ServerFillResponseData(const UCharacterMovementComponent &Movement,
										const FClientAdjustment &Adjustment) override;
	virtual bool Serialize(UCharacterMovementComponent &Movement, FArchive &Ar, UPackageMap *Map) override;
};
struct FGuLiGroundMechMoveContainer final : FCharacterNetworkMoveDataContainer
{
	FGuLiGroundMechMoveData Moves[3];
	FGuLiGroundMechMoveContainer()
	{
		NewMoveData = &Moves[0];
		PendingMoveData = &Moves[1];
		OldMoveData = &Moves[2];
	}
};
struct FGuLiGroundMechNetworkStorage
{
	FGuLiGroundMechMoveContainer Moves;
	FGuLiGroundMechMoveResponse Response;
};
class FGuLiGroundMechPredictionData final : public FNetworkPredictionData_Client_Character
{
  public:
	explicit FGuLiGroundMechPredictionData(const UCharacterMovementComponent &Movement)
		: FNetworkPredictionData_Client_Character(Movement)
	{
	}
	virtual FSavedMovePtr AllocateNewMove() override { return FSavedMovePtr(new FGuLiGroundMechSavedMove); }
	virtual void FreeMove(const FSavedMovePtr &Move) override
	{
		if (Move)
			static_cast<FGuLiGroundMechSavedMove &>(*Move).Context = {};
		FNetworkPredictionData_Client_Character::FreeMove(Move);
	}
};
