#include "Gameplay/CombatEffects/GuLiCombatEffectReplicationComponent.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectPresentationSubsystem.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

UGuLiCombatEffectReplicationComponent::UGuLiCombatEffectReplicationComponent()
{
    SetIsReplicatedByDefault(false);
    PrimaryComponentTick.bCanEverTick=false;
#if !UE_BUILD_SHIPPING
    FString ReviewCase;
    bReviewReceipts=FParse::Value(FCommandLine::Get(),TEXT("GuLiAcceptanceCase="),ReviewCase) && ReviewCase!=TEXT("steady");
#endif
}
void UGuLiCombatEffectReplicationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    ActiveFlights.Reset();
    AdditionalReceipts.Reset();ReceiptOrder.Reset();NextReceipt=0;
    Super::EndPlay(Reason);
}
bool UGuLiCombatEffectReplicationComponent::IsCurrentEpoch(uint32 MatchEpoch) const
{
    const auto* Battle=GetWorld()?GetWorld()->GetGameState<AGuLiBattleGameState>():nullptr;
    return MatchEpoch!=0 && Battle && MatchEpoch==Battle->GetMatchEpoch();
}
void UGuLiCombatEffectReplicationComponent::HandleEpoch(uint32 NewEpoch)
{
    if (NewEpoch==Epoch || !IsCurrentEpoch(NewEpoch)) return;
    Epoch=NewEpoch;ActiveFlights.Reset();ObservedFlights=FinishedFlights=0;
    AdditionalReceipts.Reset();ReceiptOrder.Reset();NextReceipt=0;
    if (auto* Visuals=GetWorld()->GetSubsystem<UGuLiCombatEffectPresentationSubsystem>()) Visuals->BeginEpoch(Epoch);
}
