#include "Commander/Presentation/GuLiCommanderPresentationActor.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "Commander/Mass/GuLiMassMovementTuning.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectTypes.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Gameplay/Skills/GuLiArmySkillSubsystem.h"
#include "Gameplay/Building/GuLiPlacedBuilding.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Gameplay/Building/GuLiConstructedUnitComponent.h"
#include "EngineUtils.h"
#include "Gameplay/Vfx/GuLiClientPresentationPolicy.h"
#include "Gameplay/GroundMech/GuLiGroundMassContactSubsystem.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

const FGuLiCommanderPresentedSoldier* AGuLiCommanderPresentationActor::GetQuerySoldier(FGuLiSoldierId Id) const
{
	const auto* Source = PresentedSoldiers.Find(Id);
	if (!Source || Source->bMainViewVisible || !GuLiClientPresentation::OffscreenUnitsEnabled() || !GetWorld()) return Source;
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCommanderPresentation_DemandPose);
	const double Now = GetWorld()->GetTimeSeconds();
	const double RenderTime = Source->RenderServerTimeSeconds + FMath::Clamp(Now - Source->LastRenderClockLocalTime, 0.0, .25);
	auto& Cached = DemandPoses.FindOrAdd(Id);
	if (Cached.Frame == GFrameCounter && Cached.Revision == Source->PoseRevision && Cached.RenderTime == RenderTime) return &Cached.Soldier;
	Cached.Frame = GFrameCounter; Cached.Revision = Source->PoseRevision; Cached.RenderTime = RenderTime;
	Cached.Soldier = *Source;
	auto& Pose = Cached.Soldier;
	Pose.RenderServerTimeSeconds = RenderTime;
	FTransform Current;
	if (!EvaluateAuthoritativeTransform(Pose, RenderTime, Current)) return Source;
	Pose.AuthoritativeTransform = Current;
	// Query evaluation cannot consume prediction/recoil queues or write an ISM instance.
	const_cast<AGuLiCommanderPresentationActor*>(this)->ApplyPrediction(Id, Now, Current, false);
	if (auto* Contacts = GetWorld()->GetSubsystem<UGuLiGroundMassContactSubsystem>()) Contacts->ApplyContactPresentation(Id, Current);
	const FTransform Before = Pose.PresentedTransform;
	Pose.PresentedTransform = Current; Pose.bHasPresentedTransform = true;
	const_cast<AGuLiCommanderPresentationActor*>(this)->UpdateMechanicalPresentation(Id, Pose, Before,
		float(FMath::Max(0.0, Now - Source->LastPresentationLocalTime)), !Source->bHasPresentedTransform,
		Source->LastLifeState == EGuLiSoldierLifeState::Alive, false);
	return &Pose;
}

bool AGuLiCommanderPresentationActor::TryGetPresentedVisualTransform(FGuLiSoldierId Id, FTransform& Out) const
{
	if (!TryGetPresentedSoldierTransform(Id, Out)) return false;
	const auto* Soldier = GetQuerySoldier(Id);
	const auto* Handle = SoldierInstanceHandles.Find(Id);
	const auto* Data = GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>();
	const auto* Def = Handle && Data ? Data->FindSoldierDefinition(Handle->BatchUnitTypeId) : nullptr;
	if (Def && Soldier)
	{
		if (Soldier->MechanicalVisual.bInitialized) Out = Soldier->MechanicalVisual.Root;
		Out = GuLiMechanicalAnimation::HoverBodyTransform(Def->MechanicalAnimation, Soldier->MechanicalPose) * Out;
	}
	return true;
}

bool AGuLiCommanderPresentationActor::TryGetPresentedSoldierModelCenter(
	FGuLiSoldierId Id, FVector& OutCenter) const
{
	const auto* Handle = SoldierInstanceHandles.Find(Id);
	const auto* World = GetWorld();
	const auto* Data = World ? World->GetSubsystem<UGuLiCommanderDataSubsystem>() : nullptr;
	const auto* Definition = Handle && Handle->UnitInstanceIndex != INDEX_NONE && Data
		? Data->FindSoldierDefinition(Handle->BatchUnitTypeId) : nullptr;
	FTransform VisualTransform;
	if (!Definition || !TryGetPresentedVisualTransform(Id, VisualTransform)
		|| VisualTransform.ContainsNaN()) return false;
	const FBox Bounds = Definition->GetModelBoundsCentimeters();
	if (!Bounds.IsValid) return false;
	// Bounds already include PresentationScale; the visual pose supplies only rotation and translation.
	const FVector Center = VisualTransform.TransformPositionNoScale(Bounds.GetCenter());
	if (Center.ContainsNaN()) return false;
	OutCenter = Center;
	return true;
}

