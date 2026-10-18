#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GuLiPerformanceSubsystem.generated.h"

class FJsonValue;
class FJsonObject;
struct FGuLiHotspotCapture;

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
    /** Explicit development capture preparation; never invoked by ordinary PIE. */
    UFUNCTION(BlueprintCallable, Category="Performance", meta=(DevelopmentOnly)) FString PrepareHotspotWorkload(const FString& FrozenJson,bool Interaction);
    /** Preparation only: select exact live controlled units without changing gameplay state. */
    UFUNCTION(BlueprintCallable, Category="Performance", meta=(DevelopmentOnly)) FString PrepareHotspotSelection(const TArray<int32>& Soldiers);
    /** Explicit transient spatial preparation; native weapons and damage remain unchanged. */
    UFUNCTION(BlueprintCallable, Category="Performance", meta=(DevelopmentOnly)) FString PrepareHotspotCombat(int32 LocalUnits);
    UFUNCTION(BlueprintCallable, Category="Performance", meta=(DevelopmentOnly)) bool StartHotspotWindow();
	void Record(FName Name, double Value, const UObject* LocalPlayer = nullptr);
    void RecordSample(FName Name,double Value);
	bool IsCapturing() const { return bCapturing; }
    bool IsCaptureValid() const { return DroppedFrameSamples==0 && DroppedNetworkSamples==0 && DroppedIndividualSamples==0; }
private:
    TSharedPtr<FGuLiHotspotCapture> Hotspot;
    void TickHotspot(float DeltaTime);
    void AppendHotspotCapture(const TSharedRef<FJsonObject>& Root) const;
	struct FCounter
	{
		double Total = 0, FrameTotal = 0;
		uint64 Calls = 0, Frame = 0, FrameQpc = 0;
		TArray<double> FrameSamples;
		TArray<uint64> FrameTimes, FrameIds;
	};
	TMap<FString, FCounter> Counters;
    struct FSampleCounter {double Total=0;TArray<double> Values;};
    TMap<FName,FSampleCounter> SampleCounters;
    uint64 DroppedIndividualSamples=0;
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
