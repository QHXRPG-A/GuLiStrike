#pragma once

#include "Gameplay/Units/GuLiExternalCharacterMovementComponent.h"
#include "GameFramework/Character.h"

// Shared revision serialization for specialized character movement protocols.
class FGuLiExternalSavedMove : public FSavedMove_Character
{
  public:
	uint32 Revision = 0;
	virtual void Clear() override
	{
		Super::Clear();
		Revision = 0;
	}
	virtual void SetMoveFor(ACharacter *Character, float Delta, const FVector &Accel,
							FNetworkPredictionData_Client_Character &Data) override
	{
		Super::SetMoveFor(Character, Delta, Accel, Data);
		Revision = CastChecked<UGuLiExternalCharacterMovementComponent>(Character->GetCharacterMovement())
					   ->GetDisplacementRevision();
	}
	virtual bool CanCombineWith(const FSavedMovePtr &NewMove, ACharacter *Character, float MaxDelta) const override
	{
		return Revision == static_cast<const FGuLiExternalSavedMove &>(*NewMove).Revision &&
			   Super::CanCombineWith(NewMove, Character, MaxDelta);
	}

  private:
	using Super = FSavedMove_Character;
};
struct FGuLiExternalMoveData : FCharacterNetworkMoveData
{
	uint32 Revision = 0;
	virtual void ClientFillNetworkMoveData(const FSavedMove_Character &Move, ENetworkMoveType Type) override
	{
		FCharacterNetworkMoveData::ClientFillNetworkMoveData(Move, Type);
		Revision = static_cast<const FGuLiExternalSavedMove &>(Move).Revision;
	}
	virtual bool Serialize(UCharacterMovementComponent &Movement, FArchive &Ar, UPackageMap *Map,
						   ENetworkMoveType Type) override
	{
		const bool bSuccess = FCharacterNetworkMoveData::Serialize(Movement, Ar, Map, Type);
		Ar << Revision;
		return bSuccess && !Ar.IsError();
	}
};
struct FGuLiExternalMoveResponse : FCharacterMoveResponseDataContainer
{
	uint32 Revision = 0;
	virtual void ServerFillResponseData(const UCharacterMovementComponent &Movement,
										const FClientAdjustment &Adjustment) override
	{
		FCharacterMoveResponseDataContainer::ServerFillResponseData(Movement, Adjustment);
		Revision = static_cast<const UGuLiExternalCharacterMovementComponent &>(Movement).GetDisplacementRevision();
	}
	virtual bool Serialize(UCharacterMovementComponent &Movement, FArchive &Ar, UPackageMap *Map) override
	{
		const bool bSuccess = FCharacterMoveResponseDataContainer::Serialize(Movement, Ar, Map);
		Ar << Revision;
		return bSuccess && !Ar.IsError();
	}
};
