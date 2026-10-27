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
    UPROPERTY() bool bHeelHit=false;
    UPROPERTY() bool bToeHit=false;
    UPROPERTY() FVector HeelNormal=FVector::UpVector;
    UPROPERTY() FVector ToeNormal=FVector::UpVector;
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
    UPROPERTY() float BlinkAlpha=0.f;
    UPROPERTY() float BlinkCrouchDepthCm=0.f;
    UPROPERTY() float BlinkKneeAngleDegrees=110.f;
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
#if WITH_EDITOR
    // Opt-in, single completed-pose snapshot. Read after joining mesh evaluation.
    bool bCaptureStructure=false;
    struct FStructureFrame
    {
        uint64 Serial=0;
        FGuLiMechGroundingSnapshot Ground;
        FTransform Input[2][4], Result[2][4];
        FVector Targets[2];
        float Plant[2]={}, Contact[2]={};
        bool LockedBefore[2]={}, LockedAfter[2]={};
        float Pelvis=0, DesiredPelvis=0, Low=0, High=0;
    } StructureFrame;
#endif
private:
    void EvaluateBlinkCrouch(FComponentSpacePoseContext& Output,TArray<FBoneTransform>& Out);
    virtual void InitializeBoneReferences(const FBoneContainer& Bones) override;
    UPROPERTY() FBoneReference Pelvis;
    UPROPERTY() TArray<FBoneReference> LegBones;
    FVector RestAnkle[2];
    FVector RestAxis[2], RestPole[2];
    FVector RestUpperDirection[2],RestLowerDirection[2],RestHingeNormal[2];
    FQuat RestUpperRotation[2],RestLowerRotation[2];
    float UpperLengths[2]={}, LowerLengths[2]={};
    FVector LocalUp[2];
    FTransform RestToeLocal[2];
    struct FLock
    {
        bool bLocked=false;
        bool bBlockedUntilSwing=false;
        float Weight=0.f;
        uint64 SurfaceId=0;
        uint32 Revision=0;
        FVector Position=FVector::ZeroVector;
        FQuat Rotation=FQuat::Identity;
        FQuat ToeRotation=FQuat::Identity;
        FTransform Surface=FTransform::Identity;
        FVector BodyDirection=FVector::RightVector;
    };
    FLock Locks[2];
    int32 LastRevision=INDEX_NONE;
    float PelvisOffset=0.f;
    float Contacts[2]={};
    float PoleTwists[2]={};
    FVector AppliedOffsets[2]={FVector::ZeroVector,FVector::ZeroVector};
    FQuat PreviousFootWorld[2]={FQuat::Identity,FQuat::Identity};
    bool bHasPreviousPose=false;
};
