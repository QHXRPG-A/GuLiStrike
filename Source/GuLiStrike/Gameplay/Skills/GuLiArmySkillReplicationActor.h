#pragma once
#include "GameFramework/Actor.h"
#include "Gameplay/Skills/GuLiSkillTypes.h"
#include "GuLiArmySkillReplicationActor.generated.h"

USTRUCT()
struct FGuLiArmySkillReplicatedState
{
	GENERATED_BODY()
	UPROPERTY() uint32 MatchEpoch = 0;
	UPROPERTY() TArray<FGuLiResolvedSkillProfile> Profiles;
};

/** One small final-configuration snapshot per World; never per-shot replication. */
UCLASS(NotBlueprintable)
class GULISTRIKE_API AGuLiArmySkillReplicationActor final : public AActor
{
	GENERATED_BODY()
public:
	AGuLiArmySkillReplicationActor();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	void Publish(uint32 MatchEpoch, const TArray<FGuLiResolvedSkillProfile>& Profiles);
private:
	UPROPERTY(ReplicatedUsing = OnRep_State) FGuLiArmySkillReplicatedState State;
	UFUNCTION() void OnRep_State();
};
