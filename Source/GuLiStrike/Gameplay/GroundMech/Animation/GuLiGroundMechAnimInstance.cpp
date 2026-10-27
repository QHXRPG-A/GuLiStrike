#include "Gameplay/GroundMech/Animation/GuLiGroundMechAnimInstance.h"
#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Gameplay/GroundMech/Movement/GuLiGroundMechMovementComponent.h"
#include "Gameplay/GroundMech/Abilities/RocketJump/GuLiGroundMechRocketComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Gameplay/GroundMech/Movement/Collision/GuLiGroundMassContactSubsystem.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/BlendSpace.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"

void UGuLiGroundMechAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	BlinkPose=BlinkEntryDirection=FVector::ZeroVector; PreviousBlinkSerial=0; PreviousBlinkPhase=BlinkPhase=0; BlinkPhaseProgress=0.f;
	PreviousPawn.Reset();
	PreviousController.Reset();
	IntegratedRecoilAge[0] = IntegratedRecoilAge[1] = BIG_NUMBER;
	IntegratedFireBlendLeft = 0.f;
	RightArmRecoil=LeftArmRecoil=0.f; ArmRecoilStart[0]=ArmRecoilStart[1]=0.f;
	IntegratedFireBlend = IntegratedForwardBlend = IntegratedStrafeBlend = 0.f;
	IntegratedControlRotation = FRotator::ZeroRotator;
	bIntegratedRunning = false;
	bJumpStarting = false; JumpStartTime = 0.f;
	RightShootWeight = LeftShootWeight = RightShootTime = LeftShootTime = 0.f;
	if (const auto* Mesh = GetSkelMeshComponent())
	{
		if (const auto* Socket=Mesh->GetSocketByName(TEXT("Muzzle"))) RightMuzzleLocal=Socket->GetSocketLocalTransform();
		if (const auto* Socket=Mesh->GetSocketByName(TEXT("Muzzle_L"))) LeftMuzzleLocal=Socket->GetSocketLocalTransform();
	}
	++AnimationStateRevision;
	SmoothedSpeed=0; RunBlend=0; Grounding={}; TurnSerial=INDEX_NONE; ActiveTurnMontage=nullptr;
	MovingTurnLeftWeight=MovingTurnRightWeight=0.f;
}

void UGuLiGroundMechAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	auto* Pawn = Cast<AGuLiGroundMechCharacter>(TryGetPawnOwner());
	const auto* Move = Pawn ? Cast<UGuLiGroundMechMovementComponent>(Pawn->GetCharacterMovement()) : nullptr;
	if (!Pawn || !Move || !Pawn->GetMobilityConfiguration().bReady)
	{
		BlinkPose=BlinkEntryDirection=FVector::ZeroVector; BlinkPhase=0; BlinkPhaseProgress=0.f;
		Speed = TurnRate = 0.f;
		HorizontalVelocity = LocalHorizontalVelocity = FVector::ZeroVector;
		bIsGrounded = true; bIsAirborne = bIsThrusting = false;
		IntegratedFireBlend = IntegratedForwardBlend = IntegratedStrafeBlend = 0.f;
		IntegratedFireBlendLeft = 0.f;
		RightArmRecoil=LeftArmRecoil=0.f; ArmRecoilStart[0]=ArmRecoilStart[1]=0.f;
		IntegratedRecoilAge[0]=IntegratedRecoilAge[1]=BIG_NUMBER;
		IntegratedControlRotation = FRotator::ZeroRotator;
		bIntegratedRunning = false;
		bIntegratedAimValid = false;
		bForwardAim=false; ReturnAlpha=1.f; MovingTurnLeftWeight=MovingTurnRightWeight=0.f;
		Grounding.GroundAlpha=0;
		return;
	}
	const float Yaw = Pawn->GetActorRotation().Yaw;
	const auto& Blink=Pawn->GetBlinkState();
	if (Blink.Serial!=PreviousBlinkSerial && Blink.Phase==1)
		BlinkEntryDirection=FVector(-IntegratedStrafeBlend,IntegratedForwardBlend,0.f);
	const bool bReset = PreviousPawn.Get() != Pawn || PreviousController.Get() != Pawn->GetController()
		|| PreviousDisplacementRevision != Move->GetDisplacementRevision()
		|| (Blink.Phase==2 && PreviousBlinkPhase!=2);
	PreviousBlinkSerial=Blink.Serial; PreviousBlinkPhase=Blink.Phase;
	BlinkPose=FVector(Pawn->GetBlinkPoseAlpha(),Pawn->GetMobilityConfiguration().Blink.BlinkArmBackDegrees,
		Pawn->GetMobilityConfiguration().Blink.BlinkBodyLeanDegrees);
	BlinkPhase=Blink.Phase;
	const auto& BlinkConfig=Pawn->GetMobilityConfiguration().Blink;
    const float PhaseDuration=Blink.Phase==1?BlinkConfig.BlinkWindupSeconds:Blink.Phase==2?BlinkConfig.BlinkConcealSeconds:BlinkConfig.BlinkRecoverySeconds;
	BlinkPhaseProgress=Blink.Phase?FMath::Clamp(Blink.Elapsed/FMath::Max(.001f,PhaseDuration),0.f,1.f):0.f;
	// Phase 2 is a brief concealed teleport beat, with no Loop clip or sway cycle.
	if (bReset)
	{
		++AnimationStateRevision;
		TurnRate = 0.f;
		PreviousYaw = Yaw;
	}
	PreviousPawn = Pawn;
	PreviousController = Pawn->GetController();
	PreviousDisplacementRevision = Move->GetDisplacementRevision();
	HorizontalVelocity = FVector(Pawn->GetVelocity().X, Pawn->GetVelocity().Y, 0.f);
	LocalHorizontalVelocity = GetSkelMeshComponent()->GetComponentTransform().InverseTransformVectorNoScale(HorizontalVelocity);
	Speed = HorizontalVelocity.Size();
	if (DeltaSeconds > UE_SMALL_NUMBER)
		TurnRate = FMath::FInterpTo(TurnRate, FMath::FindDeltaAngleDegrees(PreviousYaw, Yaw) / DeltaSeconds, DeltaSeconds, 8.f);
	PreviousYaw = Yaw;
	const bool bWasGrounded = bIsGrounded;
	bIsGrounded = Move->IsGroundedForAnimation();
	bIsAirborne = !bIsGrounded && Move->IsFalling();
	bIsThrusting = Pawn->GetRocketJump()->IsThrusting();
	if (bReset || !bIsAirborne) { bJumpStarting=false; JumpStartTime=0.f; }
	else if (bWasGrounded && bIsThrusting) { bJumpStarting=true; JumpStartTime=0.f; }
	else if (bJumpStarting)
	{
		JumpStartTime += FMath::Max(0.f,DeltaSeconds)*Pawn->GetMobilityConfiguration().Gait(TEXT("JumpStart")).BasePlayRate;
		if (JumpStartTime >= JumpStartDuration) bJumpStarting=false;
	}
	if (Pawn->UsesIntegratedBodyMesh())
	{
		const auto& Config=Pawn->GetMobilityConfiguration();
		const auto& Walk=Config.Gait(TEXT("WalkForward")); const auto& Run=Config.Gait(TEXT("RunForward"));
		WalkBlendSpace=Config.Asset<UBlendSpace>(TEXT("WalkBlend")); RunBlendSpace=Config.Asset<UBlendSpace>(TEXT("RunBlend"));
		JumpStartSequence=Config.Asset<UAnimSequence>(TEXT("JumpStart")); JumpLoopSequence=Config.Asset<UAnimSequence>(TEXT("JumpLoop"));
		ShootSequence=Config.Asset<UAnimSequence>(TEXT("Shoot"));
		MovingTurnLeft=Config.Asset<UAnimSequence>(TEXT("MovingTurnLeft"));
		MovingTurnRight=Config.Asset<UAnimSequence>(TEXT("MovingTurnRight"));
		JumpLoopPlayRate=Config.Gait(TEXT("JumpLoop")).BasePlayRate;
		JumpStartDuration=JumpStartSequence->GetPlayLength(); ShootDuration=ShootSequence->GetPlayLength();
		RecoilDistance=Config.Gait(TEXT("Shoot")).RecoilCentimeters;
		ArmRecoilDegrees=Config.Gait(TEXT("Shoot")).ArmRecoilDegrees;
		GunYawLimit=Config.Mobility.GunYawLimit; GunPitchUpLimit=Config.Mobility.GunPitchUpLimit; GunPitchDownLimit=Config.Mobility.GunPitchDownLimit;
		GaitTransitionSeconds=Walk.TransitionSeconds;
		bIntegratedRunning = Speed > Pawn->GetWalkSpeed()+UE_KINDA_SMALL_NUMBER;
		if (bReset) { SmoothedSpeed=Speed; Grounding.StrideScale=1.f; RunBlend=bIntegratedRunning?1.f:0.f; }
		SmoothedSpeed=FMath::FInterpTo(SmoothedSpeed,Speed,DeltaSeconds,1.f/Walk.SpeedSmoothingSeconds);
		RunBlend=FMath::FInterpConstantTo(RunBlend,bIntegratedRunning?1.f:0.f,DeltaSeconds,1.f/GaitTransitionSeconds);
		const FVector Direction=Blink.Phase==1?BlinkEntryDirection*(1.f-BlinkPose.X):
			Speed>Config.Mobility.TurnMovementThreshold?LocalHorizontalVelocity.GetSafeNormal():FVector::ZeroVector;
		IntegratedForwardBlend=Direction.Y; IntegratedStrafeBlend=-Direction.X;
		float SafeStrideMax=MAX_flt;
		auto Reference=[&](UBlendSpace* Blend,float Contribution)
		{
			TArray<FBlendSampleData> Samples; int32 Triangle=INDEX_NONE;
			Blend->GetSamplesFromBlendInput(FVector(IntegratedStrafeBlend,IntegratedForwardBlend,0),Samples,Triangle,true);
			FVector Travel=FVector::ZeroVector; float CycleLength=0.f;
			for (const auto& Sample:Samples) for (const auto& G:Config.Gaits)
				if (Config.Asset<UAnimSequence>(G.Key)==Sample.Animation && Sample.Animation)
				{
					// Inactive walk/run clips cannot constrain the visible gait.
					if (Contribution*Sample.GetClampedWeight()>UE_KINDA_SMALL_NUMBER)
						SafeStrideMax=FMath::Min(SafeStrideMax,FMath::Min(G.Value.MaxStrideScale,G.Value.SafeMaxStrideScale));
					FVector Axis=FVector::ZeroVector; const FString Name=G.Key.ToString();
					if (Name.EndsWith(TEXT("Forward"))) Axis.Y=1; else if (Name.EndsWith(TEXT("Backward"))) Axis.Y=-1;
					else if (Name==TEXT("StrafeLeft")) Axis.X=1; else if (Name==TEXT("StrafeRight")) Axis.X=-1;
					const float WeightedCycle=Sample.GetClampedWeight()*Sample.Animation->GetPlayLength();
					Travel+=Axis*G.Value.ReferenceSpeed*WeightedCycle; CycleLength+=WeightedCycle;
				}
			// BlendSpace synchronizes normalized time, so blend distance per cycle, not unrelated clip rates.
			return FMath::Max(1.f,float(FVector::DotProduct(Travel,Direction))/FMath::Max(.001f,CycleLength));
		};
		const float WalkReference=Reference(WalkBlendSpace,1.f-RunBlend), RunReference=Reference(RunBlendSpace,RunBlend);
		const float ReferenceSpeed=FMath::Lerp(WalkReference,RunReference,RunBlend);
		const auto& Current=bIntegratedRunning?Run:Walk;
		const float Stride=FMath::Clamp(FMath::Sqrt(SmoothedSpeed/ReferenceSpeed),Current.MinStrideScale,FMath::Min(Current.MaxStrideScale,SafeStrideMax));
		Grounding.StrideScale=FMath::FInterpTo(Grounding.StrideScale,Direction.IsNearlyZero()?1.f:Stride,DeltaSeconds,1.f/Current.SpeedSmoothingSeconds);
		WalkPlayRate=Direction.IsNearlyZero()?Config.Gait(TEXT("Idle")).BasePlayRate:FMath::Clamp(SmoothedSpeed/(Grounding.StrideScale*WalkReference),Walk.MinPlayRate,Walk.MaxPlayRate);
		RunPlayRate=Direction.IsNearlyZero()?Config.Gait(TEXT("Idle")).BasePlayRate:FMath::Clamp(SmoothedSpeed/(Grounding.StrideScale*RunReference),Run.MinPlayRate,Run.MaxPlayRate);
		const auto& Turn=Pawn->GetTurnState();
		const bool bMovingTurn=Pawn->HasMovementIntent() && Speed>Config.Mobility.TurnMovementThreshold && bIsGrounded && !bIsThrusting;
		const auto& TurnGait=Config.Gait(TurnRate<0?TEXT("TurnLeft"):TEXT("TurnRight"));
		const auto* TurnAsset=Config.Asset<UAnimMontage>(TurnRate<0?TEXT("TurnLeft"):TEXT("TurnRight"));
		const float ReferenceTurnRate=FMath::Abs(TurnGait.TurnDegrees)*TurnGait.BasePlayRate/TurnAsset->GetPlayLength();
		const float TurnWeight=bMovingTurn?TurnGait.MovingTurnMaxWeight*FMath::Clamp(FMath::Abs(TurnRate)/ReferenceTurnRate,0.f,1.f):0.f;
		MovingTurnLeftWeight=FMath::FInterpTo(MovingTurnLeftWeight,TurnRate<0?TurnWeight:0.f,DeltaSeconds,1.f/TurnGait.TransitionSeconds);
		MovingTurnRightWeight=FMath::FInterpTo(MovingTurnRightWeight,TurnRate>0?TurnWeight:0.f,DeltaSeconds,1.f/TurnGait.TransitionSeconds);
		MovingTurnPlayRate=FMath::Lerp(WalkPlayRate,RunPlayRate,RunBlend);
		// The simulation also uses the turn clock while moving or airborne, but
		// only a planted stationary turn may replace the locomotion/air pose.
		if (bReset || !Turn.Direction || !bIsGrounded || bIsThrusting || Pawn->HasMovementIntent() || Speed>Config.Mobility.TurnMovementThreshold)
		{
			if (ActiveTurnMontage) Montage_Stop(GaitTransitionSeconds,ActiveTurnMontage);
			ActiveTurnMontage=nullptr;
		}
		else if (TurnSerial!=Turn.Serial || !ActiveTurnMontage)
		{
			const FName Name=Turn.Direction<0?TEXT("TurnLeft"):TEXT("TurnRight");
			ActiveTurnMontage=Config.Asset<UAnimMontage>(Name); TurnSerial=Turn.Serial;
			Montage_Play(ActiveTurnMontage,Config.Gait(Name).BasePlayRate,EMontagePlayReturnType::MontageLength,Turn.Time*Config.Gait(Name).BasePlayRate,true);
		}
		if (ActiveTurnMontage)
		{
			const float Rate=Config.Gait(Turn.Direction<0?TEXT("TurnLeft"):TEXT("TurnRight")).BasePlayRate;
			// Set the current phase without re-playing the montage each frame; replicas use the same turn snapshot.
			Montage_SetPosition(ActiveTurnMontage,FMath::Min(Turn.Time*Rate,ActiveTurnMontage->GetPlayLength()));
		}
		UpdateMontageEvaluationData();
		UpdateGrounding(*Pawn,DeltaSeconds,bReset);
		IntegratedControlRotation = Pawn->GetIntegratedAimRotation();
		const FVector Aim = Pawn->GetIntegratedAimPoint();
		bIntegratedAimValid = !Aim.ContainsNaN() && !Aim.IsNearlyZero();
		IntegratedAimPointComponent = GetSkelMeshComponent()->GetComponentTransform().InverseTransformPosition(Aim);
		const auto& WeaponAim=Pawn->GetWeaponAimState();
		bForwardAim=WeaponAim.bForward; ReturnAlpha=WeaponAim.ReturnAlpha;
		RightReturnStart=WeaponAim.RightStart; LeftReturnStart=WeaponAim.LeftStart;
		for (int32 Side=0; Side<2; ++Side)
		{
			if (bReset) { IntegratedRecoilAge[Side]=BIG_NUMBER; ArmRecoilStart[Side]=0.f; }
			IntegratedRecoilAge[Side] += FMath::Max(0.f,DeltaSeconds);
			const float Weight = IntegratedRecoilCurve && IntegratedRecoilAge[Side] < IntegratedRecoilDuration
				? FMath::Clamp(IntegratedRecoilCurve->GetFloatValue(IntegratedRecoilAge[Side]),0.f,1.f) : 0.f;
			(Side == 0 ? IntegratedFireBlend : IntegratedFireBlendLeft) = Weight;
			const auto& Shot=Config.Gait(TEXT("Shoot"));
			const float Age=IntegratedRecoilAge[Side];
			const float Release=Shot.ArmRecoilAttackSeconds+Shot.ArmRecoilHoldSeconds;
			// Retrigger from the current displacement, never snap a recovering arm
			// back to zero. Both sides use the actual alternating shot event clock.
			const float ArmWeight=Age<Shot.ArmRecoilAttackSeconds
				? FMath::Lerp(ArmRecoilStart[Side],1.f,FMath::SmoothStep(0.f,Shot.ArmRecoilAttackSeconds,Age))
				: 1.f-FMath::SmoothStep(Release,Release+Shot.ArmRecoilRecoverySeconds,Age);
			(Side==0?RightArmRecoil:LeftArmRecoil)=FMath::Clamp(ArmWeight,0.f,1.f);
			const float ShootAge=IntegratedRecoilAge[Side]*Config.Gait(TEXT("Shoot")).BasePlayRate;
			(Side == 0 ? RightShootTime : LeftShootTime) = FMath::Min(ShootAge,ShootDuration);
			(Side == 0 ? RightShootWeight : LeftShootWeight) = ShootAge < ShootDuration
				? FMath::Clamp((ShootDuration-ShootAge)/Config.Gait(TEXT("Shoot")).TransitionSeconds,0.f,1.f) : 0.f;
		}
	}
}