void AGuLiCommanderPresentationActor::UpdateMechanicalPresentation(FGuLiSoldierId Id,
	FGuLiCommanderPresentedSoldier& Soldier, const FTransform& Before, float Dt, bool bReset, bool bAlive, bool bWriteInstances)
{
	const auto* Handle = SoldierInstanceHandles.Find(Id);
	const auto* Data = GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>();
	const auto* Definition = Handle && Data ? Data->FindSoldierDefinition(Handle->BatchUnitTypeId) : nullptr;
	if (!Definition) return;
	if (bWriteInstances && Definition->bConstructionOnly && !Soldier.bConstructionVisualHandled)
	{
		// The Mass pose may precede the site's component RepNotify. Retire the exact
		// matching completed site before drawing the new ID, avoiding a duplicate frame.
		for (TActorIterator<AGuLiPlacedBuilding> It(GetWorld()); It; ++It)
		{
			auto* Life=It->FindComponentByClass<UGuLiBuildingLifecycleComponent>();
			if (!Life || !Life->GetState().InstanceId || Life->GetTeam()!=Handle->BatchTeam
				|| Life->GetDefinition().CompletionUnitTypeId!=Handle->BatchUnitTypeId
				|| !Life->GetGroundLocation().Equals(Soldier.PresentedTransform.GetLocation(),2.f)) continue;
			// An older lifecycle snapshot can still say UnderConstruction when Mass
			// arrives first. Its exact construction-only spawn proves conversion.
			if (Life->GetState().Phase!=EGuLiBuildingPhase::Destroyed)
				if (auto* Conversion=It->FindComponentByClass<UGuLiConstructedUnitComponent>()) Conversion->RetireVisualForMass(Id);
		}
		Soldier.bConstructionVisualHandled=true;
	}
	if (Definition->VATDefinition)
	{
		const auto& D = *Definition->VATDefinition;
		Soldier.PreviousVATPlayback = Soldier.VATPlayback;
		const auto& Samples = Soldier.Samples;
		FVector VATVelocity = FVector::ZeroVector;
		if (!Samples.IsEmpty())
		{
			const auto* A = &Samples[0]; const auto* B = A;
			for (const auto& Sample : Samples)
			{
				B = &Sample;
				if (Sample.ServerTimeSeconds >= Soldier.RenderServerTimeSeconds) break;
				A = B;
			}
			const double Interval = B->ServerTimeSeconds - A->ServerTimeSeconds;
			const float Alpha = B->bTeleport || Interval <= UE_DOUBLE_SMALL_NUMBER ? 1.0f
				: float(FMath::Clamp((Soldier.RenderServerTimeSeconds - A->ServerTimeSeconds) / Interval, 0.0, 1.0));
			VATVelocity = FMath::Lerp(A->Velocity, B->Velocity, Alpha);
			Soldier.VATPlayback = Soldier.RenderServerTimeSeconds >= B->ServerTimeSeconds ? B->VATPlayback : A->VATPlayback;
			if (A->VATPlayback.Clip == B->VATPlayback.Clip && D.Clips.IsValidIndex(A->VATPlayback.Clip))
			{
				float Span = B->VATPlayback.Phase - A->VATPlayback.Phase;
				if (D.Clips[A->VATPlayback.Clip].bLoop)
				{
					// Fast locomotion may complete several cycles between 10 Hz samples.
					const float Expected = .5f * (A->VATPlayback.Rate + B->VATPlayback.Rate) * float(Interval);
					Span += FMath::RoundToFloat(Expected - Span);
				}
				const float Phase = A->VATPlayback.Phase + Span * Alpha;
				Soldier.VATPlayback.Phase = D.Clips[A->VATPlayback.Clip].bLoop ? FMath::Frac(Phase) : FMath::Min(Phase,1.0f);
			}
			const float Ahead = float(FMath::Clamp(Soldier.RenderServerTimeSeconds - B->ServerTimeSeconds, 0.0, .2));
			if (Ahead > 0 && D.Clips.IsValidIndex(Soldier.VATPlayback.Clip))
			{
				Soldier.VATPlayback.Phase += Ahead * Soldier.VATPlayback.Rate;
				Soldier.VATPlayback.Phase = D.Clips[Soldier.VATPlayback.Clip].bLoop
					? FMath::Frac(Soldier.VATPlayback.Phase) : FMath::Min(Soldier.VATPlayback.Phase,1.0f);
			}
			Soldier.MechanicalPose.UpperYawDegrees = A->UpperYawDegrees + FMath::FindDeltaAngleDegrees(A->UpperYawDegrees,B->UpperYawDegrees)*Alpha;
			for (int32 Side=0; Side<2; ++Side) Soldier.MechanicalPose.GunPitchDegrees[Side] = FMath::Lerp(A->GunPitchDegrees[Side],B->GunPitchDegrees[Side],Alpha);
			Soldier.MechanicalPose.bInitialized = true;
		}
		else GuLiVATAnimation::Step(D, FVector::ZeroVector, Soldier.PresentedTransform.Rotator().Yaw, bAlive, Dt, Soldier.VATPlayback);
		if (auto* Component = bWriteInstances ? FindUnitInstances(Handle->BatchUnitTypeId, Handle->BatchTeam) : nullptr)
			GuLiVATAnimation::WriteInstance(*Component,Handle->UnitInstanceIndex,D,Soldier.VATPlayback,Soldier.PreviousVATPlayback,
				Soldier.MechanicalPose,Soldier.PresentedTransform.Rotator().Yaw,bReset,VATVelocity);

		return;
	}
	if (!Definition->MechanicalAnimation.IsEnabled()) return;
	const auto& C = Definition->MechanicalAnimation;
	auto& S = Soldier.MechanicalPose;
	const bool bFirstPose = !S.bInitialized || (C.Model == EGuLiMechanicalModel::WarMachine && !Soldier.MechanicalVisual.bInitialized);
	const bool bResetPose = bReset || bFirstPose || (Soldier.MechanicalVisual.bInitialized
		&& Soldier.RenderServerTimeSeconds < Soldier.MechanicalVisual.PoseTime);
	Soldier.PreviousMechanicalFrame = Soldier.MechanicalFrame;
	// A late-joined phased unit/wreck still needs its first complete (lifted) pose.
	if (bAlive || bResetPose)
	{
		const auto BeforeVisual = Soldier.MechanicalVisual.bInitialized ? Soldier.MechanicalVisual.Root : Before;
		const double PreviousPoseTime = Soldier.MechanicalVisual.PoseTime;
		const bool bResetVisual = bResetPose;
		const auto& Samples = Soldier.Samples;
		if (!Samples.IsEmpty())
		{
			const auto* A = &Samples[0];
			const auto* B = A;
			for (const auto& Sample : Samples)
			{
				B = &Sample;
				if (Sample.ServerTimeSeconds >= Soldier.RenderServerTimeSeconds) break;
				A = B;
			}
			const double Interval = B->ServerTimeSeconds - A->ServerTimeSeconds;
			const float Alpha = B->bTeleport || Interval <= UE_DOUBLE_SMALL_NUMBER ? 1.0f
				: float(FMath::Clamp((Soldier.RenderServerTimeSeconds-A->ServerTimeSeconds)/Interval, 0.0, 1.0));
			S.UpperYawDegrees = A->UpperYawDegrees + FMath::FindDeltaAngleDegrees(A->UpperYawDegrees, B->UpperYawDegrees)*Alpha;
			for (int32 Side = 0; Side < 2; ++Side) S.GunPitchDegrees[Side] = FMath::Lerp(A->GunPitchDegrees[Side], B->GunPitchDegrees[Side], Alpha);
			// Select a semantic transition on its simulation timestamp; never interpolate its start time.
			const auto* Hover = B->HoverBlendStartMilliseconds * .001 <= Soldier.RenderServerTimeSeconds ? B : A;
			S.HoverBlendStartMilliseconds = Hover->HoverBlendStartMilliseconds;
			S.HoverBlendFromWeight = Hover->HoverBlendFromWeight;
			S.bHoverIdleTarget = Hover->bHoverIdleTarget;
			S.bInitialized = true;
		}
		if (!S.bInitialized)
		{
			// A reliable late-join wreck can precede its first pose sample. Initialize once.
			S.UpperYawDegrees = Soldier.PresentedTransform.Rotator().Yaw;
			S.bInitialized = true;
		}
		GuLiMechanicalAnimation::EvaluateHover(C, Id.Value, Soldier.RenderServerTimeSeconds, S);
		if (C.Model == EGuLiMechanicalModel::WarMachine)
		{
			GuLiMechanicalAnimation::StepVisualTurn(C, S, Soldier.PresentedTransform, Soldier.RenderServerTimeSeconds, bResetVisual, Soldier.MechanicalVisual,
				GuLiMassMovementTuning::GetTurnRateScale());
			const float VisualDt = bResetVisual ? 0 : float(FMath::Max(0.0, Soldier.RenderServerTimeSeconds - PreviousPoseTime));
			GuLiMechanicalAnimation::StepLocomotion(C, BeforeVisual, Soldier.MechanicalVisual.Root, VisualDt, bResetVisual, S);
		}
		else GuLiMechanicalAnimation::StepLocomotion(C, Before, Soldier.PresentedTransform, Dt, bReset, S);
		for (int32 Index = 0; Index < Soldier.PendingRecoil.Num();)
		{
			const auto& Cue = Soldier.PendingRecoil[Index];
			if (Cue.ServerTime > Soldier.RenderServerTimeSeconds) { ++Index; continue; }
			if (Cue.ServerTime >= S.RecoilStartTime[Cue.Side])
			{
				S.RecoilStartTime[Cue.Side] = Cue.ServerTime;
				S.RecoilFromCentimeters[Cue.Side] = Cue.FromCentimeters;
			}
			Soldier.PendingRecoil.RemoveAt(Index, 1, EAllowShrinking::No);
		}
		Soldier.MechanicalFrame = Soldier.MechanicalVisual.bInitialized
			? GuLiMechanicalAnimation::BuildVisualFrame(C, S, Soldier.MechanicalVisual, Soldier.RenderServerTimeSeconds)
			: GuLiMechanicalAnimation::BuildFrame(C, S, Soldier.PresentedTransform.Rotator().Yaw, Soldier.RenderServerTimeSeconds);
		const auto* Army=GetWorld()->GetSubsystem<UGuLiArmySkillSubsystem>();
		const auto* Missile=Army ? Army->FindResolvedSkill(Handle->BatchTeam,Handle->BatchUnitTypeId,TEXT("MissileLauncher")) : nullptr;
		Soldier.MechanicalFrame.MissilePodVisible=Missile && Missile->bUnlocked ? 1.0f : 0.0f;
		// A visibility transition must not generate velocity from the collapsed geometry.
		Soldier.PreviousMechanicalFrame.MissilePodVisible=Soldier.MechanicalFrame.MissilePodVisible;
	}
	else Soldier.PendingRecoil.Reset();
	if (bResetPose) Soldier.PreviousMechanicalFrame = Soldier.MechanicalFrame;
	if (auto* Component = bWriteInstances ? FindUnitInstances(Handle->BatchUnitTypeId, Handle->BatchTeam) : nullptr)
		GuLiMechanicalAnimation::WriteInstance(*Component, Handle->UnitInstanceIndex,
			Soldier.MechanicalFrame, Soldier.PreviousMechanicalFrame);
}

