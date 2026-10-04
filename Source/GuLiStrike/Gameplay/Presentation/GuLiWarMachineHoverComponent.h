#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GuLiWarMachineHoverComponent.generated.h"

class UNiagaraComponent;

struct FGuLiHoverNozzleSource
{
	uint32 UnitId = 0;
	uint8 Disc = 0;
	FVector Position = FVector::ZeroVector;
	FVector Direction = -FVector::UpVector;
	float DiscDiameter = 0;
	float HorizontalSpeed = 0;
	bool bReset = false;
};

/** One owner for the entire Mass presentation. No component or tick per soldier/nozzle. */
UCLASS()
class GULISTRIKE_API UGuLiWarMachineHoverComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UGuLiWarMachineHoverComponent();
	void BeginFrame(double Now, const FVector& Camera);
	void Submit(const FGuLiHoverNozzleSource& Source);
	void EndFrame();
	void Reset();
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	int32 GetActiveNozzles() const { return ActiveNozzles; }
	int32 GetMovingNozzles() const { return MovingNozzles; }
	int32 GetActiveSystems() const;
	static constexpr int32 NozzlesPerBatch = 1024;
	static constexpr int32 TrailSamplesPerNozzle = 10;
	// Active GPU history is measured by distance; retired slots fade for 0.30 s.
	static constexpr float TrailLifetime = 0.30f;
	static constexpr float MaxTrailLength = 1500.0f;

private:
	struct FSlot
	{
		uint64 Key = 0;
		uint32 Generation = 0;
		bool bAllocated = false, bSubmitted = false;
		double LastSeenTime = -1000;
	};
	struct FBoundsSample { FBox Bounds = FBox(ForceInit); double Time = 0; };
	struct FBatch
	{
		TWeakObjectPtr<UNiagaraComponent> Component;
		TArray<FVector> Positions, Directions, TrailPositions;
		TArray<FVector2D> Sizes, TrailSizes;
		TArray<FLinearColor> Colors, TrailColors;
		FBox Bounds = FBox(ForceInit);
		TArray<FBoundsSample> RecentBounds;
	};
	TArray<FSlot> Slots;
	TArray<FBatch> Batches;
	TArray<int32> FreeSlots;
	TMap<uint64, int32> SlotByKey;
	double FrameTime = 0;
	FVector CameraPosition = FVector::ZeroVector;
	int32 ActiveNozzles = 0, MovingNozzles = 0;
	bool bFrameEnabled = false;
	bool bReportedIncompatibleAsset = false;
	float FadeAt(const FVector& Position) const;
	int32 Acquire(uint64 Key);
};
