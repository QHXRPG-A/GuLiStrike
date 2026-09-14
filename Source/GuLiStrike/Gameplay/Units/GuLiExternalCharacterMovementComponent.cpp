#include "Gameplay/Units/GuLiExternalCharacterMovementComponent.h"
#include "GameFramework/Character.h"

namespace
{
	class FExternalSavedMove final : public FSavedMove_Character
	{
	public:
		uint32 Revision = 0;
		virtual void Clear() override { Super::Clear(); Revision = 0; }
		virtual void SetMoveFor(ACharacter* Character, float Delta, const FVector& Accel, FNetworkPredictionData_Client_Character& Data) override
		{
			Super::SetMoveFor(Character,Delta,Accel,Data);
			Revision = CastChecked<UGuLiExternalCharacterMovementComponent>(Character->GetCharacterMovement())->GetDisplacementRevision();
		}
		virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override
		{
			return Revision == static_cast<const FExternalSavedMove&>(*NewMove).Revision && Super::CanCombineWith(NewMove,Character,MaxDelta);
		}
	private:
		using Super = FSavedMove_Character;
	};
	class FExternalPredictionData final : public FNetworkPredictionData_Client_Character
	{
	public:
		explicit FExternalPredictionData(const UCharacterMovementComponent& Movement) : FNetworkPredictionData_Client_Character(Movement) {}
		virtual FSavedMovePtr AllocateNewMove() override { return FSavedMovePtr(new FExternalSavedMove); }
	};
	struct FExternalMoveData final : FCharacterNetworkMoveData
	{
		uint32 Revision = 0;
		virtual void ClientFillNetworkMoveData(const FSavedMove_Character& Move, ENetworkMoveType Type) override
		{
			FCharacterNetworkMoveData::ClientFillNetworkMoveData(Move,Type);
			Revision = static_cast<const FExternalSavedMove&>(Move).Revision;
		}
		virtual bool Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* Map, ENetworkMoveType Type) override
		{
			const bool bSuccess = FCharacterNetworkMoveData::Serialize(Movement,Ar,Map,Type);
			Ar << Revision; return bSuccess && !Ar.IsError();
		}
	};
	struct FExternalMoveContainer final : FCharacterNetworkMoveDataContainer
	{
		FExternalMoveData Moves[3];
		FExternalMoveContainer() { NewMoveData=&Moves[0]; PendingMoveData=&Moves[1]; OldMoveData=&Moves[2]; }
	};
	struct FExternalResponse final : FCharacterMoveResponseDataContainer
	{
		uint32 Revision = 0;
		virtual void ServerFillResponseData(const UCharacterMovementComponent& Movement, const FClientAdjustment& Adjustment) override
		{
			FCharacterMoveResponseDataContainer::ServerFillResponseData(Movement,Adjustment);
			Revision = static_cast<const UGuLiExternalCharacterMovementComponent&>(Movement).GetDisplacementRevision();
		}
		virtual bool Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* Map) override
		{
			const bool bSuccess = FCharacterMoveResponseDataContainer::Serialize(Movement,Ar,Map);
			Ar << Revision; return bSuccess && !Ar.IsError();
		}
	};
}
struct FGuLiExternalMovementNetworkStorage { FExternalMoveContainer Moves; FExternalResponse Response; };
void FGuLiExternalMovementStorageDeleter::operator()(FGuLiExternalMovementNetworkStorage* Storage) const { delete Storage; }
UGuLiExternalCharacterMovementComponent::UGuLiExternalCharacterMovementComponent(const FObjectInitializer& Initializer) : Super(Initializer)
{
	NetworkStorage.Reset(new FGuLiExternalMovementNetworkStorage);
	SetNetworkMoveDataContainer(NetworkStorage->Moves); SetMoveResponseDataContainer(NetworkStorage->Response);
	bServerAcceptClientAuthoritativePosition = false;
}
UGuLiExternalCharacterMovementComponent::~UGuLiExternalCharacterMovementComponent() = default;
uint32 UGuLiExternalCharacterMovementComponent::GetDisplacementRevision() const
{
	const auto* Control = GetOwner() ? GetOwner()->FindComponentByClass<UGuLiExternalUnitControlComponent>() : nullptr;
	return Control ? Control->GetState().DisplacementRevision : 0;
}
FNetworkPredictionData_Client* UGuLiExternalCharacterMovementComponent::GetPredictionData_Client() const
{
	if (!ClientPredictionData) { const_cast<UGuLiExternalCharacterMovementComponent*>(this)->ClientPredictionData = new FExternalPredictionData(*this); }
	return ClientPredictionData;
}
void UGuLiExternalCharacterMovementComponent::ApplyExternalDisplacement(const FTransform& Transform)
{
	if (!CharacterOwner || !CharacterOwner->HasAuthority()) { return; }
	CharacterOwner->SetBase(nullptr); CharacterOwner->SetActorTransform(Transform,false,nullptr,ETeleportType::TeleportPhysics);
	StopMovementImmediately(); bJustTeleported = true;
	// A queued response belongs to the previous position even if it has not been serialized yet.
	if (ServerPredictionData) { ServerPredictionData->PendingAdjustment = FClientAdjustment(); ServerPredictionData->bForceClientUpdate = true; }
}
void UGuLiExternalCharacterMovementComponent::ServerMove_PerformMovement(const FCharacterNetworkMoveData& Move)
{
	if (static_cast<const FExternalMoveData&>(Move).Revision != GetDisplacementRevision()
		|| UGuLiExternalUnitControlComponent::AreActorActionsLocked(GetOwner())) { return; }
	Super::ServerMove_PerformMovement(Move);
}
void UGuLiExternalCharacterMovementComponent::ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& Response)
{
	if (static_cast<const FExternalResponse&>(Response).Revision != GetDisplacementRevision()) { return; }
	Super::ClientHandleMoveResponse(Response);
}
