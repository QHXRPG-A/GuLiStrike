#pragma once
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "Gameplay/Data/Generated/GuLiStrikeMechTableRows.h"
#include "AnimNode_GuLiGroundMechGrounding.generated.h"

/** Game-thread query results only. No UObject/World access during pose evaluation. */
USTRUCT(BlueprintType)
struct FGuLiMechFootGroundSample
{
    GENERATED_BODY()
    UPROPERTY() bool bHit=false;
    UPROPERTY() bool bCanLock=false;
    UPROPERTY() FVector Heel=FVector::ZeroVector;
    UPROPERTY() FVector Toe=FVector::ZeroVector;
    UPROPERTY() FVector Normal=FVector::UpVector;
    UPROPERTY() FTransform Surface;
    UPROPERTY() uint64 SurfaceId=0;
    UPROPERTY() uint32 SurfaceRevision=0;
};

USTRUCT(BlueprintType)
struct FGuLiMechGroundingSnapshot
{
    GENERATED_BODY()
    UPROPERTY() FGuLiStrikeMechGroundIKRow Settings;
    UPROPERTY() FGuLiMechFootGroundSample Left;
    UPROPERTY() FGuLiMechFootGroundSample Right;
    UPROPERTY() FTransform ComponentToWorld;
    UPROPERTY() FVector MoveDirection=FVector::ZeroVector;
    UPROPERTY() float FloorHeight=0.f;
    UPROPERTY() float StrideScale=1.f;
    UPROPERTY() float GroundAlpha=0.f;
    UPROPERTY() float DeltaSeconds=0.f;
    UPROPERTY() int32 Revision=0;
};

/** Stride warping + planted soles + analytic two-link legs, without changing any bone length. */
USTRUCT(BlueprintInternalUseOnly)
struct GULISTRIKE_API FAnimNode_GuLiGroundMechGrounding : public FAnimNode_SkeletalControlBase
{
    GENERATED_BODY()
    FAnimNode_GuLiGroundMechGrounding();
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Ground",meta=(PinShownByDefault)) FGuLiMechGroundingSnapshot Ground;
    virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,TArray<FBoneTransform>& Out) override;
    virtual bool IsValidToEvaluate(const USkeleton*,const FBoneContainer& Bones) override;
private:
    virtual void InitializeBoneReferences(const FBoneContainer& Bones) override;
    UPROPERTY() FBoneReference Pelvis;
    UPROPERTY() TArray<FBoneReference> LegBones;
    FVector RestAnkle[2];
    FVector LocalUp[2];
    struct FLock
    {
        bool bLocked=false;
        uint64 SurfaceId=0;
        uint32 Revision=0;
        FVector Position;
        FQuat Rotation;
    };
    FLock Locks[2];
    int32 LastRevision=INDEX_NONE;
    float PelvisOffset=0.f;
};
