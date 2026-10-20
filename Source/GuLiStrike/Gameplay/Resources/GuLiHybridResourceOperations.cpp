#include "Gameplay/Resources/GuLiMiningVehiclePawn.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Resources/GuLiResourceInventoryComponent.h"
#include "Network/GuLiNetworkSettings.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Engine/World.h"

FGuLiResourceAmounts AGuLiMiningVehiclePawn::GetCargo() const
{
    const auto* Inventory=FindComponentByClass<UGuLiResourceInventoryComponent>();
    return Inventory?Inventory->GetCargo():FGuLiResourceAmounts{};
}
void AGuLiMiningVehiclePawn::ApplyHybridResourceResult(const FGuLiHybridTransactionResult& R)
{
    if (R.Status==EGuLiHybridTransactionStatus::Deferred || R.Status==EGuLiHybridTransactionStatus::Paused
        || R.Status==EGuLiHybridTransactionStatus::Interrupted) return;
    auto* Inventory=FindComponentByClass<UGuLiResourceInventoryComponent>();Inventory->ApplyResourceResult(R);
    auto* Resources=GetResourceSubsystem(); const auto Key=Resources->GetSettlementKey(this); if (!Key.IsValid()) return;
    const bool Pending=R.Event==PendingResourceEvent;
    if (R.IsCommitted() && R.ResourceSource==Key
        && (R.ResourceOperation==EGuLiHybridResourceOperation::Extract || R.ResourceOperation==EGuLiHybridResourceOperation::Deliver)
        && (Pending || Inventory->GetCargoRevision()>LastResourceLedger))
    {
        LastResourceLedger=Inventory->GetCargoRevision();
        if (Pending && R.ResourceOperation==EGuLiHybridResourceOperation::Extract)
        {
            MiningAccumulator=FMath::Max(0.f,MiningAccumulator-1.f);
            NextExtractionTick=PendingResourceAt+uint64(FMath::CeilToDouble(60./MiningRatePerSecond));
        }
        if (R.ResourceOperation==EGuLiHybridResourceOperation::Deliver && !GetCargoTotal()
            && TaskState==EGuLiMiningTaskState::Docking) BehaviorResult=EGuLiCommanderWorkResult::Unloaded;
        if (GetCargoTotal()>=CargoCapacity && TaskState==EGuLiMiningTaskState::Mining)
        {SetMiningVisual(false); BehaviorResult=EGuLiCommanderWorkResult::Full;}
    }
    if (!Pending) return;
    PendingResourceEvent.Invalidate(); PendingResourceOperation=0;
    if (!R.IsCommitted() && R.Status!=EGuLiHybridTransactionStatus::Interrupted && UGuLiHybridActorComponent::HasSimulation(this))
    {SetMiningVisual(false); BehaviorResult=EGuLiCommanderWorkResult::TargetLost;}
}
