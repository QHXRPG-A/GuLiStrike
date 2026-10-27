#include "GuLiGroundMechAnimationEditorLibrary.h"
#include "AnimGraphNode_GuLiGroundMechAim.h"
#include "AnimGraphNode_GuLiGroundMechGrounding.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_ApplyAdditive.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_SequenceEvaluator.h"
#include "AnimGraphNode_BlendListByBool.h"
#include "AnimGraphNode_LayeredBoneBlend.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AnimGraphNode_Root.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Animation/BlendSpace.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Editor.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KismetCompiler.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Gameplay/GroundMech/Data/GuLiGroundMechConfiguration.h"
#include "Gameplay/GroundMech/Animation/GuLiGroundMechAnimInstance.h"
#include "Gameplay/GroundMech/Weapons/GuLiGroundMechWeaponComponent.h"
#include "Gameplay/GroundMech/Abilities/RocketJump/GuLiGroundMechRocketComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"

namespace
{
FString Json(const TSharedRef<FJsonObject>& Object)
{
	FString Text; FJsonSerializer::Serialize(Object,TJsonWriterFactory<>::Create(&Text)); return Text;
}
template<class T> T* Add(UEdGraph* Graph,int32 X,int32 Y)
{
	FGraphNodeCreator<T> Creator(*Graph); T* Node=Creator.CreateNode();
	Node->NodePosX=X; Node->NodePosY=Y; Creator.Finalize(); return Node;
}
bool Link(UEdGraphNode* From,const TCHAR* Out,UEdGraphNode* To,const TCHAR* In)
{
	auto* A=From->FindPin(Out,EGPD_Output); auto* B=To->FindPin(In,EGPD_Input);
	return A && B && From->GetGraph()->GetSchema()->TryCreateConnection(A,B);
}
bool Bind(UEdGraph* Graph,UEdGraphNode* Node,const TCHAR* Pin,const TCHAR* Variable)
{
	FGraphNodeCreator<UK2Node_VariableGet> Creator(*Graph); auto* Get=Creator.CreateNode();
	Get->VariableReference.SetSelfMember(Variable); Get->NodePosX=Node->NodePosX-220; Get->NodePosY=Node->NodePosY+160;
	Creator.Finalize(); return Link(Get,Variable,Node,Pin);
}
}
bool UGuLiGroundMechAnimationEditorLibrary::RebuildBlendSpace(UBlendSpace* Target,UBlendSpace* Source)
{
	if (!Target || !Source || Target==Source) return false;
	Target->Modify();
	// Match the source's interpolation contract without copying its skeleton or sample references.
	for (const FName Name : {FName(TEXT("bInterpolateUsingGrid")),FName(TEXT("bAllowMarkerBasedSync")),FName(TEXT("bAllowMeshSpaceBlending"))})
		if (FProperty* P=FindFProperty<FProperty>(UBlendSpace::StaticClass(),Name)) P->CopyCompleteValue_InContainer(Target,Source);
	Target->ResampleData(); Target->PostEditChange(); Target->MarkPackageDirty(); return true;
}
FString UGuLiGroundMechAnimationEditorLibrary::ReadGroundStructure(USkeletalMeshComponent* Mesh,bool bCapture)
{
    auto R=MakeShared<FJsonObject>(); R->SetBoolField(TEXT("success"),false);
    if (!Mesh || !Mesh->GetAnimInstance()) return Json(R);
    Mesh->HandleExistingParallelEvaluationTask(true,true);
    auto* Anim=Mesh->GetAnimInstance();
    for (TFieldIterator<FStructProperty> It(Anim->GetClass());It;++It)
    {
        if (It->Struct!=FAnimNode_GuLiGroundMechGrounding::StaticStruct()) continue;
        auto* Node=It->ContainerPtrToValuePtr<FAnimNode_GuLiGroundMechGrounding>(Anim);
        Node->bCaptureStructure=bCapture;
        const auto& F=Node->StructureFrame;
        auto V=[](const FVector& X) { TArray<TSharedPtr<FJsonValue>> A; for(int I=0;I<3;++I) A.Add(MakeShared<FJsonValueNumber>(X[I])); return A; };
        auto Pose=[&](const FTransform* P)
        {
            TArray<TSharedPtr<FJsonValue>> A;
            for (int I=0;I<4;++I)
            {
                auto O=MakeShared<FJsonObject>(); O->SetArrayField(TEXT("position"),V(P[I].GetLocation()));
                TArray<TSharedPtr<FJsonValue>> Q; const FQuat Rotation=P[I].GetRotation();
                for (double X:{Rotation.X,Rotation.Y,Rotation.Z,Rotation.W}) Q.Add(MakeShared<FJsonValueNumber>(X));
                O->SetArrayField(TEXT("rotation"),Q); A.Add(MakeShared<FJsonValueObject>(O));
            }
            return A;
        };
        R->SetNumberField(TEXT("serial"),F.Serial); R->SetNumberField(TEXT("dt"),F.Ground.DeltaSeconds);
        R->SetNumberField(TEXT("alpha"),F.Ground.GroundAlpha); R->SetNumberField(TEXT("stride"),F.Ground.StrideScale);
        R->SetNumberField(TEXT("pelvis"),F.Pelvis); R->SetNumberField(TEXT("pelvis_target"),F.DesiredPelvis);
        R->SetNumberField(TEXT("pelvis_low"),F.Low); R->SetNumberField(TEXT("pelvis_high"),F.High);
        R->SetNumberField(TEXT("floor"),F.Ground.FloorHeight);
        R->SetArrayField(TEXT("world_position"),V(F.Ground.ComponentToWorld.GetLocation()));
        R->SetNumberField(TEXT("world_yaw"),F.Ground.ComponentToWorld.Rotator().Yaw);
        TArray<TSharedPtr<FJsonValue>> Sides;
        for (int I=0;I<2;++I)
        {
            auto O=MakeShared<FJsonObject>(); const auto& S=I?F.Ground.Right:F.Ground.Left;
            O->SetArrayField(TEXT("input"),Pose(F.Input[I])); O->SetArrayField(TEXT("output"),Pose(F.Result[I]));
            O->SetArrayField(TEXT("target"),V(F.Targets[I])); O->SetNumberField(TEXT("plant"),F.Plant[I]);
            O->SetNumberField(TEXT("contact"),F.Contact[I]); O->SetBoolField(TEXT("locked_before"),F.LockedBefore[I]);
            O->SetBoolField(TEXT("locked_after"),F.LockedAfter[I]); O->SetBoolField(TEXT("can_lock"),S.bCanLock);
            O->SetStringField(TEXT("surface"),LexToString(S.SurfaceId));
            O->SetArrayField(TEXT("heel_plane"),V(S.Heel)); O->SetArrayField(TEXT("toe_plane"),V(S.Toe));
            Sides.Add(MakeShared<FJsonValueObject>(O));
        }
        R->SetArrayField(TEXT("sides"),Sides); R->SetBoolField(TEXT("success"),true); break;
    }
    return Json(R);
}
FString UGuLiGroundMechAnimationEditorLibrary::ProbeBlendSpace(USkeletalMesh* Mesh,UBlendSpace* BlendSpace,FVector Input)
{
	auto R=MakeShared<FJsonObject>(); R->SetBoolField(TEXT("success"),false);
	if (!Mesh || !BlendSpace || !GEditor || GEditor->PlayWorld) return Json(R);
	TStrongObjectPtr<USkeletalMeshComponent> C(NewObject<USkeletalMeshComponent>(GetTransientPackage()));
	C->SetSkeletalMeshAsset(Mesh); C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	C->SetComponentTickEnabled(false); C->RegisterComponentWithWorld(GEditor->GetEditorWorldContext().World());
	C->PlayAnimation(BlendSpace,true);
	auto* A=C->GetSingleNodeInstance();
	if (!A) { C->DestroyComponent(); return Json(R); }
	A->SetBlendSpacePosition(Input);
	TArray<TSharedPtr<FJsonValue>> Frames,Weights;
	TArray<FBlendSampleData> Samples; int32 Triangle=INDEX_NONE;
	BlendSpace->GetSamplesFromBlendInput(Input,Samples,Triangle,true);
	for (const auto& Sample:Samples)
	{
		auto W=MakeShared<FJsonObject>(); W->SetNumberField(TEXT("sample"),Sample.SampleDataIndex);
		W->SetNumberField(TEXT("weight"),Sample.GetClampedWeight());
		W->SetStringField(TEXT("animation"),Sample.Animation?Sample.Animation->GetPathName():TEXT(""));
		Weights.Add(MakeShared<FJsonValueObject>(W));
	}
	TArray<FQuat> First; double MaxAngle=0;
	const FName Names[]={TEXT("Foot_R"),TEXT("Foot_L"),TEXT("Leg_R"),TEXT("Leg_L")};
	for (int32 Frame=0;Frame<45;++Frame)
	{
		C->TickAnimation(1.f/30.f,false); C->RefreshBoneTransforms();
		const auto& Local=C->GetBoneSpaceTransforms();
		auto F=MakeShared<FJsonObject>(); F->SetNumberField(TEXT("frame"),Frame); F->SetNumberField(TEXT("player_time"),A->GetCurrentTime());
		for (int32 I=0;I<4;++I)
		{
			const int32 Index=C->GetBoneIndex(Names[I]); if (!Local.IsValidIndex(Index)) continue;
			const FQuat Q=Local[Index].GetRotation(); if (Frame==0) First.Add(Q);
			if (First.IsValidIndex(I)) MaxAngle=FMath::Max(MaxAngle,FMath::RadiansToDegrees(First[I].AngularDistance(Q)));
			F->SetStringField(Names[I].ToString(),Q.ToString());
		}
		if (Frame==0 || Frame==4 || Frame==9 || Frame==19 || Frame==44) Frames.Add(MakeShared<FJsonValueObject>(F));
	}
	C->DestroyComponent();
	R->SetStringField(TEXT("blendspace"),BlendSpace->GetPathName()); R->SetStringField(TEXT("input"),Input.ToString());
	R->SetArrayField(TEXT("weights"),Weights); R->SetArrayField(TEXT("frames"),Frames);
	R->SetNumberField(TEXT("max_leg_rotation_degrees"),MaxAngle); R->SetBoolField(TEXT("success"),true); return Json(R);
}
FString UGuLiGroundMechAnimationEditorLibrary::BuildAnimationGraph(UAnimBlueprint* BP,const FString& Folder)
{
	auto R=MakeShared<FJsonObject>(); R->SetBoolField(TEXT("success"),false);
	if (!BP || !GEditor || GEditor->PlayWorld) return Json(R);
	UEdGraph* Graph=nullptr;
	for (UEdGraph* G:BP->FunctionGraphs) if (G->GetFName()==TEXT("AnimGraph")) Graph=G;
	if (!Graph) return Json(R);
	BP->Modify(); Graph->Modify();
	UAnimGraphNode_Root* Root=nullptr;
	const auto OldNodes=Graph->Nodes;
	for (UEdGraphNode* N:OldNodes)
		if (auto* Found=Cast<UAnimGraphNode_Root>(N)) Root=Found;
		else FBlueprintEditorUtils::RemoveNode(BP,N,true);
	if (!Root) Root=Add<UAnimGraphNode_Root>(Graph,2000,0);
	Root->BreakAllNodeLinks(); Root->NodePosX=2000; Root->NodePosY=0;
	// The native snapshot is the sole input source. Retire the copied vendor input event graph.
	for (UEdGraph* G:BP->UbergraphPages)
	{
		const auto Nodes=G->Nodes; for (UEdGraphNode* N:Nodes) FBlueprintEditorUtils::RemoveNode(BP,N,true);
	}
	bool OK=true;
	auto Blend=[&](const TCHAR* Name,int32 Y)
	{
		auto* N=Add<UAnimGraphNode_BlendSpacePlayer>(Graph,0,Y);
		N->Node.SetBlendSpace(LoadObject<UBlendSpace>(nullptr,*(Folder/TEXT("BlendSpace_Loco_")+Name)));
		N->Node.SetPlayRate(1.f); N->Node.SetLoop(true); N->ReconstructNode();
		N->Node.SetGroupName(TEXT("GroundGait")); N->Node.SetGroupRole(EAnimGroupRole::CanBeLeader);
		N->Node.SetGroupMethod(EAnimSyncMethod::SyncGroup); N->Node.SetResetPlayTimeWhenBlendSpaceChanges(false);
		OK &= N->Node.GetBlendSpace()!=nullptr;
		OK &= Bind(Graph,N,TEXT("X"),TEXT("IntegratedStrafeBlend"));
		OK &= Bind(Graph,N,TEXT("Y"),TEXT("IntegratedForwardBlend"));
		for (auto& Pin:N->ShowPinForProperties) if (Pin.PropertyName==TEXT("BlendSpace") || Pin.PropertyName==TEXT("PlayRate")) Pin.bShowPin=true;
		N->ReconstructNode();
		OK &= Bind(Graph,N,TEXT("PlayRate"),FString(Name)==TEXT("Walk")?TEXT("WalkPlayRate"):TEXT("RunPlayRate"));
		OK &= Bind(Graph,N,TEXT("BlendSpace"),FString(Name)==TEXT("Walk")?TEXT("WalkBlendSpace"):TEXT("RunBlendSpace"));
		return N;
	};
	auto* Walk=Blend(TEXT("Walk"),0); auto* Run=Blend(TEXT("Run"),260);
	auto Bool=[&](UEdGraphNode* Yes,UEdGraphNode* No,const TCHAR* Variable,int32 X,int32 Y)
	{
		auto* N=Add<UAnimGraphNode_BlendListByBool>(Graph,X,Y);
		N->ReconstructNode();
		OK &= Link(Yes,TEXT("Pose"),N,TEXT("BlendPose_0"));
		OK &= Link(No,TEXT("Pose"),N,TEXT("BlendPose_1"));
		OK &= Bind(Graph,N,TEXT("bActiveValue"),Variable);
		OK &= Bind(Graph,N,TEXT("BlendTime_0"),TEXT("GaitTransitionSeconds"));
		OK &= Bind(Graph,N,TEXT("BlendTime_1"),TEXT("GaitTransitionSeconds")); return N;
	};
	auto* Ground=Bool(Run,Walk,TEXT("bIntegratedRunning"),350,0);
	if (auto* P=FindFProperty<FEnumProperty>(FAnimNode_BlendListBase::StaticStruct(),TEXT("ChildUpateMode")))
		P->GetUnderlyingProperty()->SetIntPropertyValue(P->ContainerPtrToValuePtr<void>(&Ground->Node),int64(EBlendListChildUpdateMode::AlwaysTickChildren));
	auto* TurnSlot=Add<UAnimGraphNode_Slot>(Graph,520,0);
	TurnSlot->Node.SlotName=TEXT("DefaultSlot"); TurnSlot->Node.bAlwaysUpdateSourcePose=true; TurnSlot->ReconstructNode();
	UEdGraphNode* MovingPose=Ground;
	for (int32 Side=0;Side<2;++Side)
	{
		auto* Clip=Add<UAnimGraphNode_SequencePlayer>(Graph,350+Side*300,-550);
		auto* Sequence=LoadObject<UAnimSequence>(nullptr,*(Folder/(Side?TEXT("Anim_Mecha_MoveTurn_R"):TEXT("Anim_Mecha_MoveTurn_L"))));
		OK &= Sequence!=nullptr;
		Clip->Node.SetSequence(Sequence);
		Clip->Node.SetLoopAnimation(true); Clip->Node.SetGroupName(TEXT("GroundGait"));
		Clip->Node.SetGroupRole(EAnimGroupRole::AlwaysFollower); Clip->Node.SetGroupMethod(EAnimSyncMethod::SyncGroup);
		for (auto& Pin:Clip->ShowPinForProperties) if (Pin.PropertyName==TEXT("Sequence") || Pin.PropertyName==TEXT("PlayRate")) Pin.bShowPin=true;
		// Exposing Sequence moves its default onto the pin; the runtime value is
		// supplied by the configuration binding below, not the node's literal.
		Clip->ReconstructNode();
		OK &= Bind(Graph,Clip,TEXT("Sequence"),Side?TEXT("MovingTurnRight"):TEXT("MovingTurnLeft"));
		OK &= Bind(Graph,Clip,TEXT("PlayRate"),TEXT("MovingTurnPlayRate"));
		auto* Apply=Add<UAnimGraphNode_ApplyAdditive>(Graph,450+Side*300,-250); Apply->ReconstructNode();
		OK &= Link(MovingPose,TEXT("Pose"),Apply,TEXT("Base"));
		OK &= Link(Clip,TEXT("Pose"),Apply,TEXT("Additive"));
		OK &= Bind(Graph,Apply,TEXT("Alpha"),Side?TEXT("MovingTurnRightWeight"):TEXT("MovingTurnLeftWeight"));
		MovingPose=Apply;
	}
	OK &= Link(MovingPose,TEXT("Pose"),TurnSlot,TEXT("Source"));
	auto Eval=[&](const TCHAR* Name,const TCHAR* Time,int32 X,int32 Y)
	{
		auto* N=Add<UAnimGraphNode_SequenceEvaluator>(Graph,X,Y);
		N->Node.SetSequence(LoadObject<UAnimSequence>(nullptr,*(Folder/Name)));
		OK &= N->Node.GetSequence()!=nullptr;
		N->Node.SetShouldLoop(false); N->ReconstructNode();
		for (auto& Pin:N->ShowPinForProperties) if (Pin.PropertyName==TEXT("Sequence")) Pin.bShowPin=true;
		N->ReconstructNode();
		OK &= Bind(Graph,N,TEXT("Sequence"),FString(Name).Contains(TEXT("Jump_Start"))?TEXT("JumpStartSequence"):TEXT("ShootSequence"));
		OK &= Bind(Graph,N,TEXT("ExplicitTime"),Time); return N;
	};
	auto* Start=Eval(TEXT("Anim_Mecha_Jump_Start"),TEXT("JumpStartTime"),0,520);
	auto* Loop=Add<UAnimGraphNode_SequencePlayer>(Graph,0,780);
	Loop->Node.SetSequence(LoadObject<UAnimSequence>(nullptr,*(Folder/TEXT("Anim_Mecha_Jump_Loop"))));
	Loop->Node.SetLoopAnimation(true); Loop->ReconstructNode();
	for (auto& Pin:Loop->ShowPinForProperties) if (Pin.PropertyName==TEXT("Sequence") || Pin.PropertyName==TEXT("PlayRate")) Pin.bShowPin=true;
	Loop->ReconstructNode(); OK &= Bind(Graph,Loop,TEXT("Sequence"),TEXT("JumpLoopSequence"));
	OK &= Bind(Graph,Loop,TEXT("PlayRate"),TEXT("JumpLoopPlayRate"));
	auto* Air=Bool(Start,Loop,TEXT("bJumpStarting"),350,520);
	UEdGraphNode* Pose=Bool(Air,TurnSlot,TEXT("bIsAirborne"),650,0);
	for (int32 Side=0;Side<2;++Side)
	{
		auto* Shoot=Eval(TEXT("Anim_Mecha_Shoot"),Side?TEXT("LeftShootTime"):TEXT("RightShootTime"),650+Side*350,650);
		auto* Layer=Add<UAnimGraphNode_LayeredBoneBlend>(Graph,1000+Side*350,0);
		Layer->Node.BlendPoses.SetNum(1); Layer->Node.BlendWeights={0.f}; Layer->Node.LayerSetup.SetNum(1);
		FBranchFilter Filter; Filter.BoneName=Side?TEXT("Gun_L"):TEXT("Gun_R"); Filter.BlendDepth=1;
		Layer->Node.LayerSetup[0].BranchFilters={Filter}; Layer->Node.bMeshSpaceRotationBlend=false; Layer->ReconstructNode();
		Layer->Node.CurveBlendOption=ECurveBlendOption::UseBasePose;
		OK &= Link(Pose,TEXT("Pose"),Layer,TEXT("BasePose"));
		OK &= Link(Shoot,TEXT("Pose"),Layer,TEXT("BlendPoses_0"));
		OK &= Bind(Graph,Layer,TEXT("BlendWeights_0"),Side?TEXT("LeftShootWeight"):TEXT("RightShootWeight")); Pose=Layer;
	}
	auto* ToCS=Add<UAnimGraphNode_LocalToComponentSpace>(Graph,1650,0);
	auto* IK=Add<UAnimGraphNode_GuLiGroundMechGrounding>(Graph,1750,0);
	auto* Aim=Add<UAnimGraphNode_GuLiGroundMechAim>(Graph,1850,0);
	auto* ToLocal=Add<UAnimGraphNode_ComponentToLocalSpace>(Graph,2150,0); Root->NodePosX=2400;
	OK &= Link(Pose,TEXT("Pose"),ToCS,TEXT("LocalPose"));
	OK &= Link(ToCS,TEXT("ComponentPose"),IK,TEXT("ComponentPose"));
	OK &= Bind(Graph,IK,TEXT("Ground"),TEXT("Grounding"));
	OK &= Link(IK,TEXT("Pose"),Aim,TEXT("ComponentPose"));
	OK &= Link(Aim,TEXT("Pose"),ToLocal,TEXT("ComponentPose"));
	OK &= Link(ToLocal,TEXT("Pose"),Root,TEXT("Result"));
	for (const auto& Pair:TArray<TPair<FName,FName>>{
		{TEXT("AimPointComponent"),TEXT("IntegratedAimPointComponent")},{TEXT("bAimValid"),TEXT("bIntegratedAimValid")},
		{TEXT("bForwardAim"),TEXT("bForwardAim")},{TEXT("ReturnAlpha"),TEXT("ReturnAlpha")},
		{TEXT("RightReturnStart"),TEXT("RightReturnStart")},{TEXT("LeftReturnStart"),TEXT("LeftReturnStart")},
		{TEXT("RightMuzzleLocal"),TEXT("RightMuzzleLocal")},{TEXT("LeftMuzzleLocal"),TEXT("LeftMuzzleLocal")},
		{TEXT("RightRecoil"),TEXT("IntegratedFireBlend")},{TEXT("LeftRecoil"),TEXT("IntegratedFireBlendLeft")},
		{TEXT("RecoilDistance"),TEXT("RecoilDistance")},{TEXT("GunYawLimit"),TEXT("GunYawLimit")},
		{TEXT("RightArmRecoil"),TEXT("RightArmRecoil")},{TEXT("LeftArmRecoil"),TEXT("LeftArmRecoil")},
		{TEXT("ArmRecoilDegrees"),TEXT("ArmRecoilDegrees")},
		{TEXT("BlinkPose"),TEXT("BlinkPose")},
		{TEXT("GunPitchUpLimit"),TEXT("GunPitchUpLimit")},{TEXT("GunPitchDownLimit"),TEXT("GunPitchDownLimit")}})
		OK &= Bind(Graph,Aim,*Pair.Key.ToString(),*Pair.Value.ToString());
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	FCompilerResultsLog Log; FKismetEditorUtilities::CompileBlueprint(BP,EBlueprintCompileOptions::None,&Log);
	R->SetBoolField(TEXT("connections_ok"),OK); R->SetNumberField(TEXT("compile_errors"),Log.NumErrors);
	R->SetNumberField(TEXT("compile_warnings"),Log.NumWarnings); R->SetNumberField(TEXT("nodes"),Graph->Nodes.Num());
	R->SetBoolField(TEXT("success"),OK && Log.NumErrors==0); return Json(R);
}

