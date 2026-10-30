#include "Gameplay/CombatEffects/GuLiCombatEffectPresentationSubsystem.h"
#include "Gameplay/CombatEffects/GuLiBulletTrailPresentation.h"
#include "Commander/Presentation/GuLiCommanderLODSubsystem.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "NiagaraSystem.h"

void UGuLiCombatEffectPresentationSubsystem::BeginBulletTrailsFrame(float Now,bool bEnabled)
{
	BulletLocalTeam=EGuLiTeam::Unassigned; BulletLocalViews.Reset();
	for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
	{
		const auto* PC=It->Get(); if (!PC || !PC->IsLocalController()) continue;
		FVector Position; FRotator Rotation; PC->GetPlayerViewPoint(Position,Rotation); BulletLocalViews.Add(Position);
		if (BulletLocalTeam!=EGuLiTeam::Unassigned) continue;
		if (const auto* PS=PC->GetPlayerState<AGuLiBattlePlayerState>()) BulletLocalTeam=PS->GetTeam();
		if (BulletLocalTeam==EGuLiTeam::Unassigned && PC->GetPawn())
			if (const auto* Health=PC->GetPawn()->FindComponentByClass<UGuLiCombatHealthComponent>()) BulletLocalTeam=Health->GetCombatTeam();
	}
	if (BulletTrails) BulletTrails->BeginFrame(Now,bEnabled);
}

FLinearColor UGuLiCombatEffectPresentationSubsystem::GroundBulletTint(const FGuLiCombatEffectState& State) const
{
	if (State.Source.Kind==EGuLiTargetKind::GroundActor) return FLinearColor(FColor::FromHex(TEXT("EE9D58")));
	if (!Catalog || BulletLocalTeam==EGuLiTeam::Unassigned) return FLinearColor::White;
	return State.SourceTeam==BulletLocalTeam ? Catalog->FriendlyGroundMachineGunTint : Catalog->EnemyGroundMachineGunTint;
}

bool UGuLiCombatEffectPresentationSubsystem::UpdateMassBulletTrail(FGuLiLocalCombatEffect& Visual,float Now,bool bEnabled)
{
	const auto& State=Visual.State;
	const bool Finished=State.Phase==EGuLiCombatEffectPhase::Finished;
	if (Finished) Visual.RenderLocation=State.Location;
	else if (!Visual.bHasPrediction) Visual.RenderLocation=FVector(State.LaunchLocation)+FVector(State.Velocity)
		*FMath::Clamp(Now-State.StartTime,0.f,State.EndTime-State.StartTime);
	const FVector Direction=FVector(State.LaunchDirection).GetSafeNormal();
	const FVector Origin=State.LaunchLocation;
	if (Visual.GroundTrailFacing.IsNearlyZero())
	{
		FVector View=Origin+FVector::UpVector; FRotator Rotation;
		if (const auto* PC=GetWorld()->GetFirstPlayerController()) PC->GetPlayerViewPoint(View,Rotation);
		Visual.GroundTrailFacing=FVector::VectorPlaneProject(View-Origin,Direction).GetSafeNormal(UE_SMALL_NUMBER,FVector::UpVector);
	}
	// Bound the recent path, including its retiring portion; a visible old tail must survive head culling.
	FBox Bounds(ForceInit); Bounds+=Visual.RenderLocation;
	Bounds+=Visual.RenderLocation-Direction*BulletTrails->GetMaximumLength();
	Bounds=Bounds.ExpandBy(BulletTrails->GetStrokeEnvelope());
	return BulletTrails->SubmitMass(State,Origin,Visual.GroundTrailFacing,GroundBulletTint(State),bEnabled && IsVisibleBounds(Bounds));
}

void UGuLiCombatEffectPresentationSubsystem::SubmitBulletLight(const FGuLiLocalCombatEffect& Visual,float Now,bool bEnabled)
{
	if (!BulletTrails || !BulletTrails->HasLightSystem() || !bEnabled || !Catalog) return;
	const auto& State=Visual.State;
	if (State.Phase==EGuLiCombatEffectPhase::Finished || Now<State.StartTime || Now>=State.EndTime) return;
	if (State.Source.Kind!=EGuLiTargetKind::GroundActor && State.Source.Kind!=EGuLiTargetKind::CommanderSoldier) return;
	const float Radius=FMath::Max(0.f,Catalog->TracerLightRadius);
	const FBox Bounds(Visual.RenderLocation-FVector(Radius),Visual.RenderLocation+FVector(Radius));
	float Near=7200.f, Far=8800.f;
	if (auto* LOD=GetWorld()->GetSubsystem<UGuLiCommanderLODSubsystem>())
	{ Near=LOD->GetSettings().NearEnterDistance; Far=LOD->GetSettings().NearHoldDistance; }
	double DistanceSquared=TNumericLimits<double>::Max();
	for (const FVector& View:BulletLocalViews) DistanceSquared=FMath::Min(DistanceSquared,FVector::DistSquared(View,Visual.RenderLocation));
	if (DistanceSquared>FMath::Square(double(Far))) return;
	BulletTrails->SubmitLight(State.EffectId,Visual.RenderLocation,GroundBulletTint(State),DistanceSquared,IsVisibleBounds(Bounds),Near,Far);
}

