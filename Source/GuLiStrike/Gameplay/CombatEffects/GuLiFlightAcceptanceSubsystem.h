#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectTypes.h"
#include "GuLiFlightAcceptanceSubsystem.generated.h"

/** Explicitly activated acceptance load. Does nothing during normal gameplay. */
UCLASS()
class GULISTRIKE_API UGuLiFlightAcceptanceSubsystem final : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void Deinitialize() override;
	UFUNCTION(BlueprintCallable, Category="Network|Acceptance") bool StartLoad(int32 FlightCount=500,float Duration=30);
	UFUNCTION(BlueprintCallable, Category="Network|Acceptance") void StopLoad();
private:
	bool SpawnFlight(int32 Domain,int32 Ordinal);
	TArray<FGuLiCombatTargetSnapshot> Sources;
	TArray<FGuid> Flights[4];
	FGuLiTargetHandle EnemyTarget;
	FVector Origin = FVector::ZeroVector;
	UPROPERTY() TSubclassOf<class AGuLiStrikeProjectile> ShipClass;
	UPROPERTY() TObjectPtr<class UGuLiProjectileEffectDefinition> CurveDefinition;
	float FinishTime = 0, NextRefill = 0;
	int32 Desired = 500, Serial = 0, Peak = 0;
	bool bRunning = false;
};
