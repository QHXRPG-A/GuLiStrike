#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Network/GuLiNetworkSettings.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "GuLiHybridNetworkReviewActor.generated.h"

/** Saved review configuration. Construction and BeginPlay never launch a test. */
UCLASS(Blueprintable)
class GULISTRIKE_API AGuLiHybridNetworkReviewActor final : public AActor
{
    GENERATED_BODY()
public:
    AGuLiHybridNetworkReviewActor();
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Hybrid Review") EGuLiNetworkMode Mode=EGuLiNetworkMode::Hybrid;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hybrid Review") EGuLiNetworkTransport Transport=EGuLiNetworkTransport::LocalUdp;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hybrid Review") FGuid Room;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hybrid Review") FGuid DevelopmentIdentity;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hybrid Review") EGuLiTeam Team=EGuLiTeam::Red;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hybrid Review") EGuLiCommanderRole Seat=EGuLiCommanderRole::Commander;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hybrid Review") EGuLiClientRunMode RunMode=EGuLiClientRunMode::Rendered;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hybrid Review",meta=(ClampMin="200",ClampMax="600")) int32 Units=600;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hybrid Review",meta=(ClampMin="0",ClampMax="500")) int32 AdditionalFlights=500;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hybrid Review",meta=(ClampMin="0",ClampMax="65535")) int32 UdpPort=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hybrid Review") FString CoordinatorUrl=TEXT("ws://127.0.0.1:8787/v1/match");
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hybrid Review") FFilePath Manifest;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Hybrid Review") FString LaunchArguments;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Hybrid Review") FString PreparationError;
    /** Produces arguments only; the launcher owns all processes and CPU grouping. */
    UFUNCTION(CallInEditor,BlueprintCallable,Category="Hybrid Review") void PrepareLaunchArguments();
    /** Pure authoring validation; also used by saved-entity readback to detect
     * stale arguments after a property or manifest change. Does not launch. */
    bool BuildLaunchArguments(FString& OutArguments,FString& OutError) const;
    UFUNCTION(BlueprintCallable,Category="Hybrid Review") void SelectObjects(const TArray<FGuLiHybridObjectKey>& Objects);
    UFUNCTION(BlueprintPure,Category="Hybrid Review") FString GetOwnershipStatus() const;
    UFUNCTION(BlueprintCallable,Category="Hybrid Review") bool LeaveRoom();
};
