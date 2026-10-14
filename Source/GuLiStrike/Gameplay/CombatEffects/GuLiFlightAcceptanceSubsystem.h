#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectTypes.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "GuLiFlightAcceptanceSubsystem.generated.h"

/** Explicitly activated acceptance load. Does nothing during normal gameplay. */
UCLASS()
class GULISTRIKE_API UGuLiFlightAcceptanceSubsystem final : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;
	virtual void OnWorldBeginPlay(UWorld& World) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void Deinitialize() override;
	UFUNCTION(BlueprintCallable, Category="Network|Acceptance") bool StartLoad(int32 FlightCount=500,float Duration=30);
	/** Uses the production ground-missile definition, live Wingman identity and frozen authored explosion field. */
	UFUNCTION(BlueprintCallable, Category="Network|Acceptance") bool StartWingmanGroundLoad(int32 FlightCount=125, float Duration=45, class UGuLiGroundWarningStyle* WarningStyle=nullptr);
	UFUNCTION(BlueprintCallable, Category="Network|Acceptance") void StopLoad();
	UFUNCTION(BlueprintPure, Category="Network|Acceptance") FString GetLoadStatsJson() const;
	const FString& GetLoadFailure() const { return LoadFailure; }
	bool HasAutomaticLoad() const { return bAutomaticLoad; }
private:
    friend class FGuLiAutomaticFlightState;
	void AdvanceAutomaticLoad();
	FString LoadFailure;
	bool bAutomaticLoad=false;
	int32 AutomaticCount=0,AutomaticSeed=0;
	uint32 AutomaticEpoch=0;
	uint64 AutomaticOrdinal=0;
	TMap<int32,FGuid> AutomaticFlights;
	TMap<int32,FGuid> AutomaticPreparedFlights;
	TMap<int32,uint64> AutomaticGenerations;
    TMap<int32,FGuLiHybridObjectKey> AutomaticSources;
    struct FSourceReplacement { int32 Index=0; FGuLiHybridObjectKey Previous,Next; uint64 At=0; };
    TArray<FSourceReplacement> AutomaticSourceReplacements;
    uint64 AutomaticReplacementCount=0;
	int32 AutomaticLocalGoal=0,AutomaticLive=0,AutomaticMinimum=MAX_int32,AutomaticOrdinary=0;
    int32 AutomaticProducerCount=0;
	FGuid AutomaticSampleId;
	FVector AutomaticSamplePosition=FVector::ZeroVector;
	float AutomaticSampleTime=0;
	int64 AutomaticSpeedSamples=0;
	double AutomaticMinimumSpeed=TNumericLimits<double>::Max(),AutomaticMaximumSpeed=0;
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
