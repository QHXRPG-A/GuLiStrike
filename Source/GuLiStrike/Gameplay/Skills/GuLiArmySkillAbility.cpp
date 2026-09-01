#include "Gameplay/Skills/GuLiArmySkillAbility.h"
#include "Gameplay/Skills/GuLiArmySkillSubsystem.h"
#include "Gameplay/Skills/GuLiSkillTags.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Engine/World.h"

UGuLiArmySkillAbility::UGuLiArmySkillAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	FAbilityTriggerData Trigger;
	Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	Trigger.TriggerTag = TAG_GuLi_ArmySkillCommand;
	AbilityTriggers.Add(Trigger);
}

void UGuLiArmySkillAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	const auto* Payload = TriggerEventData ? Cast<UGuLiArmySkillCommandPayload>(TriggerEventData->OptionalObject.Get()) : nullptr;
	const auto* Commander = ActorInfo ? Cast<AGuLiBattlePlayerState>(ActorInfo->OwnerActor.Get()) : nullptr;
	if (Payload)
	{
		Payload->bExecuted = true;
		if (Commander && Commander->HasAuthority() && Payload->GetOuter() == Commander)
		{
			if (auto* Bridge = Commander->GetWorld()->GetSubsystem<UGuLiArmySkillSubsystem>())
			{
				Payload->bSucceeded = Bridge->ExecuteCommand(*Commander, Payload->Request, Payload->Error);
			}
			else Payload->Error = TEXT("Army skill bridge is unavailable.");
		}
		else Payload->Error = TEXT("Army skill ability requires an authoritative owning PlayerState payload.");
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, !Payload || !Payload->bSucceeded);
}
