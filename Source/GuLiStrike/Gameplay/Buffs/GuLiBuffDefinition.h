#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GuLiNetBuffs.h"
#include "GuLiBuffDefinition.generated.h"
class UDataTable;
struct FGuLiStrikeBuffsBuffsRow;

/** Excel-owned identity and class routing; the portable state carries no UObject paths. */
UCLASS(Config=Game,DefaultConfig,meta=(DisplayName="Buffs"))
class GULISTRIKE_API UGuLiBuffSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UGuLiBuffSettings();
    UPROPERTY(Config,EditAnywhere,Category="Data") TSoftObjectPtr<UDataTable> Buffs;
    UPROPERTY(Config,EditAnywhere,Category="Data") TSoftObjectPtr<UDataTable> Burning;
    UPROPERTY(Config,EditAnywhere,Category="Data") TSoftObjectPtr<UDataTable> BurningBurst;
};
UCLASS(Abstract)
class GULISTRIKE_API UGuLiBuffHandler : public UObject
{
    GENERATED_BODY()
public:
    virtual bool Build(const FGuLiStrikeBuffsBuffsRow&,GuLiNet::Buffs::Definition&,FString&) const;
    virtual float PreviewSeconds(const GuLiNet::Buffs::Definition&) const;
    static bool Load(int32 Id,GuLiNet::Buffs::Definition&,FString&);
};
UCLASS()
class GULISTRIKE_API UGuLiBurningBuffHandler final : public UGuLiBuffHandler
{
    GENERATED_BODY()
public:
    virtual bool Build(const FGuLiStrikeBuffsBuffsRow&,GuLiNet::Buffs::Definition&,FString&) const override;
};
UCLASS()
class GULISTRIKE_API UGuLiBurningBurstBuffHandler final : public UGuLiBuffHandler
{
    GENERATED_BODY()
public:
    virtual bool Build(const FGuLiStrikeBuffsBuffsRow&,GuLiNet::Buffs::Definition&,FString&) const override;
};