void AGuLiCommanderPresentationActor::ObserveMechanicalShot(const FGuLiCombatShotCue& Cue)
{
	if (!Cue.bMechanicalShot || Cue.MuzzleIndex > 1) return;
	if (auto* Soldier = PresentedSoldiers.Find(FGuLiSoldierId(Cue.Source.LocalId)))
	{
		if (Soldier->PendingRecoil.Num() >= 16) Soldier->PendingRecoil.RemoveAt(0);
		Soldier->PendingRecoil.Add({Cue.MechanicalPoseTimeSeconds, Cue.RecoilFromCentimeters, Cue.MuzzleIndex});
		Soldier->PendingRecoil.Sort([](const auto& A, const auto& B) { return A.ServerTime < B.ServerTime; });
		++Soldier->PoseRevision;
	}
}

bool AGuLiCommanderPresentationActor::ResolveMechanicalMuzzle(const FGuLiCombatShotCue& Cue,
	FTransform& Out, float& RenderTime) const
{
	const FGuLiSoldierId Id(Cue.Source.LocalId);
	const auto* Soldier = GetQuerySoldier(Id);
	const auto* Handle = SoldierInstanceHandles.Find(Id);
	const auto* Data = GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>();
	const auto* Definition = Handle && Data ? Data->FindSoldierDefinition(Handle->BatchUnitTypeId) : nullptr;
	if (!Soldier || !Soldier->bHasPresentedTransform || !Definition) return false;
	RenderTime = Soldier->RenderServerTimeSeconds;
	if (Definition->VATDefinition)
		return GuLiVATAnimation::ResolveMuzzle(*Definition->VATDefinition, Soldier->VATPlayback, Soldier->MechanicalPose,
			Soldier->PresentedTransform, Cue.MuzzleIndex, Out);
	if (Soldier->MechanicalVisual.bInitialized)
		return GuLiMechanicalAnimation::ResolveVisualMuzzle(Definition->MechanicalAnimation, Soldier->MechanicalPose,
			Soldier->MechanicalVisual, Cue.SlotId, Cue.MuzzleIndex, Out);
	return GuLiMechanicalAnimation::ResolveMuzzle(Definition->MechanicalAnimation, Soldier->MechanicalPose,
		Soldier->PresentedTransform, Cue.SlotId, Cue.MuzzleIndex, RenderTime, Out);
}

