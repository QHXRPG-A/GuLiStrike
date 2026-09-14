#pragma once
#include "Abilities/GameplayAbility.h"
#include "NativeGameplayTags.h"
#include "GuLiTeleportAbility.generated.h"

UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GuLi_CommanderTeleport);

UENUM(BlueprintType)
enum class EGuLiTeleportCommand : uint8 { Source, Destination, Cancel };

UCLASS()
class UGuLiTeleportCommandPayload final : public UObject
{
	GENERATED_BODY()
public:
	EGuLiTeleportCommand Command = EGuLiTeleportCommand::Source;
	int32 Level = 1;
	FVector Point = FVector::ZeroVector;
	mutable FGuid CastId;
	mutable bool bSucceeded = false;
	mutable FString Error;
};

UCLASS()
class GULISTRIKE_API UGuLiTeleportAbility final : public UGameplayAbility
{
	GENERATED_BODY()
public:
	UGuLiTeleportAbility();
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
};
