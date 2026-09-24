#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GuLiEngineeringPathSubsystem.generated.h"
class UGuLiEngineeringTravelComponent;
enum class EGuLiEngineeringPathOrigin : uint8 { Manual, Work, Repath };

/** Authority-only FIFO. Owns computation scheduling, never a vehicle's work decisions. */
UCLASS()
class GULISTRIKE_API UGuLiEngineeringPathSubsystem final : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual void Deinitialize() override;
	void Enqueue(UGuLiEngineeringTravelComponent& Travel);
	void Cancel(UGuLiEngineeringTravelComponent& Travel);
	void RecordPathQuery(EGuLiEngineeringPathOrigin Origin, double Milliseconds);
	UFUNCTION(BlueprintPure, Category="Engineering|Navigation") FString GetBudgetDebug() const;
	UFUNCTION(BlueprintPure, Category="Engineering|Navigation") TArray<double> GetRecentQueueWaitMilliseconds() const { return QueueWaitSamples; }
private:
	TArray<TWeakObjectPtr<UGuLiEngineeringTravelComponent>> Queue;
	uint64 QueryCount = 0;
	uint64 ActualQueries[3] = {};
	double ActualQueryMilliseconds[3] = {};
	double MaxActualQueryMilliseconds = 0;
	uint64 OverBudgetSingleQueries = 0;
	double TotalMilliseconds = 0;
	double MaxQueryMilliseconds = 0;
	double MaxQueueMilliseconds = 0;
	int32 LastFrameQueries = 0;
	TArray<double> QueueWaitSamples;
	int32 QueueWaitCursor = 0;
};
