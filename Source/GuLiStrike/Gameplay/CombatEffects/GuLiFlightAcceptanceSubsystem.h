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
	/** Uses the production ground-missile definition, live Wingman identity and frozen authored explosion field. */
	UFUNCTION(BlueprintCallable, Category="Network|Acceptance") bool StartWingmanGroundLoad(int32 FlightCount=125, float Duration=45, class UGuLiGroundWarningStyle* WarningStyle=nullptr);
	UFUNCTION(BlueprintCallable, Category="Network|Acceptance") void StopLoad();
	UFUNCTION(BlueprintPure, Category="Network|Acceptance") FString GetLoadStatsJson() const;
private:
	bool SpawnFlight(int32 Domain,int32 Ordinal);
	bool SpawnWingmanGroundFlight(int32 Ordinal);
	TArray<FGuLiCombatTargetSnapshot> Sources;
	TArray<FGuid> Flights[3];
	FGuLiTargetHandle EnemyTarget;
	FVector Origin = FVector::ZeroVector;
	UPROPERTY() TObjectPtr<class UGuLiProjectileEffectDefinition> CurveDefinition;
	UPROPERTY() TObjectPtr<class UGuLiGroundWarningStyle> GroundWarningStyle;
	FGuLiSpellFieldConfig GroundField;
	FGuLiProjectileMotionSettings GroundMotion;
	float FinishTime = 0, NextRefill = 0;
	int32 Desired = 500, Serial = 0, Peak = 0;
	bool bRunning = false;
	bool bWingmanGround = false;
	int32 Accepted = 0, Completed = 0, Rejected = 0;
};
