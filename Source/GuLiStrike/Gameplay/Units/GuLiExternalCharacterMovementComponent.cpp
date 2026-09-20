#include "Gameplay/Units/GuLiExternalCharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "Gameplay/Units/GuLiExternalCharacterMovementNetwork.h"

namespace
{
	class FExternalPredictionData final : public FNetworkPredictionData_Client_Character
	{
	public:
		explicit FExternalPredictionData(const UCharacterMovementComponent& Movement) : FNetworkPredictionData_Client_Character(Movement) {}
		virtual FSavedMovePtr AllocateNewMove() override { return FSavedMovePtr(new FGuLiExternalSavedMove); }
	};
	struct FExternalMoveContainer final : FCharacterNetworkMoveDataContainer
	{
		FGuLiExternalMoveData Moves[3];
		FExternalMoveContainer() { NewMoveData=&Moves[0]; PendingMoveData=&Moves[1]; OldMoveData=&Moves[2]; }
	};

}
struct FGuLiExternalMovementNetworkStorage { FExternalMoveContainer Moves; FGuLiExternalMoveResponse Response; };
void FGuLiExternalMovementStorageDeleter::operator()(FGuLiExternalMovementNetworkStorage* Storage) const { delete Storage; }
UGuLiExternalCharacterMovementComponent::UGuLiExternalCharacterMovementComponent(const FObjectInitializer& Initializer) : Super(Initializer)
{
	// Version-1 object scale. Absolute defaults, never multiplied during initialization/pooling.
	MaxAcceleration = 409.6f;
	BrakingDecelerationWalking = 409.6f;
	MaxStepHeight = 9.0f;
	PerchRadiusThreshold = 0.0f;
	PerchAdditionalHeight = 8.0f;
	JumpZVelocity = 84.0f;
	GravityScale = 0.2f; // Per-character acceleration only; world gravity/terrain remain unchanged.
	NetworkMaxSmoothUpdateDistance = 51.2f;
	NetworkNoSmoothUpdateDistance = 76.8f;
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
	if (static_cast<const FGuLiExternalMoveData&>(Move).Revision != GetDisplacementRevision()
		|| UGuLiExternalUnitControlComponent::AreActorActionsLocked(GetOwner())) { return; }
	Super::ServerMove_PerformMovement(Move);
}
void UGuLiExternalCharacterMovementComponent::ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& Response)
{
	if (static_cast<const FGuLiExternalMoveResponse&>(Response).Revision != GetDisplacementRevision()) { return; }
	BeforeValidatedMoveResponse(Response);
	Super::ClientHandleMoveResponse(Response);
	AfterValidatedMoveResponse(Response);
}
