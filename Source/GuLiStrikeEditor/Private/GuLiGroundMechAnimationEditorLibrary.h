#pragma once
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GuLiGroundMechAnimationEditorLibrary.generated.h"
class UAnimBlueprint;
class UAnimSequence;
class UBlendSpace;
class USkeletalMesh;
class UCurveFloat;
class UBlueprint;

/** Scoped authoring and numerical inspection for the Ground player's imported animation assets. */
UCLASS()
class UGuLiGroundMechAnimationEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
    /** Read the already-open Blueprint's preview view. No open/focus, render, save or pose evaluation. */
    UFUNCTION(BlueprintCallable,Category="GuLiStrike|GroundMech|Editor")
    static FString ReadBlueprintPreviewCamera(UBlueprint* Blueprint);
    /** Numerical debugging of a live completed pose. Does not advance simulation or modify assets. */
    UFUNCTION(BlueprintCallable,Category="GuLiStrike|GroundMech|Editor")
    static FString ReadGroundStructure(class USkeletalMeshComponent* Mesh,bool bCapture);
	UFUNCTION(BlueprintCallable,Category="GuLiStrike|GroundMech|Editor")
	static FString ConfigureMobilityAssets(bool bApply);
	UFUNCTION(BlueprintCallable,Category="GuLiStrike|GroundMech|Editor")
	static FString BakeGroundContacts(USkeletalMesh* Mesh,const FString& AnimationFolder,float ContactHeightTolerance,float ContactBlendHeight,const FString& GaitFilter = TEXT(""));
	UFUNCTION(BlueprintCallable,Category="GuLiStrike|GroundMech|Editor")
	static bool RebuildBlendSpace(UBlendSpace* Target,UBlendSpace* Source);
	UFUNCTION(BlueprintCallable,Category="GuLiStrike|GroundMech|Editor")
	static FString ProbeBlendSpace(USkeletalMesh* Mesh,UBlendSpace* BlendSpace,FVector Input);
	UFUNCTION(BlueprintCallable,Category="GuLiStrike|GroundMech|Editor")
	static FString BuildAnimationGraph(UAnimBlueprint* Blueprint,const FString& AnimationFolder);
	/** Upgrade only the three recoil bindings; preserve all existing pose nodes and links. */
	UFUNCTION(BlueprintCallable,Category="GuLiStrike|GroundMech|Editor")
	static FString BindArmRecoil(UAnimBlueprint* Blueprint);
	/** Add only the upper-body recoil input, preserving the existing animation graph. */
	UFUNCTION(BlueprintCallable,Category="GuLiStrike|GroundMech|Editor")
	static FString BindUpperBodyRecoil(UAnimBlueprint* Blueprint);
	/** Bind only the stabilized turret frame; preserve the production graph and existing links. */
	UFUNCTION(BlueprintCallable,Category="GuLiStrike|GroundMech|Editor")
	static FString BindUpperBodyAim(UAnimBlueprint* Blueprint);
	/** Attach the editable one-shot after aiming and IK, preserving the rest of the production graph. */
	UFUNCTION(BlueprintCallable,Category="GuLiStrike|GroundMech|Editor")
	static FString BindBlinkAnimation(UAnimBlueprint* Blueprint,UBlendSpace* Blend);
	/** Author only directional samples/settings; never modify the source animation keys. */
	UFUNCTION(BlueprintCallable,Category="GuLiStrike|GroundMech|Editor")
	static bool ConfigureBlinkBlend(UBlendSpace* Blend,UAnimSequence* Forward,UAnimSequence* Backward,UAnimSequence* Left,UAnimSequence* Right);
	/** Finish initial animation authoring inside the controller bracket, including platform compression sampling. */
	UFUNCTION(BlueprintCallable,Category="GuLiStrike|GroundMech|Editor")
	static bool NotifyAnimationKeysPopulated(UAnimSequence* Sequence);
	UFUNCTION(BlueprintCallable,Category="GuLiStrike|GroundMech|Editor")
	static FString ProbeAnimationGraph(UClass* PawnClass,FVector Velocity,bool bJumpScenario,int32 ShotIndex,FVector AimPointComponent,UCurveFloat* RecoilCurve,bool bTakeoffThrust=true);
};