bool AGuLiCommanderPresentationActor::ResolveMechanicalLaunchOffset(const FGuLiTargetHandle& Source,
	FName Slot, const FVector& LaunchLocation, FVector& OutOffset) const
{
	const FGuLiSoldierId Id(Source.LocalId);
	const auto* Soldier = GetQuerySoldier(Id);
	const auto* Handle = SoldierInstanceHandles.Find(Id);
	const auto* Data = GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>();
	const auto* Definition = Handle && Handle->BatchUnitTypeId == 2 && Data ? Data->FindSoldierDefinition(2) : nullptr;
	if (!Soldier || !Definition || !Soldier->MechanicalVisual.bInitialized) return false;
	// Existing launch position identifies the calibrated left/right mount, including Q missiles.
	// The projectile freezes this display-to-launch displacement once; it never follows the mount.
	double Closest = TNumericLimits<double>::Max();
	bool bFound = false;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		FTransform Logical, Visual;
		if (!GuLiMechanicalAnimation::ResolveMuzzle(Definition->MechanicalAnimation, Soldier->MechanicalPose,
			Soldier->PresentedTransform, Slot, Side, Soldier->RenderServerTimeSeconds, Logical)
			|| !GuLiMechanicalAnimation::ResolveVisualMuzzle(Definition->MechanicalAnimation, Soldier->MechanicalPose,
				Soldier->MechanicalVisual, Slot, Side, Visual)) continue;
		const double Distance = FVector::DistSquared(Logical.GetLocation(), LaunchLocation);
		if (Distance < Closest)
		{
			Closest = Distance;
			OutOffset = Visual.GetLocation() - LaunchLocation;
			bFound = true;
		}
	}
	return bFound;
}

