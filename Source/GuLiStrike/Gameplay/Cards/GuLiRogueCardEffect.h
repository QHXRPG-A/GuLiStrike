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
	virtual bool Validate(const FGuLiStrikeRogueCardsCardsRow& Card, FString& Error) const;
	virtual FText FormatDescription(const FGuLiStrikeRogueCardsCardsRow& Card, const FText& Pattern) const;
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

UCLASS()
class GULISTRIKE_API UGuLiRogueCardMissilePodEffect final : public UGuLiRogueCardEffect
{
	GENERATED_BODY()
public:
	virtual bool Validate(const FGuLiStrikeRogueCardsCardsRow&, FString&) const override;
	virtual FText FormatDescription(const FGuLiStrikeRogueCardsCardsRow&, const FText& Pattern) const override { return Pattern; }
	virtual bool Apply(const AGuLiBattlePlayerState&, const FGuLiStrikeRogueCardsCardsRow&, FGuid, FString&) const override;
};

UCLASS()
class GULISTRIKE_API UGuLiRogueCardMissileCountEffect final : public UGuLiRogueCardEffect
{
	GENERATED_BODY()
public:
	virtual bool Validate(const FGuLiStrikeRogueCardsCardsRow&, FString&) const override;
	virtual FText FormatDescription(const FGuLiStrikeRogueCardsCardsRow&, const FText&) const override;
	virtual bool Apply(const AGuLiBattlePlayerState&, const FGuLiStrikeRogueCardsCardsRow&, FGuid, FString&) const override;
};
