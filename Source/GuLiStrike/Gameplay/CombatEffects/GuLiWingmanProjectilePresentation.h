#pragma once

#include "CoreMinimal.h"
#include "Commander/Presentation/GuLiCommanderLODPolicy.h"
#include "Gameplay/CombatEffects/GuLiPresentationSlotPool.h"
#include "GuLiWingmanProjectilePresentation.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;
class UGuLiProjectileFlightPresentationProfile;

struct FGuLiWingmanFlightSlot
{
	FGuid Id;
	FGuLiPresentationSlotHandle Handle;
	FVector Head=FVector::ZeroVector;
	TArray<FVector> Trail;
	float ArcLength=0;
	double StartServerTime=0, FinishedAt=-1, LODChangedAt=-1;
	EGuLiCommanderLODLevel Level=EGuLiCommanderLODLevel::Full;
	bool bVisible=false, bSubmitted=false;
	bool bOwnsPresentation=false;
};

USTRUCT()
struct FGuLiWingmanFlightRenderBlock
{
	GENERATED_BODY()
	UPROPERTY() TObjectPtr<UNiagaraComponent> Component;
	TArray<FVector> Positions, Scales;
	TArray<FQuat> Orientations;
	TArray<FLinearColor> Colors;
	TArray<int32> UsedRows, PreviousRows;
	FBox Bounds=FBox(ForceInit);
	FGuLiPresentationUploadStamp UploadStamp;
	uint64 PayloadRevision=1;
	bool bDirty=false;
};

USTRUCT()
struct FGuLiWingmanFlightGroup
{
	GENERATED_BODY()
	UPROPERTY() TObjectPtr<UGuLiProjectileFlightPresentationProfile> Profile;
	UPROPERTY() TObjectPtr<UNiagaraSystem> System;
	UPROPERTY() TArray<FGuLiWingmanFlightRenderBlock> Blocks;
	FGuLiPresentationSlotPool Pool{32,32};
	TArray<FGuLiWingmanFlightSlot> Slots;
};

UCLASS()
class GULISTRIKE_API UGuLiWingmanProjectilePresentation final : public UObject
{
	GENERATED_BODY()
public:
	virtual UWorld* GetWorld() const override;
	void Prepare(UGuLiProjectileFlightPresentationProfile* Profile);
	void BeginFrame(double LocalNow,double ServerNow,bool bEnabled);
	/** Consumes the caller's final display position; never predicts a second time. */
	bool Submit(const FGuid& Id,const FVector& DisplayPosition,double LaunchServerTime,UGuLiProjectileFlightPresentationProfile* Profile);
	void Finish(const FGuid& Id,const FVector& DisplayPosition,bool bImmediate);
	void EndFrame();
	void Reset();
	int32 GetActiveCount() const { return ActiveCount; }
	int32 GetComponentCount() const;
	uint64 Uploads=0, OffscreenDrops=0, Reentries=0;
	static constexpr int32 SlotsPerBlock=32;
	static constexpr int32 MaximumSegments=8;
	static constexpr int32 RowsPerSlot=9;
private:
	struct FLocation { int32 Group=INDEX_NONE; FGuLiPresentationSlotHandle Handle; };
	int32 FindGroup(UGuLiProjectileFlightPresentationProfile* Profile) const;
	void AppendTrail(FGuLiWingmanFlightSlot& Slot,const FVector& Position,float Limit);
	static FVector SampleTrail(const FGuLiWingmanFlightSlot& Slot,float DistanceFromOldest);
	static FBox BoundsFor(const FGuLiWingmanFlightSlot& Slot,float W0);
	void ReleaseSlot(int32 GroupIndex,int32 SlotIndex);
	static void Release(UNiagaraComponent* Component);
	UPROPERTY(Transient) TArray<FGuLiWingmanFlightGroup> Groups;
	TMap<FGuid,FLocation> Locations;
	double LocalTime=0, ServerTime=0;
	bool bFrameEnabled=false;
	int32 ActiveCount=0;
};