void AGuLiCommanderPresentationActor::ApplyMechanicalOverlay(UInstancedStaticMeshComponent& Component,
	const TArray<FGuLiSoldierId>& Ids, const TArray<float>* HitTimes) const
{
	for (int32 Index = 0; Index < Ids.Num(); ++Index)
		if (const auto* Soldier = PresentedSoldiers.Find(Ids[Index]))
		{
			GuLiMechanicalAnimation::WriteInstance(Component, Index, Soldier->MechanicalFrame, Soldier->PreviousMechanicalFrame,
				HitTimes && HitTimes->IsValidIndex(Index) ? (*HitTimes)[Index] : -1000.0f);
			const auto* Handle = SoldierInstanceHandles.Find(Ids[Index]);
			const auto* Data = GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>();
			const auto* D = Handle && Data ? Data->FindSoldierDefinition(Handle->BatchUnitTypeId) : nullptr;
			if (D && D->VATDefinition) GuLiVATAnimation::WriteInstance(Component,Index,*D->VATDefinition,
				Soldier->VATPlayback,Soldier->PreviousVATPlayback,Soldier->MechanicalPose,Soldier->PresentedTransform.Rotator().Yaw,true,
				Soldier->Samples.IsEmpty() ? FVector::ZeroVector : Soldier->Samples.Last().Velocity);
		}
}
