#include "Gameplay/Skills/GuLiArmySkillReplicationActor.h"
#include "Gameplay/Skills/GuLiArmySkillSubsystem.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

AGuLiArmySkillReplicationActor::AGuLiArmySkillReplicationActor()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	PrimaryActorTick.bCanEverTick = false;
}

void AGuLiArmySkillReplicationActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGuLiArmySkillReplicationActor, State);
}

void AGuLiArmySkillReplicationActor::Publish(uint32 MatchEpoch, const TArray<FGuLiResolvedSkillProfile>& Profiles)
{
	if (!HasAuthority()) return;
	State.MatchEpoch = MatchEpoch;
	State.Profiles = Profiles;
	ForceNetUpdate();
}

void AGuLiArmySkillReplicationActor::OnRep_State()
{
	if (auto* Subsystem = GetWorld()->GetSubsystem<UGuLiArmySkillSubsystem>())
	{
		Subsystem->ReceiveReplicatedProfiles(State.MatchEpoch, State.Profiles);
	}
}
