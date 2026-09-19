#include "Gameplay/GroundMech/GuLiGroundMechAnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/BlendSpace.h"
#include "AnimNodes/AnimNode_BlendSpacePlayer.h"
#include "GameFramework/Pawn.h"

namespace
{
	// The Blueprint supplies the same source blendspace; the native proxy avoids its marketplace-character interface.
	struct FGroundMechAnimProxy final : FAnimInstanceProxy
	{
		using FAnimInstanceProxy::FAnimInstanceProxy;
		FAnimNode_BlendSpacePlayer_Standalone Player;
		virtual void Initialize(UAnimInstance* Instance) override
		{
			FAnimInstanceProxy::Initialize(Instance);
			Player.SetBlendSpace(CastChecked<UGuLiGroundMechAnimInstance>(Instance)->LocomotionBlendSpace);
			Player.Initialize_AnyThread(FAnimationInitializeContext(this));
		}
		virtual void PreUpdate(UAnimInstance* Instance,float DeltaSeconds) override
		{
			FAnimInstanceProxy::PreUpdate(Instance,DeltaSeconds);
			const auto& Anim=*CastChecked<UGuLiGroundMechAnimInstance>(Instance);
			if (!Anim.LocomotionBlendSpace) return; // Animation Blueprint preview before an asset is assigned.
			const UBlendSpace& BS=*Anim.LocomotionBlendSpace;
			const float Speed01=FMath::Clamp(Anim.Speed/Anim.RunSpeed,0.f,1.f);
			const float Turn01=FMath::Clamp(Anim.TurnRate/180.f,-1.f,1.f);
			Player.SetPosition(FVector(FMath::Lerp(BS.GetBlendParameter(0).Min,BS.GetBlendParameter(0).Max,Speed01),
				FMath::Lerp(BS.GetBlendParameter(1).Min,BS.GetBlendParameter(1).Max,(Turn01+1.f)*.5f),0));
		}
		virtual void CacheBones() override { Player.CacheBones_AnyThread(FAnimationCacheBonesContext(this)); }
		virtual void UpdateAnimationNode(const FAnimationUpdateContext& Context) override { Player.Update_AnyThread(Context); }
		virtual bool Evaluate(FPoseContext& Output) override { Player.Evaluate_AnyThread(Output); return true; }
	};
}

void UGuLiGroundMechAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	if (const APawn* Pawn=TryGetPawnOwner()) PreviousYaw=Pawn->GetActorRotation().Yaw;
}

void UGuLiGroundMechAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	const APawn* Pawn=TryGetPawnOwner();
	if (!Pawn || DeltaSeconds<=0.f) return;
	Speed=Pawn->GetVelocity().Size2D();
	const float Yaw=Pawn->GetActorRotation().Yaw;
	TurnRate=FMath::FInterpTo(TurnRate,FMath::FindDeltaAngleDegrees(PreviousYaw,Yaw)/DeltaSeconds,DeltaSeconds,8.f);
	PreviousYaw=Yaw;
}

FAnimInstanceProxy* UGuLiGroundMechAnimInstance::CreateAnimInstanceProxy() { return new FGroundMechAnimProxy(this); }
void UGuLiGroundMechAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) { delete Proxy; }
