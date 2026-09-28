#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GuLiRogueCardEffect.generated.h"
class AGuLiBattlePlayerState;
struct FGuLiStrikeRogueCardsCardsRow;

/** Only the authority subsystem calls effects, at the fixed simulation boundary. */
UCLASS(Abstract, BlueprintType)
class GULISTRIKE_API UGuLiRogueCardEffect : public UObject
{
	GENERATED_BODY()
public:
	virtual bool Apply(const AGuLiBattlePlayerState& Commander, const FGuLiStrikeRogueCardsCardsRow& Card,
		FGuid SourceId, FString& Error) const PURE_VIRTUAL(UGuLiRogueCardEffect::Apply, return false;);
};
UCLASS()
class GULISTRIKE_API UGuLiRogueCardFireRateEffect final : public UGuLiRogueCardEffect
{
	GENERATED_BODY()
public:
	virtual bool Apply(const AGuLiBattlePlayerState&, const FGuLiStrikeRogueCardsCardsRow&, FGuid, FString&) const override;
};
UCLASS()
class GULISTRIKE_API UGuLiRogueCardMoveSpeedEffect final : public UGuLiRogueCardEffect
{
	GENERATED_BODY()
public:
	virtual bool Apply(const AGuLiBattlePlayerState&, const FGuLiStrikeRogueCardsCardsRow&, FGuid, FString&) const override;
};
UCLASS()
class GULISTRIKE_API UGuLiRogueCardMissileDamageEffect final : public UGuLiRogueCardEffect
{
	GENERATED_BODY()
public:
	virtual bool Apply(const AGuLiBattlePlayerState&, const FGuLiStrikeRogueCardsCardsRow&, FGuid, FString&) const override;
};
