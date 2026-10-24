#include "Gameplay/GroundMech/GuLiGroundMechAnimInstance.h"
#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Gameplay/GroundMech/GuLiGroundMechMovementComponent.h"
#include "Gameplay/GroundMech/GuLiGroundMechRocketComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Gameplay/GroundMech/GuLiGroundMassContactSubsystem.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/BlendSpace.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"

void UGuLiGroundMechAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	PreviousPawn.Reset();
	PreviousController.Reset();
	IntegratedRecoilAge[0] = IntegratedRecoilAge[1] = BIG_NUMBER;
	IntegratedFireBlendLeft = 0.f;
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
}

void UGuLiGroundMechAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	auto* Pawn = Cast<AGuLiGroundMechCharacter>(TryGetPawnOwner());
	const auto* Move = Pawn ? Cast<UGuLiGroundMechMovementComponent>(Pawn->GetCharacterMovement()) : nullptr;
	if (!Pawn || !Move || !Pawn->GetMobilityConfiguration().bReady)
	{
		Speed = TurnRate = 0.f;
		HorizontalVelocity = LocalHorizontalVelocity = FVector::ZeroVector;
		bIsGrounded = true; bIsAirborne = bIsThrusting = false;
		IntegratedFireBlend = IntegratedForwardBlend = IntegratedStrafeBlend = 0.f;
		IntegratedFireBlendLeft = 0.f;
		IntegratedControlRotation = FRotator::ZeroRotator;
		bIntegratedRunning = false;
		bIntegratedAimValid = false;
		Grounding.GroundAlpha=0;
		return;
	}
	const float Yaw = Pawn->GetActorRotation().Yaw;
	const bool bReset = PreviousPawn.Get() != Pawn || PreviousController.Get() != Pawn->GetController()
		|| PreviousDisplacementRevision != Move->GetDisplacementRevision();
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
		JumpLoopPlayRate=Config.Gait(TEXT("JumpLoop")).BasePlayRate;
		JumpStartDuration=JumpStartSequence->GetPlayLength(); ShootDuration=ShootSequence->GetPlayLength();
		RecoilDistance=Config.Gait(TEXT("Shoot")).RecoilCentimeters;
		GunYawLimit=Config.Mobility.GunYawLimit; GunPitchUpLimit=Config.Mobility.GunPitchUpLimit; GunPitchDownLimit=Config.Mobility.GunPitchDownLimit;
		GaitTransitionSeconds=Walk.TransitionSeconds;
		bIntegratedRunning = Speed > Pawn->GetWalkSpeed()+UE_KINDA_SMALL_NUMBER;
		if (bReset) { SmoothedSpeed=Speed; Grounding.StrideScale=1.f; RunBlend=bIntegratedRunning?1.f:0.f; }
		SmoothedSpeed=FMath::FInterpTo(SmoothedSpeed,Speed,DeltaSeconds,1.f/Walk.SpeedSmoothingSeconds);
		RunBlend=FMath::FInterpConstantTo(RunBlend,bIntegratedRunning?1.f:0.f,DeltaSeconds,1.f/GaitTransitionSeconds);
		const FVector Direction=Speed>Config.Mobility.TurnMovementThreshold?LocalHorizontalVelocity.GetSafeNormal():FVector::ZeroVector;
		IntegratedForwardBlend=Direction.Y; IntegratedStrafeBlend=-Direction.X;
		auto Reference=[&](UBlendSpace* Blend)
		{
			TArray<FBlendSampleData> Samples; int32 Triangle=INDEX_NONE;
			Blend->GetSamplesFromBlendInput(FVector(IntegratedStrafeBlend,IntegratedForwardBlend,0),Samples,Triangle,true);
			FVector Travel=FVector::ZeroVector; float CycleLength=0.f;
			for (const auto& Sample:Samples) for (const auto& G:Config.Gaits)
				if (Config.Asset<UAnimSequence>(G.Key)==Sample.Animation && Sample.Animation)
				{
					FVector Axis=FVector::ZeroVector; const FString Name=G.Key.ToString();
					if (Name.EndsWith(TEXT("Forward"))) Axis.Y=1; else if (Name.EndsWith(TEXT("Backward"))) Axis.Y=-1;
					else if (Name==TEXT("StrafeLeft")) Axis.X=1; else if (Name==TEXT("StrafeRight")) Axis.X=-1;
					const float WeightedCycle=Sample.GetClampedWeight()*Sample.Animation->GetPlayLength();
					Travel+=Axis*G.Value.ReferenceSpeed*WeightedCycle; CycleLength+=WeightedCycle;
				}
			// BlendSpace synchronizes normalized time, so blend distance per cycle, not unrelated clip rates.
			return FMath::Max(1.f,float(FVector::DotProduct(Travel,Direction))/FMath::Max(.001f,CycleLength));
		};
		const float WalkReference=Reference(WalkBlendSpace), RunReference=Reference(RunBlendSpace);
		const float ReferenceSpeed=FMath::Lerp(WalkReference,RunReference,RunBlend);
		const auto& Current=bIntegratedRunning?Run:Walk;
		const float Stride=FMath::Clamp(FMath::Sqrt(SmoothedSpeed/ReferenceSpeed),Current.MinStrideScale,Current.MaxStrideScale);
		Grounding.StrideScale=FMath::FInterpTo(Grounding.StrideScale,Direction.IsNearlyZero()?1.f:Stride,DeltaSeconds,1.f/Current.SpeedSmoothingSeconds);
		WalkPlayRate=Direction.IsNearlyZero()?Config.Gait(TEXT("Idle")).BasePlayRate:FMath::Clamp(SmoothedSpeed/(Grounding.StrideScale*WalkReference),Walk.MinPlayRate,Walk.MaxPlayRate);
		RunPlayRate=Direction.IsNearlyZero()?Config.Gait(TEXT("Idle")).BasePlayRate:FMath::Clamp(SmoothedSpeed/(Grounding.StrideScale*RunReference),Run.MinPlayRate,Run.MaxPlayRate);
		const auto& Turn=Pawn->GetTurnState();
		// The simulation also uses the turn clock while moving or airborne, but
		// only a planted stationary turn may replace the locomotion/air pose.
		if (bReset || !Turn.Direction || !bIsGrounded || bIsThrusting || Speed>Config.Mobility.TurnMovementThreshold)
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
		for (int32 Side=0; Side<2; ++Side)
		{
			if (bReset) IntegratedRecoilAge[Side] = BIG_NUMBER;
			IntegratedRecoilAge[Side] += FMath::Max(0.f,DeltaSeconds);
			const float Weight = IntegratedRecoilCurve && IntegratedRecoilAge[Side] < IntegratedRecoilDuration
				? FMath::Clamp(IntegratedRecoilCurve->GetFloatValue(IntegratedRecoilAge[Side]),0.f,1.f) : 0.f;
			(Side == 0 ? IntegratedFireBlend : IntegratedFireBlendLeft) = Weight;
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
	Grounding.MoveDirection=LocalHorizontalVelocity.GetSafeNormal();
	Grounding.FloorHeight=Pawn.GetActorLocation().Z-Pawn.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	if (Move->CurrentFloor.IsWalkableFloor()) Grounding.FloorHeight=Move->CurrentFloor.HitResult.ImpactPoint.Z;
	const bool bGround=Move->IsGroundedForAnimation() && !bIsThrusting && !bIsAirborne;
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
		Sample.bHit=true; Sample.bCanLock=H.Hit && T.Hit && H.ID==T.ID;
		Sample.Heel=H.Hit?H.Point:T.Point; Sample.Toe=T.Hit?T.Point:H.Point;
		Sample.Normal=((H.Hit?H.Normal:T.Normal)+(T.Hit?T.Normal:H.Normal)).GetSafeNormal();
		if (H.Hit && T.Hit)
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
		if (Age <= IntegratedRecoilAge[MuzzleIndex]) IntegratedRecoilAge[MuzzleIndex]=Age;
	}
}
