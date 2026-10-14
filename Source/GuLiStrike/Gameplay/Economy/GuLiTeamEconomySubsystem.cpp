#include "Gameplay/Economy/GuLiTeamEconomySubsystem.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Engine/World.h"
bool UGuLiTeamEconomySubsystem::IsMatchActive() const
{
    const auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    return H && H->IsWorldReady();
}
bool UGuLiTeamEconomySubsystem::AreTransactionsOpen() const
{
    const auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    return H && H->CanPublishFacts();
}
FGuLiResourceAmounts UGuLiTeamEconomySubsystem::GetTeamBalance(EGuLiTeam Team) const
{
    const auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    return H?H->GetTeamBalance(Team):FGuLiResourceAmounts{};
}
