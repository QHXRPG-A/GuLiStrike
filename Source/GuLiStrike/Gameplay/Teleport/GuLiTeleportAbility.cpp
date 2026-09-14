#include "Gameplay/Teleport/GuLiTeleportAbility.h"
#include "Gameplay/Teleport/GuLiTeleportFieldActor.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Engine/World.h"

UE_DEFINE_GAMEPLAY_TAG(TAG_GuLi_CommanderTeleport, "GuLi.Commander.Teleport");

UGuLiTeleportAbility::UGuLiTeleportAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	FAbilityTriggerData Trigger;
	Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	Trigger.TriggerTag = TAG_GuLi_CommanderTeleport;
	AbilityTriggers.Add(Trigger);
}

void UGuLiTeleportAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	const auto* Payload = TriggerEventData ? Cast<UGuLiTeleportCommandPayload>(TriggerEventData->OptionalObject.Get()) : nullptr;
	auto* Commander = ActorInfo ? Cast<AGuLiBattlePlayerState>(ActorInfo->OwnerActor.Get()) : nullptr;
	if (Payload && Commander && Commander->HasAuthority() && Payload->GetOuter() == Commander)
	{
		if (Payload->Command == EGuLiTeleportCommand::Source)
		{
			if (auto* Field = AGuLiTeleportFieldActor::StartCast(*Commander,Payload->Level,Payload->Point,Payload->Error))
			{ Payload->CastId = Field->GetCastState().CastId; Payload->bSucceeded = true; }
		}
		else if (auto* Field = AGuLiTeleportFieldActor::FindCast(*Commander->GetWorld(),Commander->GetPlayerGuid()); Field && Field->GetCastState().CastId == Payload->CastId)
		{
			if (Payload->Command == EGuLiTeleportCommand::Destination) { Payload->bSucceeded = Field->SubmitDestination(*Commander,Payload->Point,Payload->Error); }
			else { Field->Cancel(*Commander); Payload->bSucceeded = true; }
		}
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, !Payload || !Payload->bSucceeded);
}
