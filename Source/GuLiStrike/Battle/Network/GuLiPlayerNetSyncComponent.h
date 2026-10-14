// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Network/GuLiBattleTypes.h"
#include "Components/ActorComponent.h"
#include "GuLiPlayerNetSyncComponent.generated.h"

class AGuLiBattleGameState;
class AGuLiBattlePlayerState;
class APlayerController;

/** 可复用的本地滑动窗口；调用者持有自己的预算，载具输入不能挤占指挥官命令额度。 */
struct FGuLiNetworkRequestWindow
{
	bool Consume(double NowSeconds, int32 MaximumRequests, double WindowSeconds);
	void Reset() { RequestTimes.Reset(); }

private:
	TArray<double> RequestTimes;
};

/** Local readiness bridge. GameInstance owns the connection; this component has no RPC or replicated handshake. */
UCLASS(ClassGroup=(GuLiStrike),meta=(BlueprintSpawnableComponent))
class UGuLiPlayerNetSyncComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UGuLiPlayerNetSyncComponent();
    UFUNCTION(BlueprintPure,Category="Battle|Network") bool IsConnectionReady() const;
    uint32 GetConnectionGeneration() const;
protected:
    APlayerController* GetOwningPlayerController() const;
    AGuLiBattlePlayerState* GetBattlePlayerState() const;
    AGuLiBattleGameState* GetBattleGameState() const;
    // Existing specialized components use these local invalidation hooks.
    virtual void OnConnectionBootstrapReset() {}
    virtual void OnConnectionBootstrapReady() {}
};