void UGuLiGroundMechAnimInstance::UpdateGrounding(AGuLiGroundMechCharacter& Pawn,float DeltaSeconds,bool bReset)
{
	const auto& Config=Pawn.GetMobilityConfiguration(); const auto& S=Config.IK;
	const auto* Move=CastChecked<UGuLiGroundMechMovementComponent>(Pawn.GetCharacterMovement());
	Grounding.Settings=S; Grounding.ComponentToWorld=GetSkelMeshComponent()->GetComponentTransform();
	Grounding.Revision=AnimationStateRevision; Grounding.DeltaSeconds=DeltaSeconds;
    Grounding.BlinkAlpha=Pawn.GetBlinkPoseAlpha();
    Grounding.BlinkCrouchDepthCm=Config.Blink.BlinkCrouchDepthCm;
    Grounding.BlinkKneeAngleDegrees=Config.Blink.BlinkKneeAngleDegrees;
	Grounding.MoveDirection=LocalHorizontalVelocity.GetSafeNormal();
	Grounding.FloorHeight=Pawn.GetActorLocation().Z-Pawn.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	if (Move->CurrentFloor.IsWalkableFloor()) Grounding.FloorHeight=Move->CurrentFloor.HitResult.ImpactPoint.Z;
	const bool bGround=Move->IsGroundedForAnimation() && !bIsThrusting && !bIsAirborne && Pawn.GetBlinkState().Phase!=2;
	if (bReset) Grounding.GroundAlpha=0;
	Grounding.GroundAlpha=FMath::FInterpConstantTo(Grounding.GroundAlpha,bGround?1.f:0.f,DeltaSeconds,1.f/(bGround?S.BlendInSeconds:S.BlendOutSeconds));
	Grounding.Left={}; Grounding.Right={};
	if (!bGround || !S.Enabled) return;
	auto* World=Pawn.GetWorld(); auto* Mass=World->GetSubsystem<UGuLiGroundMassContactSubsystem>();
	struct FContact { bool Hit=false; FVector Point,Normal; FTransform Surface; uint64 ID=0; uint32 Revision=0; };
	auto Trace=[&](FVector At)
	{
		FContact C; FHitResult Hit;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(GroundMechSole),false,&Pawn);
		const FVector Start=At+FVector(0,0,S.TraceUp),End=At-FVector(0,0,S.TraceDown);
		if (World->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility,Query) && Hit.ImpactNormal.Z>0.1f)
		{
			C.Hit=true; C.Point=Hit.ImpactPoint; C.Normal=Hit.ImpactNormal;
			if (auto* Component=Hit.GetComponent()) { C.Surface=Component->GetComponentTransform(); C.ID=Component->GetUniqueID(); }
		}
		if (Mass)
		{
			TArray<FGuLiGroundMassBody> Bodies;
			Mass->QueryMassBodies(FBox2D(FVector2D(At)-FVector2D(1),FVector2D(At)+FVector2D(1)),0.f,Bodies);
			for (const auto& Body:Bodies)
				if (FVector::DistSquared2D(At,Body.Location)<=FMath::Square(Body.RadiusCentimeters)
					&& Body.TopZ<=Start.Z && Body.TopZ>=End.Z && (!C.Hit || Body.TopZ>C.Point.Z))
				{
					C.Hit=true; C.Point=FVector(At.X,At.Y,Body.TopZ); C.Normal=FVector::UpVector;
					C.Surface=FTransform(Body.Rotation,Body.Location); C.ID=(uint64(1)<<63)|Body.SoldierId.Value;
					C.Revision=HashCombine(Mass->GetCurrentEpoch(),Body.DisplacementRevision);
				}
		}
		return C;
	};
	for (int32 Side=0;Side<2;++Side)
	{
		auto& Sample=Side?Grounding.Right:Grounding.Left;
		const FTransform Foot=GetSkelMeshComponent()->GetSocketTransform(Side?TEXT("Foot_R"):TEXT("Foot_L"));
		const FTransform Toe=GetSkelMeshComponent()->GetSocketTransform(Side?TEXT("FootEnd_R"):TEXT("FootEnd_L"));
		const FContact H=Trace(Foot.TransformPosition(Side?S.HeelRight:S.HeelLeft));
		const FContact T=Trace(Toe.TransformPosition(Side?S.ToeRight:S.ToeLeft));
		if (!H.Hit && !T.Hit) continue;
		const FContact& Contact=H.Hit?H:T;
		Sample.bHit=true; Sample.bHeelHit=H.Hit; Sample.bToeHit=T.Hit;
		Sample.HeelNormal=H.Hit?H.Normal:T.Normal; Sample.ToeNormal=T.Hit?T.Normal:H.Normal;
		Sample.bCanLock=H.Hit && T.Hit && H.ID==T.ID && H.Revision==T.Revision
			&& FVector::DotProduct(H.Normal,T.Normal)>.99f
			&& FMath::Abs(FVector::DotProduct(T.Point-H.Point,H.Normal))<=S.ContactHeightTolerance;
		Sample.Heel=H.Hit?H.Point:T.Point; Sample.Toe=T.Hit?T.Point:H.Point;
		Sample.Normal=((H.Hit?H.Normal:T.Normal)+(T.Hit?T.Normal:H.Normal)).GetSafeNormal();
		if (Sample.bCanLock)
		{
			const FVector Forward=(T.Point-H.Point).GetSafeNormal();
			const FVector Right=FVector::CrossProduct(Forward,Sample.Normal).GetSafeNormal();
			if (!Right.IsNearlyZero()) Sample.Normal=FVector::CrossProduct(Right,Forward).GetSafeNormal();
		}
		Sample.Surface=Contact.Surface; Sample.SurfaceId=Contact.ID; Sample.SurfaceRevision=Contact.Revision;
		if (Contact.ID&(uint64(1)<<63)) Grounding.FloorHeight=Contact.Point.Z;
	}
}

void UGuLiGroundMechAnimInstance::ConfigureIntegratedRecoil(UCurveFloat* Curve, float Duration)
{
	IntegratedRecoilCurve = Curve;
	IntegratedRecoilDuration = Duration;
}

void UGuLiGroundMechAnimInstance::TriggerIntegratedRecoil(float AgeSeconds, uint8 MuzzleIndex)
{
	if (MuzzleIndex < 2 && FMath::IsFinite(AgeSeconds))
	{
		// A delayed cue must not rewind/cancel a more recent shot on the same barrel.
		const float Age=FMath::Max(0.f,AgeSeconds);
		if (Age <= IntegratedRecoilAge[MuzzleIndex])
		{
			ArmRecoilStart[MuzzleIndex]=MuzzleIndex==0?RightArmRecoil:LeftArmRecoil;
			IntegratedRecoilAge[MuzzleIndex]=Age;
		}
	}
}
