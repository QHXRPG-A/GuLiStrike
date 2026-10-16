#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UObject/NoExportTypes.h"
#include "Battle/Network/GuLiBattleTypes.h"
#include "GuLiNetworkSettings.generated.h"

UENUM(BlueprintType)
enum class EGuLiNetworkMode : uint8 { Hybrid = 0 };
UENUM(BlueprintType)
enum class EGuLiNetworkTransport : uint8 { LocalUdp, EOS };
UENUM(BlueprintType)
enum class EGuLiClientRunMode : uint8 { Rendered, LogicOnly };

struct FGuLiNetworkLaunchConfig
{
    EGuLiNetworkMode Mode = EGuLiNetworkMode::Hybrid;
    EGuLiNetworkTransport Transport = EGuLiNetworkTransport::LocalUdp;
    EGuLiClientRunMode ClientRunMode = EGuLiClientRunMode::Rendered;
    FString CoordinatorUrl = TEXT("ws://127.0.0.1:8787/v1/match");
    FString ManifestFile;
    FGuid RoomId;
    FGuid DevelopmentPlayerId;
    EGuLiTeam Team = EGuLiTeam::Red;
    EGuLiCommanderRole Role = EGuLiCommanderRole::Commander;
    int32 LocalUdpPort = 0;
    FString ProductId, SandboxId, DeploymentId, ClientId, ClientSecret;
    double ConnectionTimeout = 10;
    double ReadyTimeout = 5;
    double MembershipTimeout = 15;
    double EmptyTeamTimeout = 30;
    FString ValidationError;
};

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="GuLi Network"))
class GULISTRIKE_API UGuLiNetworkSettings final : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UPROPERTY(Config, EditAnywhere, Category="Session") EGuLiNetworkMode Mode = EGuLiNetworkMode::Hybrid;
    UPROPERTY(Config, EditAnywhere, Category="Session") EGuLiNetworkTransport Transport = EGuLiNetworkTransport::LocalUdp;
    UPROPERTY(Config, EditAnywhere, Category="Runtime") EGuLiClientRunMode ClientRunMode = EGuLiClientRunMode::Rendered;
    UPROPERTY(Config, VisibleAnywhere, Category="Simulation") int32 WingmanMotionRateHz = 10;
    UPROPERTY(Config, EditAnywhere, Category="Session") FString CoordinatorUrl = TEXT("ws://127.0.0.1:8787/v1/match");
    UPROPERTY(Config, EditAnywhere, Category="Session") FFilePath ManifestFile;
    UPROPERTY(Config, EditAnywhere, Category="Session") FGuid RoomId;
    UPROPERTY(Config, EditAnywhere, Category="Development") FGuid DevelopmentPlayerId;
    UPROPERTY(Config, EditAnywhere, Category="Development", meta=(ClampMin="0", ClampMax="65535")) int32 LocalUdpPort = 0;
    UPROPERTY(Config, EditAnywhere, Category="Session") EGuLiTeam Team = EGuLiTeam::Red;
    UPROPERTY(Config, EditAnywhere, Category="Session") EGuLiCommanderRole Role = EGuLiCommanderRole::Commander;
    UPROPERTY(Config, EditAnywhere, Category="EOS") FString ProductId;
    UPROPERTY(Config, EditAnywhere, Category="EOS") FString SandboxId;
    UPROPERTY(Config, EditAnywhere, Category="EOS") FString DeploymentId;
    UPROPERTY(Config, EditAnywhere, Category="EOS") FString ClientId;
    // Restricted game client credential comes from GULI_EOS_GAME_CLIENT_SECRET, never a server credential.
    UPROPERTY(Config, EditAnywhere, Category="Timeouts", meta=(ClampMin="1")) float ConnectionTimeoutSeconds = 10;
    UPROPERTY(Config, EditAnywhere, Category="Timeouts", meta=(ClampMin="1")) float ReadyTimeoutSeconds = 5;
    UPROPERTY(Config, EditAnywhere, Category="Timeouts", meta=(ClampMin="1")) float MembershipTimeoutSeconds = 15;
    UPROPERTY(Config, EditAnywhere, Category="Timeouts", meta=(ClampMin="1")) float EmptyTeamTimeoutSeconds = 30;

    UPROPERTY(Config,EditAnywhere,Category="Ownership",meta=(ClampMin="0")) double CommanderIdleSeconds=10;
    UPROPERTY(Config,EditAnywhere,Category="Ownership",meta=(ClampMin="0.1")) double OwnershipEvaluationSeconds=1;
    UPROPERTY(Config,EditAnywhere,Category="Ownership",meta=(ClampMin="0.1")) double HandoffTimeoutSeconds=5;
    UPROPERTY(Config,EditAnywhere,Category="Ownership",meta=(ClampMin="0",ClampMax="0.99")) double MinimumRelativeLoadGap=0.20;
    UPROPERTY(Config,EditAnywhere,Category="Ownership",meta=(ClampMin="0")) double MinimumLoadGapMilliseconds=0.5;
    UPROPERTY(Config,EditAnywhere,Category="Ownership",meta=(ClampMin="1",ClampMax="4096")) int32 MaximumBackgroundHandoffBatch=32;
    /** Per-peer socket payload budgets. Simulation never waits on these lanes. */
    UPROPERTY(Config,EditAnywhere,Category="Transport",meta=(ClampMin="1000")) int32 ReliableBytesPerSecond=2097152;
    UPROPERTY(Config,EditAnywhere,Category="Transport",meta=(ClampMin="1000")) int32 MotionBytesPerSecond=160000;
    UPROPERTY(Config,EditAnywhere,Category="Transport",meta=(ClampMin="1000")) int32 HandoffBytesPerSecond=1048576;

    static FGuLiNetworkLaunchConfig Resolve();
    static bool IsHybrid(const UWorld* World);
    static bool IsLogicOnly();
    static bool CanPresent(const UWorld* World);
    /** Presentation graphs share the same Rendered/LogicOnly policy as native
     * code. This is not an object-simulation or transaction-authority query. */
    UFUNCTION(BlueprintPure, Category="GuLiStrike|Presentation", meta=(WorldContext="WorldContextObject"))
    static bool IsPresentationDisabled(const UObject* WorldContextObject);
    static bool CanCapturePerformance(const UWorld* World);
    static uint32 WingmanMotionIntervalSimulationTicks();
    static float WingmanMotionIntervalSeconds();
};
