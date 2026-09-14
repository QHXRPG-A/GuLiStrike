#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "GuLiExternalCharacterMovementComponent.generated.h"

struct FGuLiExternalMovementNetworkStorage;
struct FGuLiExternalMovementStorageDeleter
{
	void operator()(FGuLiExternalMovementNetworkStorage* Storage) const;
};

/** Character movement with a version boundary for authoritative external displacement. */
UCLASS()
class GULISTRIKE_API UGuLiExternalCharacterMovementComponent : public UCharacterMovementComponent, public IGuLiExternalDisplacementTarget
{
	GENERATED_BODY()
public:
	UGuLiExternalCharacterMovementComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual ~UGuLiExternalCharacterMovementComponent() override;
	virtual void ApplyExternalDisplacement(const FTransform& Transform) override;
	uint32 GetDisplacementRevision() const;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
protected:
	virtual void ServerMove_PerformMovement(const FCharacterNetworkMoveData& MoveData) override;
	virtual void ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& Response) override;
private:
	TUniquePtr<FGuLiExternalMovementNetworkStorage,FGuLiExternalMovementStorageDeleter> NetworkStorage;
};
