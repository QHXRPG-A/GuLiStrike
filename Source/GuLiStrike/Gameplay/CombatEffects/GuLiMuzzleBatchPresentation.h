#pragma once

#include "CoreMinimal.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectTypes.h"
#include "Gameplay/CombatEffects/GuLiPresentationSlotPool.h"
#include "GuLiMuzzleBatchPresentation.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;
class UNiagaraDataChannelAsset;
class UGuLiCombatEffectCatalog;

struct FGuLiMuzzleIdentity
{
	FGuid Shot; uint32 Epoch=0;
	bool operator==(const FGuLiMuzzleIdentity& R) const { return Shot==R.Shot && Epoch==R.Epoch; }
	friend uint32 GetTypeHash(const FGuLiMuzzleIdentity& K) { return HashCombineFast(GetTypeHash(K.Shot),K.Epoch); }
};

USTRUCT()
struct FGuLiMuzzleSlot
{
	GENERATED_BODY()
	FGuLiPresentationSlotHandle Handle;
	UPROPERTY() FGuLiCombatShotCue Cue;
	UPROPERTY() TObjectPtr<UNiagaraSystem> SingleSystem;
	UPROPERTY() TObjectPtr<UNiagaraSystem> BatchSystem;
	UPROPERTY() TObjectPtr<UNiagaraComponent> SingleComponent;
	FTransform Pose=FTransform::Identity, BirthPose=FTransform::Identity;
	FVector Scale=FVector::OneVector;
	FBox Bounds=FBox(ForceInit);
	double BornAt=-1, OffscreenSince=-1;
	uint64 AcceptedFrame=0,PublishedFrame=MAX_uint64,BornFrame=MAX_uint64;
	int32 Mode=0, Group=INDEX_NONE, ReviewMode=INDEX_NONE;
	bool bAdmitted=false, bKnownBounds=false;
};

USTRUCT()
struct FGuLiMuzzleConsumerGroup
{
	GENERATED_BODY()
	UPROPERTY() TObjectPtr<UNiagaraSystem> System;
	UPROPERTY() TObjectPtr<UNiagaraComponent> Component;
	FGuLiPresentationUploadStamp LifeStamp, PoseStamp;
	FBox Bounds=FBox(ForceInit);
	bool bNeeded=false;
};

/** Cosmetic shot owner, one per client World. The resolver uses the parent's complete pose cache. */
UCLASS()
class GULISTRIKE_API UGuLiMuzzleBatchPresentation final : public UObject
{
	GENERATED_BODY()
public:
	using FPoseResolver=TFunctionRef<bool(const FGuLiCombatShotCue&,FTransform&,float&)>;
	virtual UWorld* GetWorld() const override;
	void Prepare(const UGuLiCombatEffectCatalog* Catalog);
	bool Enqueue(const FGuLiCombatShotCue& Cue,int32 ReviewMode=INDEX_NONE);
	void BeginFrame(uint64 Frame,double LocalWorldSeconds,float ServerSeconds,bool bEnabled,FPoseResolver Resolve);
	void PublishFrame(uint64 Frame);
	void Reset();
	void ResetPreparation() { Reset(); bPrepared=false; Channel=nullptr; }
	int32 GetActiveCount() const { return Pool.ActiveIndices().Num(); }
	int32 GetComponentCount() const;
#if WITH_EDITOR
	FString GetProtocolSnapshot() const;
#endif
	uint64 Accepted=0,Born=0,Duplicates=0,Expired=0,Culled=0,OffscreenRecycled=0,Fallbacks=0,Published=0;
	uint64 PoseUnresolved=0,TooOld=0,AdmissionLate=0;
	uint64 PoseQueries=0,LifeUploads=0,PoseUploads=0,UnknownBounds=0;
private:
	bool Admit(FGuLiMuzzleSlot& Slot);
	bool SpawnSingle(FGuLiMuzzleSlot& Slot);
	void Retire(int32 Index);
	int32 FindOrAddGroup(UNiagaraSystem* System);
	static void Release(UNiagaraComponent* Component);
	FGuLiPresentationSlotPool Pool;
	UPROPERTY(Transient) TArray<FGuLiMuzzleSlot> Slots;
	UPROPERTY(Transient) TArray<FGuLiMuzzleConsumerGroup> Groups;
	UPROPERTY(Transient) TObjectPtr<UNiagaraDataChannelAsset> Channel;
	UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> FullSystem;
	TMap<FGuLiMuzzleIdentity,double> Seen;
	TArray<TPair<FGuLiMuzzleIdentity,double>> SeenOrder;
	TArray<int32> Pending, Retired;
	TArray<int32> Generations;
	TArray<float> BirthTimes;
	TArray<FVector> Positions;
	TArray<FQuat> Rotations;
	int32 SeenHead=0, LastMode=-1;
	uint64 PoseRevision=0;
	double WorldNow=0;
	bool bPrepared=false,bEnabled=true;
};
