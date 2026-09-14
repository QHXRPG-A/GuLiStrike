#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Gameplay/Teleport/GuLiTeleportTypes.h"
#include "GuLiTeleportFieldActor.generated.h"
class AGuLiBattlePlayerState;
class UDecalComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
struct FGuLiTeleportFieldRuntime;
struct FGuLiTeleportFieldRuntimeDeleter { void operator()(FGuLiTeleportFieldRuntime* Runtime) const; };

/** One cast, one owner, one participant list. Authority runs the transaction; clients derive presentation. */
UCLASS(NotBlueprintable)
class GULISTRIKE_API AGuLiTeleportFieldActor : public AActor
{
	GENERATED_BODY()
public:
	AGuLiTeleportFieldActor();
	virtual ~AGuLiTeleportFieldActor() override;
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	static AGuLiTeleportFieldActor* StartCast(AGuLiBattlePlayerState& Commander, int32 Level, FVector Source, FString& Error);
	static AGuLiTeleportFieldActor* FindCast(UWorld& World, FGuid CommanderId);
	static double GetSynchronizedTime(const UWorld& World);
	bool SubmitDestination(AGuLiBattlePlayerState& Commander, FVector Point, FString& Error);
	void Cancel(AGuLiBattlePlayerState& Commander);
	UFUNCTION(BlueprintPure, Category="Commander|Teleport")
	FGuLiTeleportCastState GetCastState() const { return State; }
	void SetPreview(const FVector& Point, float Radius, bool bValid);
private:
	void Capture();
	bool PlanLanding(FVector Center, bool bReturning);
	bool ApplyParticipants(bool bPhased, bool bLocked, bool bDisplace);
	bool CommitLanding(bool bReturning);
	void ReturnToSource(const FString& Reason);
	void Finish(const FString& Message);
	void PruneParticipants();
	void Publish();
	void TickAuthority();
	void OnWorldPostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds);
	void TickVisuals();
	void EnsureMaterials();
	UPROPERTY() TObjectPtr<UDecalComponent> Ground;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Beam;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> GroundMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BeamMID;
	UPROPERTY(Replicated) FGuLiTeleportCastState State;
	TUniquePtr<FGuLiTeleportFieldRuntime,FGuLiTeleportFieldRuntimeDeleter> Runtime;
	bool bPreview = false;
	FDelegateHandle WorldTickHandle;
};
