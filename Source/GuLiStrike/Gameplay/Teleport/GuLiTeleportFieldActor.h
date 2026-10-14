#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Gameplay/Teleport/GuLiTeleportTypes.h"
#include "GuLiTeleportFieldActor.generated.h"
class AGuLiBattlePlayerState;
class UDecalComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

/** Local presentation of the teleport capability state; execution belongs to its object owner. */
UCLASS(NotBlueprintable)
class GULISTRIKE_API AGuLiTeleportFieldActor : public AActor
{
	GENERATED_BODY()
public:
	AGuLiTeleportFieldActor();



	virtual void Tick(float DeltaSeconds) override;

	static AGuLiTeleportFieldActor* FindCast(UWorld& World, FGuid CommanderId);
	static double GetSynchronizedTime(const UWorld& World);

	UFUNCTION(BlueprintPure, Category="Commander|Teleport")
	FGuLiTeleportCastState GetCastState() const { return State; }
	void SetPreview(const FVector& Point, float Radius, bool bValid);
    void ApplyHybridView(const FGuLiTeleportCastState& View);
private:

	void TickVisuals();
	void EnsureMaterials();
	UPROPERTY() TObjectPtr<UDecalComponent> Ground;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Beam;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> GroundMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BeamMID;
	UPROPERTY() FGuLiTeleportCastState State;
	bool bPreview = false;
};
