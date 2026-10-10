#pragma once

#include "CoreMinimal.h"
#include "Commander/Presentation/GuLiCommanderLODPolicy.h"
#include "Gameplay/CombatEffects/GuLiPresentationSlotPool.h"
#include "GuLiImpactBatchPresentation.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;
class UNiagaraDataChannelAsset;
class UGuLiCombatEffectCatalog;

struct FGuLiImpactIdentity
{
	FGuid Effect;
	uint32 Epoch=0, TerminalSequence=0;
	uint8 Purpose=0;
	bool operator==(const FGuLiImpactIdentity& R) const
	{ return Effect==R.Effect && Epoch==R.Epoch && TerminalSequence==R.TerminalSequence && Purpose==R.Purpose; }
	friend uint32 GetTypeHash(const FGuLiImpactIdentity& K)
	{ return HashCombineFast(HashCombineFast(GetTypeHash(K.Effect),K.Epoch),HashCombineFast(K.TerminalSequence,K.Purpose)); }
};

struct FGuLiImpactEvent
{
	FGuLiImpactIdentity Identity;
	FVector Position=FVector::ZeroVector, Scale=FVector::OneVector;
	FQuat Rotation=FQuat::Identity;
	FLinearColor Tint=FLinearColor::White;
	int32 VfxId=0, Seed=0;
	float Lifetime=3;
};

USTRUCT()
struct FGuLiImpactSlot
{
	GENERATED_BODY()
	FGuLiPresentationSlotHandle Handle;
	FGuLiImpactEvent Event;
	FBox Bounds=FBox(ForceInit);
	UPROPERTY() TObjectPtr<UNiagaraSystem> SingleSystem;
	UPROPERTY() TObjectPtr<UNiagaraSystem> BatchSystem;
	UPROPERTY() TObjectPtr<UNiagaraComponent> SingleComponent;
	uint64 AcceptedFrame=0, PublishedFrame=MAX_uint64;
	double BornAt=-1, ExpiresAt=-1, OffscreenSince=-1;
	int32 Group=INDEX_NONE, Mode=0;
	bool bKnownBounds=false;
};

USTRUCT()
struct FGuLiImpactConsumerGroup
{
	GENERATED_BODY()
	UPROPERTY() TObjectPtr<UNiagaraSystem> System;
	UPROPERTY() TObjectPtr<UNiagaraComponent> Component;
	FGuLiPresentationUploadStamp UploadStamp;
	FBox Bounds=FBox(ForceInit);
	bool bNeeded=false;
};

/** One owner per rendering World. NDC publication is independent of local-view count. */
UCLASS()
class GULISTRIKE_API UGuLiImpactBatchPresentation final : public UObject
{
	GENERATED_BODY()
public:
	virtual UWorld* GetWorld() const override;
	void Prepare(const UGuLiCombatEffectCatalog* Catalog);
	bool Enqueue(const FGuLiImpactEvent& Event);
	void BeginFrame(uint64 FrameNumber, double LocalWorldSeconds, bool bEnabled=true);
	void PublishFrame(uint64 FrameNumber);
	void Reset();
	void ResetPreparation() { Reset(); bPrepared=false; Channel=nullptr; }
	int32 GetActiveCount() const { return Pool.ActiveIndices().Num(); }
	int32 GetComponentCount() const;
	uint64 Accepted=0, Culled=0, Duplicates=0, UnknownBounds=0, Published=0, Fallbacks=0, GenerationUploads=0;
private:
	bool SpawnSingle(FGuLiImpactSlot& Slot);
	void RetireSlot(int32 Index);
	int32 FindOrAddGroup(UNiagaraSystem* System);
	static bool GetEnvelope(UNiagaraSystem* System, const FGuLiImpactEvent& Event, FBox& Bounds);
	static void Release(UNiagaraComponent* Component);
	FGuLiPresentationSlotPool Pool;
	UPROPERTY(Transient) TArray<FGuLiImpactSlot> Slots;
	UPROPERTY(Transient) TArray<FGuLiImpactConsumerGroup> Groups;
	UPROPERTY(Transient) TObjectPtr<UNiagaraDataChannelAsset> Channel;
	TMap<FGuLiImpactIdentity,double> Seen;
	TArray<TPair<FGuLiImpactIdentity,double>> SeenOrder;
	TArray<int32> Pending, Expired;
	int32 SeenHead=0;
	double WorldNow=0;
	bool bEnabled=true, bPrepared=false;
};
