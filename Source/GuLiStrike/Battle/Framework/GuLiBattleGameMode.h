// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Battle/Network/GuLiBattleTypes.h"
#include "GameFramework/GameMode.h"
#include "GuLiBattleGameMode.generated.h"
class AGuLiBattlePlayerState;

/** Local-world spawn configuration. Membership and combat lifecycle belong to the coordinator. */
UCLASS()
class GULISTRIKE_API AGuLiBattleGameMode : public AGameMode
{
    GENERATED_BODY()
public:
    AGuLiBattleGameMode();
    virtual void RestartPlayer(AController* NewPlayer) override;
    virtual UClass* GetDefaultPawnClassForController_Implementation(AController* Controller) override;
    bool EnsureHybridCommanderPawn(APlayerController& Controller);
    UClass* GetConfiguredRolePawnClass(EGuLiCommanderRole AssignedRole) const { return ResolveConfiguredPawnClass(AssignedRole); }
protected:
    UPROPERTY(EditDefaultsOnly,Category="Battle|Spawning")
    TMap<EGuLiCommanderRole,TSubclassOf<APawn>> RolePawnClasses;
private:
    UClass* ResolveConfiguredPawnClass(EGuLiCommanderRole AssignedRole) const;
    bool HasCurrentRoleSlot(const AGuLiBattlePlayerState& State) const;
};
