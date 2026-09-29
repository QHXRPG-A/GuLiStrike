#include "Commander/Presentation/GuLiCommanderPresentationActor.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectTypes.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Gameplay/Skills/GuLiArmySkillSubsystem.h"

bool AGuLiCommanderPresentationActor::TryGetPresentedVisualTransform(FGuLiSoldierId Id, FTransform& Out) const
{
	if (!TryGetPresentedSoldierTransform(Id, Out)) return false;
	const auto* Soldier = PresentedSoldiers.Find(Id);
	const auto* Handle = SoldierInstanceHandles.Find(Id);
	const auto* Data = GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>();
	const auto* Def = Handle && Data ? Data->FindSoldierDefinition(Handle->BatchUnitTypeId) : nullptr;
	if (Def && Soldier) Out = GuLiMechanicalAnimation::HoverBodyTransform(Def->MechanicalAnimation, Soldier->MechanicalPose) * Out;
	return true;
}

void AGuLiCommanderPresentationActor::UpdateMechanicalPresentation(FGuLiSoldierId Id,
	FGuLiCommanderPresentedSoldier& Soldier, const FTransform& Before, float Dt, bool bReset, bool bAlive)
{
	const auto* Handle = SoldierInstanceHandles.Find(Id);
	const auto* Data = GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>();
	const auto* Definition = Handle && Data ? Data->FindSoldierDefinition(Handle->BatchUnitTypeId) : nullptr;
	if (!Definition || !Definition->MechanicalAnimation.IsEnabled()) return;
	const auto& C = Definition->MechanicalAnimation;
	auto& S = Soldier.MechanicalPose;
	Soldier.PreviousMechanicalFrame = Soldier.MechanicalFrame;
	// A late-joined phased unit/wreck still needs its first complete (lifted) pose.
	if (bAlive || !S.bInitialized)
	{
		GuLiMechanicalAnimation::StepLocomotion(C, Before, Soldier.PresentedTransform, Dt, bReset, S);
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
		GuLiMechanicalAnimation::EvaluateHover(C, Id.Value, Soldier.RenderServerTimeSeconds, S);
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
		Soldier.MechanicalFrame = GuLiMechanicalAnimation::BuildFrame(C, S, Soldier.PresentedTransform.Rotator().Yaw, Soldier.RenderServerTimeSeconds);
		const auto* Army=GetWorld()->GetSubsystem<UGuLiArmySkillSubsystem>();
		const auto* Missile=Army ? Army->FindResolvedSkill(Handle->BatchTeam,Handle->BatchUnitTypeId,TEXT("MissileLauncher")) : nullptr;
		Soldier.MechanicalFrame.MissilePodVisible=Missile && Missile->bUnlocked ? 1.0f : 0.0f;
		// A visibility transition must not generate velocity from the collapsed geometry.
		Soldier.PreviousMechanicalFrame.MissilePodVisible=Soldier.MechanicalFrame.MissilePodVisible;
	}
	else Soldier.PendingRecoil.Reset();
	if (bReset) Soldier.PreviousMechanicalFrame = Soldier.MechanicalFrame;
	if (auto* Component = FindUnitInstances(Handle->BatchUnitTypeId, Handle->BatchTeam))
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
	}
}

bool AGuLiCommanderPresentationActor::ResolveMechanicalMuzzle(const FGuLiCombatShotCue& Cue,
	FTransform& Out, float& RenderTime) const
{
	const FGuLiSoldierId Id(Cue.Source.LocalId);
	const auto* Soldier = PresentedSoldiers.Find(Id);
	const auto* Handle = SoldierInstanceHandles.Find(Id);
	const auto* Data = GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>();
	const auto* Definition = Handle && Data ? Data->FindSoldierDefinition(Handle->BatchUnitTypeId) : nullptr;
	if (!Soldier || !Soldier->bHasPresentedTransform || !Definition) return false;
	RenderTime = Soldier->RenderServerTimeSeconds;
	return GuLiMechanicalAnimation::ResolveMuzzle(Definition->MechanicalAnimation, Soldier->MechanicalPose,
		Soldier->PresentedTransform, Cue.SlotId, Cue.MuzzleIndex, RenderTime, Out);
}

void AGuLiCommanderPresentationActor::ApplyMechanicalOverlay(UInstancedStaticMeshComponent& Component,
	const TArray<FGuLiSoldierId>& Ids, const TArray<float>* HitTimes) const
{
	for (int32 Index = 0; Index < Ids.Num(); ++Index)
		if (const auto* Soldier = PresentedSoldiers.Find(Ids[Index]))
			GuLiMechanicalAnimation::WriteInstance(Component, Index, Soldier->MechanicalFrame, Soldier->PreviousMechanicalFrame,
				HitTimes && HitTimes->IsValidIndex(Index) ? (*HitTimes)[Index] : -1000.0f);
}
