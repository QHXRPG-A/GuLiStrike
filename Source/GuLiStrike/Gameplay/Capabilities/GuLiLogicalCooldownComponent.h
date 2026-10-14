#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GuLiLogicalCooldownComponent.generated.h"

DECLARE_MULTICAST_DELEGATE(FGuLiLogicalCooldownChanged);

/** Capability-owned cooldown domains; private implementation uses portable core state. */
UCLASS(ClassGroup=(Gameplay),meta=(BlueprintSpawnableComponent))
class GULISTRIKE_API UGuLiLogicalCooldownComponent final : public UActorComponent
{
    GENERATED_BODY()
public:
    UGuLiLogicalCooldownComponent();
    virtual ~UGuLiLogicalCooldownComponent() override;

    bool Reserve(FName Domain,FName Name,FGuid Token,float Seconds);
    bool Rollback(FName Domain,FName Name,FGuid Token);
    bool ClearDomain(FName Domain);
    bool IsActive(FName Domain,FName Name) const;
    double EndsAtSeconds(FName Domain,FName Name) const;
    TArray<FName> GetActiveNames(FName Domain) const;
    bool Advance(uint64 Tick);
    bool CaptureState(TArray<uint8>& Out) const;
    bool ValidateState(TConstArrayView<uint8> Bytes) const;
    bool RestoreState(TConstArrayView<uint8> Bytes);
    FGuLiLogicalCooldownChanged OnChanged;
private:
    struct FImpl;
    TSharedPtr<FImpl> Runtime;
    uint64 LogicalTick() const;
};
