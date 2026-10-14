#pragma once
#include "CoreMinimal.h"
#include "Gameplay/Economy/GuLiEconomyTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "GuLiTeamEconomySubsystem.generated.h"
/** Read-only view of the received team account. Mutations use owner requests. */
UCLASS()
class GULISTRIKE_API UGuLiTeamEconomySubsystem final : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    bool IsMatchActive() const;
    bool AreTransactionsOpen() const;
    FGuLiResourceAmounts GetTeamBalance(EGuLiTeam Team) const;
};
