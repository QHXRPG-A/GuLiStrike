#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "EditorSubsystem.h"
#include "Containers/Ticker.h"
#include "Network/GuLiNetworkSettings.h"
#include "GuLiLocalPIESubsystem.generated.h"

/** Normal local play, separate from the acceptance launcher and its preparations. */
UCLASS(Config=Editor, DefaultConfig, meta=(DisplayName="GuLi Local PIE"))
class GULISTRIKEEDITOR_API UGuLiLocalPIESettings final : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UPROPERTY(Config, EditAnywhere, Category="Local PIE") bool bEnabled=true;
    /** Interactive local play chooses a seat before any session process starts. */
    UPROPERTY(Config, EditAnywhere, Category="Local PIE", meta=(DisplayName="开局选择席位"))
    bool bPromptForRole=true;
    UPROPERTY(Config, EditAnywhere, Category="Local PIE", meta=(ValidEnumValues="Commander,Ground,Air"))
    EGuLiCommanderRole MainRole=EGuLiCommanderRole::Commander;
    UPROPERTY(Config, EditAnywhere, Category="Local PIE", meta=(ClampMin="1024",ClampMax="65535"))
    int32 CoordinatorPort=18787;
    virtual FName GetCategoryName() const override { return TEXT("GuLiStrike"); }
};

/** Owns exactly two children; the existing editor hosts the rendered PIE client. */
UCLASS()
class GULISTRIKEEDITOR_API UGuLiLocalPIESubsystem final : public UEditorSubsystem
{
    GENERATED_BODY()
public:
    UGuLiLocalPIESubsystem();
    virtual ~UGuLiLocalPIESubsystem() override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    bool PrepareForPIE(FString& Error);
    UFUNCTION(BlueprintCallable, Category="GuLiStrike|Local PIE") FString GetSessionStatus() const;
    /** Explicit performance fixture only; ordinary production commits remain unchanged. */
    UFUNCTION(BlueprintCallable, Category="GuLiStrike|Local PIE", meta=(DevelopmentOnly))
    FString PreparePerformanceProduction(const TArray<FString>& AlreadyPrepared);
private:
    struct FState;
    struct FStateDeleter { void operator()(FState* Value) const; };
    TUniquePtr<FState,FStateDeleter> State;
    FDelegateHandle PreHandle,BeginHandle,EndHandle,MapHandle,ExitHandle;
    FTSTicker::FDelegateHandle TickHandle;
    void BeforePIE(bool Simulate);
    void BeganPIE(bool Simulate);
    void EndedPIE(bool Simulate);
    void MapOpened(const FString& Name,bool AsTemplate);
    void ConfigurePlaySettings();
    void StopChildren();
    bool Tick(float Delta);
    bool ResolveWorld(const UWorld* World,FGuLiNetworkLaunchConfig& Config);
};
