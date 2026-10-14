#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Gameplay/Skills/GuLiSkillTypes.h"
#include "GuLiRogueCardEffect.generated.h"
class AGuLiBattlePlayerState;
struct FGuLiStrikeRogueCardsCardsRow;

/** Pure authored effect description exported to the shared numeric capability. */
struct FGuLiRogueEffectDefinition
{
    FGuLiSkillSource Source;
    TMap<uint16,float> MovementPercent;
};

/** Data-defined effects export descriptions; the owner capability applies numerical changes. */
UCLASS(Abstract, BlueprintType)
class GULISTRIKE_API UGuLiRogueCardEffect : public UObject
{
	GENERATED_BODY()
public:
    virtual bool Describe(const FGuLiStrikeRogueCardsCardsRow&,FGuLiRogueEffectDefinition&,FString& Error) const;
	virtual bool Validate(const FGuLiStrikeRogueCardsCardsRow& Card, FString& Error) const;
	virtual FText FormatDescription(const FGuLiStrikeRogueCardsCardsRow& Card, const FText& Pattern) const;
};
UCLASS()
class GULISTRIKE_API UGuLiRogueCardFireRateEffect final : public UGuLiRogueCardEffect
{
	GENERATED_BODY()
public:
    virtual bool Describe(const FGuLiStrikeRogueCardsCardsRow&,FGuLiRogueEffectDefinition&,FString&) const override;

};
UCLASS()
class GULISTRIKE_API UGuLiRogueCardMoveSpeedEffect final : public UGuLiRogueCardEffect
{
	GENERATED_BODY()
public:
    virtual bool Describe(const FGuLiStrikeRogueCardsCardsRow&,FGuLiRogueEffectDefinition&,FString&) const override;

};
UCLASS()
class GULISTRIKE_API UGuLiRogueCardMissileDamageEffect final : public UGuLiRogueCardEffect
{
	GENERATED_BODY()
public:
    virtual bool Describe(const FGuLiStrikeRogueCardsCardsRow&,FGuLiRogueEffectDefinition&,FString&) const override;

};

UCLASS()
class GULISTRIKE_API UGuLiRogueCardMissilePodEffect final : public UGuLiRogueCardEffect
{
	GENERATED_BODY()
public:
    virtual bool Describe(const FGuLiStrikeRogueCardsCardsRow&,FGuLiRogueEffectDefinition&,FString&) const override;
	virtual bool Validate(const FGuLiStrikeRogueCardsCardsRow&, FString&) const override;
	virtual FText FormatDescription(const FGuLiStrikeRogueCardsCardsRow&, const FText& Pattern) const override { return Pattern; }

};

UCLASS()
class GULISTRIKE_API UGuLiRogueCardMissileCountEffect final : public UGuLiRogueCardEffect
{
	GENERATED_BODY()
public:
    virtual bool Describe(const FGuLiStrikeRogueCardsCardsRow&,FGuLiRogueEffectDefinition&,FString&) const override;
	virtual bool Validate(const FGuLiStrikeRogueCardsCardsRow&, FString&) const override;
	virtual FText FormatDescription(const FGuLiStrikeRogueCardsCardsRow&, const FText&) const override;

};
