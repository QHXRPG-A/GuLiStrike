#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GuLiPerformanceSubsystem.generated.h"

class FJsonValue;

/** Opt-in counters are owned by a World; view counters include the LocalPlayer identity. */
UCLASS()
class GULISTRIKE_API UGuLiPerformanceSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return bCapturing && Super::IsTickable(); }
	virtual TStatId GetStatId() const override;
	UFUNCTION(BlueprintCallable, Category="Performance") void BeginCapture();
	UFUNCTION(BlueprintCallable, Category="Performance") void EndCapture();
	UFUNCTION(BlueprintPure, Category="Performance") FString GetCaptureJson() const;
	void Record(FName Name, double Value, const UObject* LocalPlayer = nullptr);
	bool IsCapturing() const { return bCapturing; }
    bool IsCaptureValid() const { return DroppedFrameSamples==0 && DroppedNetworkSamples==0; }
private:
	struct FCounter
	{
		double Total = 0, FrameTotal = 0;
		uint64 Calls = 0, Frame = 0, FrameQpc = 0;
		TArray<double> FrameSamples;
		TArray<uint64> FrameTimes, FrameIds;
	};
	TMap<FString, FCounter> Counters;
	uint64 FirstFrame = 0, LastFrame = 0;
	bool bCapturing = false;
	TArray<TSharedPtr<FJsonValue>> NetworkSamples;
	double LastNetworkSample = -1;
	uint32 DroppedNetworkSamples = 0;
    uint64 DroppedFrameSamples = 0;
	void CaptureNetworkSample();
};

class FGuLiPerformanceScope
{
public:
	FGuLiPerformanceScope(UWorld* World, FName Name, const UObject* LocalPlayer = nullptr);
	~FGuLiPerformanceScope();
private:
	UGuLiPerformanceSubsystem* Capture = nullptr;
	FName Name;
	const UObject* LocalPlayer = nullptr;
	double Started = 0;
};