FString UGuLiGroundMechAnimationEditorLibrary::BindArmRecoil(UAnimBlueprint* BP)
{
	auto R=MakeShared<FJsonObject>(); R->SetBoolField(TEXT("success"),false);
	if (!BP || !GEditor || GEditor->PlayWorld) return Json(R);
	UEdGraph* Graph=nullptr; UAnimGraphNode_GuLiGroundMechAim* Aim=nullptr;
	for (UEdGraph* G:BP->FunctionGraphs) if (G->GetFName()==TEXT("AnimGraph")) Graph=G;
	if (!Graph) return Json(R);
	for (UEdGraphNode* N:Graph->Nodes) if (auto* A=Cast<UAnimGraphNode_GuLiGroundMechAim>(N))
	{ if (Aim) return Json(R); Aim=A; }
	if (!Aim) return Json(R);
	BP->Modify(); Graph->Modify(); Aim->Modify();
	R->SetNumberField(TEXT("nodes_before"),Graph->Nodes.Num());
	Aim->ReconstructNode(); bool OK=true;
	for (const TCHAR* Name:{TEXT("RightArmRecoil"),TEXT("LeftArmRecoil"),TEXT("ArmRecoilDegrees"),TEXT("BlinkPose")})
	{
		auto* Pin=Aim->FindPin(Name,EGPD_Input);
		if (!Pin) { OK=false; continue; }
		if (Pin->LinkedTo.IsEmpty()) OK &= Bind(Graph,Aim,Name,Name);
		else
		{
			const auto* Get=Cast<UK2Node_VariableGet>(Pin->LinkedTo[0]->GetOwningNode());
			OK &= Pin->LinkedTo.Num()==1 && Get && Get->VariableReference.GetMemberName()==FName(Name);
		}
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	FCompilerResultsLog Log; FKismetEditorUtilities::CompileBlueprint(BP,EBlueprintCompileOptions::None,&Log);
	R->SetNumberField(TEXT("nodes_after"),Graph->Nodes.Num());
	R->SetNumberField(TEXT("compile_errors"),Log.NumErrors); R->SetNumberField(TEXT("compile_warnings"),Log.NumWarnings);
	R->SetBoolField(TEXT("success"),OK && Log.NumErrors==0); return Json(R);
}

FString UGuLiGroundMechAnimationEditorLibrary::ConfigureMobilityAssets(bool bApply)
{
	auto R=MakeShared<FJsonObject>(); FGuLiGroundMechConfiguration C; FString Error;
	if (!C.Load(Error) || !GEditor || GEditor->PlayWorld)
	{ R->SetBoolField(TEXT("success"),false); R->SetStringField(TEXT("error"),Error); return Json(R); }
	bool OK=true; TArray<TSharedPtr<FJsonValue>> Samples,Curves;
	for (bool Running:{false,true})
	{
		auto* BS=C.Asset<UBlendSpace>(Running?TEXT("RunBlend"):TEXT("WalkBlend"));
		const TPair<const TCHAR*,FVector> Wanted[]={
			{TEXT("Idle"),FVector::ZeroVector},{TEXT("StrafeLeft"),FVector(-1,0,0)},{TEXT("StrafeRight"),FVector(1,0,0)},
			{Running?TEXT("RunForward"):TEXT("WalkForward"),FVector(0,1,0)},
			{Running?TEXT("RunBackward"):TEXT("WalkBackward"),FVector(0,-1,0)}};
		if (bApply)
		{
			BS->Modify(); while (BS->GetNumberOfBlendSamples()) BS->DeleteSample(BS->GetNumberOfBlendSamples()-1);
			for (const auto& W:Wanted) BS->AddSample(C.Asset<UAnimSequence>(W.Key),W.Value);
			if (auto* P=FindFProperty<FBoolProperty>(UBlendSpace::StaticClass(),TEXT("bAllowMarkerBasedSync"))) P->SetPropertyValue_InContainer(BS,true);
			BS->ResampleData(); BS->PostEditChange(); BS->MarkPackageDirty();
		}
		OK &= BS->GetNumberOfBlendSamples()==5;
		for (const auto& W:Wanted)
		{
			const auto* Sample=BS->GetBlendSamples().FindByPredicate([&](const FBlendSample& S){ return S.SampleValue.Equals(W.Value,.001); });
			const bool Match=Sample && Sample->Animation==C.Asset<UAnimSequence>(W.Key); OK &= Match;
			auto Item=MakeShared<FJsonObject>(); Item->SetStringField(TEXT("blend"),BS->GetPathName()); Item->SetStringField(TEXT("row"),W.Key);
			Item->SetStringField(TEXT("asset"),C.Gait(W.Key).Asset.ToString()); Item->SetBoolField(TEXT("matches"),Match); Samples.Add(MakeShared<FJsonValueObject>(Item));
		}
	}
	for (const auto& Pair:C.Gaits)
	{
		auto* Seq=C.Asset<UAnimSequence>(Pair.Key);
		if (auto* Montage=C.Asset<UAnimMontage>(Pair.Key)) Seq=Cast<UAnimSequence>(Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference());
		if (!Seq || Pair.Key==TEXT("Shoot") || Pair.Key==TEXT("JumpStart") || Pair.Key==TEXT("JumpLoop")) continue;
		auto Item=MakeShared<FJsonObject>(); Item->SetStringField(TEXT("row"),Pair.Key.ToString());
		Item->SetNumberField(TEXT("reference_speed"),Pair.Value.ReferenceSpeed); Item->SetNumberField(TEXT("sync_markers"),Seq->AuthoredSyncMarkers.Num());
		for (const FName Name:{FName(TEXT("GroundPlant_L")),FName(TEXT("GroundPlant_R"))})
		{
			const auto* Curve=Seq->GetDataModel()->FindFloatCurve(FAnimationCurveIdentifier(Name,ERawCurveTrackTypes::RCT_Float));
			const int32 Keys=Curve?Curve->FloatCurve.GetNumKeys():0; OK &= Keys>1; Item->SetNumberField(Name.ToString(),Keys);
		}
		if (Pair.Key.ToString().StartsWith(TEXT("Turn")))
		{
			const float End=Seq->EvaluateCurveData(TEXT("GroundTurnProgress"),FAnimExtractContext(double(Seq->GetPlayLength())),true);
			Item->SetNumberField(TEXT("turn_progress_end"),End); OK &= FMath::IsNearlyEqual(End,1.f,.001f);
		}
		Curves.Add(MakeShared<FJsonValueObject>(Item));
	}
	R->SetStringField(TEXT("configuration_fingerprint"),C.Fingerprint); R->SetArrayField(TEXT("samples"),Samples); R->SetArrayField(TEXT("curves"),Curves);
	R->SetNumberField(TEXT("walk_speed"),C.Mobility.WalkSpeed); R->SetNumberField(TEXT("sprint_speed"),C.Mobility.SprintSpeed);
	R->SetBoolField(TEXT("success"),OK); R->SetBoolField(TEXT("asset_mutations"),bApply); return Json(R);
}

FString UGuLiGroundMechAnimationEditorLibrary::BakeGroundContacts(USkeletalMesh* Mesh,const FString& Folder,float Tolerance,float BlendHeight,const FString& GaitFilter)
{
	auto R=MakeShared<FJsonObject>(); R->SetBoolField(TEXT("success"),false);
	if (!Mesh || !GEditor || GEditor->PlayWorld || Tolerance<=0 || BlendHeight<=0 || !Mesh->GetImportedModel()
		|| Mesh->GetImportedModel()->LODModels.IsEmpty()) return Json(R);
	const auto& Ref=Mesh->GetRefSkeleton(); const auto& Locals=Ref.GetRefBonePose();
	TArray<FTransform> Global; Global.SetNum(Locals.Num());
	for (int32 I=0;I<Locals.Num();++I) Global[I]=Ref.GetParentIndex(I)>=0?Locals[I]*Global[Ref.GetParentIndex(I)]:Locals[I];
	FVector Contacts[2][2]; auto Sole=MakeShared<FJsonObject>();
	for (int32 Side=0;Side<2;++Side) for (int32 Point=0;Point<2;++Point)
	{
		const FName Bone=Side?(Point?TEXT("FootEnd_R"):TEXT("Foot_R")):(Point?TEXT("FootEnd_L"):TEXT("Foot_L"));
		const int32 Index=Ref.FindBoneIndex(Bone); if (Index<0) return Json(R);
		TArray<FVector> Vertices; float Lowest=BIG_NUMBER;
		for (const auto& Section:Mesh->GetImportedModel()->LODModels[0].Sections) for (const auto& V:Section.SoftVertices)
		{
			int32 Influence=0; for (int32 K=1;K<MAX_TOTAL_INFLUENCES;++K) if (V.InfluenceWeights[K]>V.InfluenceWeights[Influence]) Influence=K;
			if (Section.BoneMap.IsValidIndex(V.InfluenceBones[Influence]) && Section.BoneMap[V.InfluenceBones[Influence]]==Index)
			{ const FVector P(V.Position); Vertices.Add(P); Lowest=FMath::Min(Lowest,float(P.Z)); }
		}
		if (Vertices.IsEmpty()) return Json(R);
		FVector Sum=FVector::ZeroVector; int32 Count=0;
		for (const FVector& V:Vertices) if (V.Z<=Lowest+1.f) { Sum+=V; ++Count; }
		const FVector CS=Sum/Count;
		Contacts[Side][Point]=Global[Index].InverseTransformPosition(CS);
		const FString Prefix=(Point?TEXT("Toe"):TEXT("Heel"))+FString(Side?TEXT("Right"):TEXT("Left"));
		for (int32 Axis=0;Axis<3;++Axis) Sole->SetNumberField(Prefix+FString(1,&TEXT("XYZ")[Axis]),Contacts[Side][Point][Axis]);
		R->SetStringField(Prefix+TEXT("_rest_component"),CS.ToString());
	}
	R->SetObjectField(TEXT("sole"),Sole);
	auto Gaits=MakeShared<FJsonObject>(); TArray<TSharedPtr<FJsonValue>> ClipReports;
	const TPair<const TCHAR*,const TCHAR*> Clips[]={
		{TEXT("Idle"),TEXT("Anim_Mecha_Idle")},{TEXT("WalkForward"),TEXT("Anim_Mecha_Walk_FWD")},
		{TEXT("WalkBackward"),TEXT("Anim_Mecha_WalK_BWD")},{TEXT("RunForward"),TEXT("Anim_Run_FWD")},
		{TEXT("RunBackward"),TEXT("Anim_Mecha_Run_BWD")},{TEXT("StrafeLeft"),TEXT("Anim_Mecha_Strafe_L")},
		{TEXT("StrafeRight"),TEXT("Anim_Mecha_Strafe_R")},{TEXT("TurnLeft"),TEXT("Anim_Mecha_Turn_L")},
		{TEXT("TurnRight"),TEXT("Anim_Mecha_Turn_R")},{TEXT("MovingTurnLeft"),TEXT("Anim_Mecha_MoveTurn_L")},
		{TEXT("MovingTurnRight"),TEXT("Anim_Mecha_MoveTurn_R")}};
	TArray<FString> SelectedGaits; GaitFilter.ParseIntoArray(SelectedGaits,TEXT(","),true);
	for (const auto& Clip:Clips)
	{
		if (!SelectedGaits.IsEmpty() && !SelectedGaits.Contains(Clip.Key)) continue;
		auto* Sequence=LoadObject<UAnimSequence>(nullptr,*(Folder/Clip.Value)); if (!Sequence) return Json(R);
		const bool Turning=FString(Clip.Key).Contains(TEXT("Turn")), Idle=FString(Clip.Key)==TEXT("Idle");
		const bool MovingTurn=FString(Clip.Key).StartsWith(TEXT("Moving"));
		const int32 Frames=Sequence->GetNumberOfSampledKeys(); if (Frames<2) return Json(R);
		const double Step=Sequence->GetPlayLength()/(Frames-1);
		TArray<float> Heights[2]; TArray<FVector> Points[2]; TArray<float> Yaws;
		float MinHeight[2]={BIG_NUMBER,BIG_NUMBER};
		for (int32 Frame=0;Frame<Frames;++Frame)
		{
			TArray<FTransform> Pose; Pose.SetNum(Ref.GetNum());
			for (int32 I=0;I<Ref.GetNum();++I)
			{
				const int32 SkeletonIndex=Sequence->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(Ref.GetBoneName(I));
				Sequence->GetBoneTransform(Pose[I],FSkeletonPoseBoneIndex(SkeletonIndex),FAnimExtractContext(Frame*Step),true);
				if (Ref.GetParentIndex(I)>=0) Pose[I]=Pose[I]*Pose[Ref.GetParentIndex(I)];
			}
			Yaws.Add(Pose[0].Rotator().Yaw);
			for (int32 Side=0;Side<2;++Side)
			{
				const int32 FootIndex=Ref.FindBoneIndex(Side?TEXT("Foot_R"):TEXT("Foot_L"));
				FTransform Foot=Pose[FootIndex];
				// Support calibration and the runtime solver share a flat mechanical
				// toe reference; vendor toe roll is retained only during swing.
				const FTransform ToeLocal=Locals[Ref.FindBoneIndex(Side?TEXT("FootEnd_R"):TEXT("FootEnd_L"))];
				// Contact calibration uses the same leveled sole as grounding, not the ankle or the raw foot-roll excursion.
				const FVector Up=Foot.TransformVectorNoScale(Global[FootIndex].InverseTransformVectorNoScale(FVector::UpVector));
				Foot.SetRotation((FQuat::FindBetweenNormals(Up.GetSafeNormal(),FVector::UpVector)*Foot.GetRotation()).GetNormalized());
				const FTransform Toe=ToeLocal*Foot;
				const FVector H=Foot.TransformPosition(Contacts[Side][0]),T=Toe.TransformPosition(Contacts[Side][1]);
				const float Height=FMath::Max(H.Z,T.Z); Heights[Side].Add(Height); Points[Side].Add((H+T)*.5);
				MinHeight[Side]=FMath::Min(MinHeight[Side],Height);
			}
		}
		TArray<float> Speeds; auto& Controller=Sequence->GetController();
		Sequence->AuthoredSyncMarkers.RemoveAll([](const FAnimSyncMarker& M){ return M.MarkerName==TEXT("GroundLeftPlant") || M.MarkerName==TEXT("GroundRightPlant"); });
		Controller.OpenBracket(FText::FromString(TEXT("Ground sole support and root yaw calibration")),false);
		for (int32 Side=0;Side<2;++Side)
		{
			TArray<FRichCurveKey> Keys;
			for (int32 Frame=0;Frame<Frames;++Frame)
			{
				const float Plant=Idle?1.f:1.f-FMath::SmoothStep(Tolerance,Tolerance+BlendHeight,Heights[Side][Frame]-MinHeight[Side]);
				Keys.Emplace(Frame*Step,Plant); Keys.Last().InterpMode=RCIM_Linear;
				if (!Idle && !Turning && Frame>0 && Plant>.8f && Keys[Frame-1].Value>.8f)
				{
					const float Speed=FVector::Dist2D(Points[Side][Frame],Points[Side][Frame-1])/Step;
					if (Speed>1.f) Speeds.Add(Speed);
				}
			}
			FAnimationCurveIdentifier ID(Side?FName(TEXT("GroundPlant_R")):FName(TEXT("GroundPlant_L")),ERawCurveTrackTypes::RCT_Float);
			if (MovingTurn) Controller.RemoveCurve(ID,false);
			else { Controller.AddCurve(ID,0x00000004,false); Controller.SetCurveKeys(ID,Keys,false); }
			if (!Idle && (!Turning || MovingTurn)) for (int32 Frame=0;Frame<Frames-1;++Frame)
				if (Keys[Frame].Value>=.65f && Keys[Frame?Frame-1:Frames-2].Value<.65f)
				{
					auto& Marker=Sequence->AuthoredSyncMarkers.AddDefaulted_GetRef();
					Marker.MarkerName=Side?TEXT("GroundRightPlant"):TEXT("GroundLeftPlant"); Marker.Time=Frame*Step; Marker.TrackIndex=0;
				}
			if (MovingTurn)
			{
				const FName MarkerName=Side?TEXT("GroundRightPlant"):TEXT("GroundLeftPlant");
				if (!Sequence->AuthoredSyncMarkers.ContainsByPredicate([&](const FAnimSyncMarker& M){return M.MarkerName==MarkerName;}))
				{
					// A pivot foot may stay planted for the entire turn. Give that
					// cycle its opposite support phase so the additive can follow gait.
					auto& Marker=Sequence->AuthoredSyncMarkers.AddDefaulted_GetRef();
					Marker.MarkerName=MarkerName; Marker.Time=Side?Sequence->GetPlayLength()*.5f:0.f; Marker.TrackIndex=0;
				}
			}
		}
		const float TurnDegrees=FMath::FindDeltaAngleDegrees(Yaws[0],Yaws.Last());
		if (Turning && !MovingTurn && FMath::Abs(TurnDegrees)>1.f)
		{
			TArray<FRichCurveKey> Keys; float Previous=0;
			for (int32 I=0;I<Frames;++I)
			{
				Previous=FMath::Clamp(FMath::FindDeltaAngleDegrees(Yaws[0],Yaws[I])/TurnDegrees,Previous,1.f);
				Keys.Emplace(I*Step,Previous); Keys.Last().InterpMode=RCIM_Linear;
			}
			Keys.Last().Value=1.f;
			FAnimationCurveIdentifier ID(TEXT("GroundTurnProgress"),ERawCurveTrackTypes::RCT_Float);
			Controller.AddCurve(ID,0x00000004,false); Controller.SetCurveKeys(ID,Keys,false);
		}
		Controller.CloseBracket(false); Sequence->SortSyncMarkers(); Sequence->RefreshSyncMarkerDataFromAuthored(); Sequence->MarkPackageDirty();
		Speeds.Sort(); const float Reference=Speeds.IsEmpty()?1.f:Speeds[Speeds.Num()/2];
		auto Row=MakeShared<FJsonObject>(); Row->SetNumberField(TEXT("ReferenceSpeed"),Reference);
		Row->SetNumberField(TEXT("TurnDegrees"),Turning?TurnDegrees:0.f); Gaits->SetObjectField(Clip.Key,Row);
		auto Report=MakeShared<FJsonObject>(); Report->SetStringField(TEXT("asset"),Sequence->GetPathName());
		Report->SetNumberField(TEXT("frames"),Frames); Report->SetNumberField(TEXT("planted_velocity_samples"),Speeds.Num());
		Report->SetNumberField(TEXT("reference_speed_cm_s"),Reference); Report->SetNumberField(TEXT("left_min_sole_height"),MinHeight[0]);
		Report->SetNumberField(TEXT("right_min_sole_height"),MinHeight[1]); Report->SetNumberField(TEXT("turn_degrees"),TurnDegrees);
		ClipReports.Add(MakeShared<FJsonValueObject>(Report));
	}
	R->SetObjectField(TEXT("gait"),Gaits); R->SetArrayField(TEXT("clips"),ClipReports);
	R->SetBoolField(TEXT("success"),SelectedGaits.IsEmpty() || ClipReports.Num()==SelectedGaits.Num()); return Json(R);
}

FString UGuLiGroundMechAnimationEditorLibrary::ProbeAnimationGraph(UClass* PawnClass,FVector Velocity,bool bJumpScenario,int32 ShotIndex,FVector AimPointComponent,UCurveFloat* RecoilCurve,bool bTakeoffThrust)
{
	auto R=MakeShared<FJsonObject>(); R->SetBoolField(TEXT("success"),false);
	if (!PawnClass || !PawnClass->IsChildOf(AGuLiGroundMechCharacter::StaticClass()) || !GEditor || GEditor->PlayWorld) return Json(R);
	UWorld* World=GEditor->GetEditorWorldContext().World();
	FActorSpawnParameters Params; Params.ObjectFlags=RF_Transient; Params.bTemporaryEditorActor=true;
	Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Pawn=World->SpawnActor<AGuLiGroundMechCharacter>(PawnClass,FTransform::Identity,Params);
	if (!Pawn) return Json(R);
	auto* Mesh=Pawn->GetMesh(); auto* Move=Pawn->GetCharacterMovement();
	Mesh->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Mesh->SetComponentTickEnabled(false); Mesh->InitAnim(true);
	auto* Anim=Cast<UGuLiGroundMechAnimInstance>(Mesh->GetAnimInstance());
	if (!Anim) { World->DestroyActor(Pawn); return Json(R); }
	Anim->ConfigureIntegratedRecoil(RecoilCurve,.15f);
	Pawn->GetWeapon()->AimPoint=Mesh->GetComponentTransform().TransformPosition(AimPointComponent);
	auto* Thrust=FindFProperty<FBoolProperty>(UGuLiGroundMechRocketComponent::StaticClass(),TEXT("bReplicatedThrusting"));
	TArray<TSharedPtr<FJsonValue>> Frames;
	for (int32 Frame=0;Frame<45;++Frame)
	{
		const bool Air=bJumpScenario && Frame>0 && Frame<35;
		Move->SetMovementMode(Air?MOVE_Falling:MOVE_Walking); Move->Velocity=Velocity;
		if (Thrust) Thrust->SetPropertyValue_InContainer(Pawn->GetRocketJump(),Air && ((bTakeoffThrust && Frame<12) || Frame>24));
		if (Frame==4 && ShotIndex>=0) Anim->TriggerIntegratedRecoil(0.f,ShotIndex==2?0:ShotIndex);
		if (Frame==12 && ShotIndex==2) Anim->TriggerIntegratedRecoil(0.f,1);
		Mesh->TickAnimation(1.f/30.f,false); Mesh->RefreshBoneTransforms();
		auto F=MakeShared<FJsonObject>(); F->SetNumberField(TEXT("frame"),Frame);
		F->SetBoolField(TEXT("airborne"),Anim->bIsAirborne); F->SetBoolField(TEXT("jump_start"),Anim->bJumpStarting);
		F->SetNumberField(TEXT("jump_time"),Anim->JumpStartTime);
		F->SetNumberField(TEXT("forward"),Anim->IntegratedForwardBlend); F->SetNumberField(TEXT("strafe"),Anim->IntegratedStrafeBlend);
		F->SetNumberField(TEXT("right_time"),Anim->RightShootTime); F->SetNumberField(TEXT("left_time"),Anim->LeftShootTime);
		F->SetNumberField(TEXT("right_recoil"),Anim->IntegratedFireBlend); F->SetNumberField(TEXT("left_recoil"),Anim->IntegratedFireBlendLeft);
		const auto Local=Mesh->GetBoneSpaceTransforms();
		for (const FName Bone:{FName(TEXT("Foot_R")),FName(TEXT("Foot_L")),FName(TEXT("Gun_R")),FName(TEXT("Gun_L"))})
		{
			const int32 I=Mesh->GetBoneIndex(Bone);
			if (Local.IsValidIndex(I)) F->SetStringField(Bone.ToString(),Local[I].ToString());
		}
		for (const FName Socket:{FName(TEXT("Muzzle")),FName(TEXT("Muzzle_L"))})
		{
			const FTransform M=Mesh->GetSocketTransform(Socket);
			const FVector Direction=(FVector(Pawn->GetWeapon()->AimPoint)-M.GetLocation()).GetSafeNormal();
			const double Error=FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(M.GetUnitAxis(EAxis::X),Direction),-1.,1.)));
			F->SetNumberField(Socket.ToString()+TEXT("_error_degrees"),Error);
		}
		Frames.Add(MakeShared<FJsonValueObject>(F));
	}
	World->DestroyActor(Pawn);
	R->SetArrayField(TEXT("frames"),Frames); R->SetBoolField(TEXT("success"),true); return Json(R);
}
