#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GuLiPerformanceSubsystem.generated.h"

/** Opt-in counters are owned by a World; view counters include the LocalPlayer identity. */
UCLASS()
class GULISTRIKE_API UGuLiPerformanceSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	UFUNCTION(BlueprintCallable, Category="Performance") void BeginCapture();
	UFUNCTION(BlueprintCallable, Category="Performance") void EndCapture();
	UFUNCTION(BlueprintPure, Category="Performance") FString GetCaptureJson() const;
	void Record(FName Name, double Value, const UObject* LocalPlayer = nullptr);
	bool IsCapturing() const { return bCapturing; }
private:
	struct FCounter
	{
		double Total = 0, FrameTotal = 0;
		uint64 Calls = 0, Frame = 0;
		TArray<double> FrameSamples;
	};
	TMap<FString, FCounter> Counters;
	uint64 FirstFrame = 0, LastFrame = 0;
	bool bCapturing = false;
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
