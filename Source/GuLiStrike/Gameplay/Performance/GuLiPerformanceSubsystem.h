#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GuLiPerformanceSubsystem.generated.h"

class FJsonValue;
class FJsonObject;
struct FGuLiHotspotCapture;
struct FGuLiContinuousHotspot;

/** Opt-in counters are owned by a World; view counters include the LocalPlayer identity. */
UCLASS()
class GULISTRIKE_API UGuLiPerformanceSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return (bCapturing || bHotspotPending || bContinuousHotspot || ObservationDeadline>0) && Super::IsTickable(); }
	virtual TStatId GetStatId() const override;
	UFUNCTION(BlueprintCallable, Category="Performance") void BeginCapture();
    /** Native stop boundary for sparse long-run samples; no bridge call inside the window. */
    UFUNCTION(BlueprintCallable, Category="Performance", meta=(DevelopmentOnly)) bool BeginTimedCapture(float Seconds);
    /** Bounded native 1 Hz population/history/queue samples independent of short frame windows. */
    UFUNCTION(BlueprintCallable, Category="Performance", meta=(DevelopmentOnly)) bool BeginLongObservation(float Seconds);
    UFUNCTION(BlueprintPure, Category="Performance", meta=(DevelopmentOnly)) FString GetLongObservationJson() const;
    /** Native mixed-business schedule and sparse trace windows; explicit development opt-in only. */
    UFUNCTION(BlueprintCallable, Category="Performance", meta=(DevelopmentOnly)) bool BeginContinuousHotspot(const FString& OutputDirectory,float Seconds=2700);
	UFUNCTION(BlueprintCallable, Category="Performance") void EndCapture();
	UFUNCTION(BlueprintPure, Category="Performance") FString GetCaptureJson() const;
    /** Explicit development capture preparation; never invoked by ordinary PIE. */
    UFUNCTION(BlueprintCallable, Category="Performance", meta=(DevelopmentOnly)) FString PrepareHotspotWorkload(const FString& FrozenJson,bool Interaction);
    /** Preparation only: select exact live controlled units without changing gameplay state. */
    UFUNCTION(BlueprintCallable, Category="Performance", meta=(DevelopmentOnly)) FString PrepareHotspotSelection(const TArray<int32>& Soldiers);
    /** Explicit transient spatial preparation; native weapons and damage remain unchanged. */
    UFUNCTION(BlueprintCallable, Category="Performance", meta=(DevelopmentOnly)) FString PrepareHotspotCombat(int32 LocalUnits,int32 UnitType=0,int32 SkipLocalUnits=0);
    /** Transient fixture positions only; ordinary capture, gifts and production do the work. */
    UFUNCTION(BlueprintCallable, Category="Performance", meta=(DevelopmentOnly)) FString PrepareHotspotTerritories(int32 NeutralTerritories);
    UFUNCTION(BlueprintCallable, Category="Performance", meta=(DevelopmentOnly)) bool StartHotspotWindow();
	void Record(FName Name, double Value, const UObject* LocalPlayer = nullptr);
    void RecordSample(FName Name,double Value);
	bool IsCapturing() const { return bCapturing; }
    bool IsPerformanceCaptureBusy() const { return bCapturing || bHotspotPending || bContinuousHotspot || ObservationDeadline > 0; }
    bool IsCaptureValid() const { return DroppedFrameSamples==0 && DroppedNetworkSamples==0 && DroppedIndividualSamples==0; }
private:
    TSharedPtr<FGuLiContinuousHotspot> ContinuousHotspot;
    bool bContinuousHotspot=false;
    void TickContinuousHotspot();
    void AppendContinuousHotspot(const TSharedRef<FJsonObject>& Root) const;
    TSharedPtr<FGuLiHotspotCapture> Hotspot;
    bool bHotspotPending=false;
    void TickHotspot(float DeltaTime);
    void AppendHotspotCapture(const TSharedRef<FJsonObject>& Root) const;
    bool ValidateHotspotBehaviorFacts();
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
    double CaptureDeadline=0;
    FDelegateHandle CaptureFrameHandle;
    double PreviousCaptureFrameSeconds=0;
    void StopFrameIntervals();
	TArray<TSharedPtr<FJsonValue>> NetworkSamples;
	TArray<TSharedPtr<FJsonValue>> ObservationSamples;
    double ObservationStarted=0,ObservationDeadline=0,ObservationRequested=0,LastObservationSample=-1;
    uint32 DroppedObservationSamples=0;
	double LastNetworkSample = -1;
	uint32 DroppedNetworkSamples = 0;
    uint64 DroppedFrameSamples = 0;
	void CaptureNetworkSample(bool ObservationOnly=false);
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
