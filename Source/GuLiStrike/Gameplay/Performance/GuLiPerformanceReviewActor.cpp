#include "Gameplay/Performance/GuLiPerformanceReviewActor.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectPresentationSubsystem.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "Gameplay/Vfx/GuLiClientPresentationPolicy.h"
#include "Commander/Presentation/GuLiCommanderLODSubsystem.h"

AGuLiPerformanceReviewActor::AGuLiPerformanceReviewActor()
{
	PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.bStartWithTickEnabled=false;
	PrimaryActorTick.TickGroup=TG_PostUpdateWork;
	Effect=CreateDefaultSubobject<UNiagaraComponent>(TEXT("ReviewEffect")); SetRootComponent(Effect);
	Effect->bAutoActivate=false; Effect->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Effect->SetCastShadow(false); bReplicates=false;
	Tags.Add(TEXT("GuLiPerformanceReview"));
}
void AGuLiPerformanceReviewActor::StartComparison()
{
	if (!GetWorld() || GetNetMode()==NM_DedicatedServer) return;
	StopComparison();
	if (bCatalogImpacts || bCatalogMuzzles) { Elapsed=0; PendingImpactEvents=0; if (!bCatalogMuzzles) ImpactSerial=0; bActive=true; SetActorTickEnabled(true); return; }
	auto* Loaded=System.LoadSynchronous(); if (!Loaded) return;
	Effect->SetAsset(Loaded); Effect->SetWorldScale3D(BaseScale); Elapsed=0; bActive=true;
	bPolicyVisible=true; PolicyDetail=0; PolicyNextCheck=0; PolicyOffscreenSince=-1; PolicyLastTransition=0;
	Effect->SetVariablePosition(TEXT("User.Beam End"),GetActorLocation()+RelativeBeamEnd);
	Effect->Activate(true);
 const int32 Count=FMath::Clamp(EffectCount,1,128);
 while (AdditionalEffects.Num()<Count-1)
 {
  auto* C=NewObject<UNiagaraComponent>(this); C->bAutoActivate=false;
  C->SetCollisionEnabled(ECollisionEnabled::NoCollision); C->SetCastShadow(false);
  C->SetupAttachment(Effect); C->RegisterComponent(); AdditionalEffects.Add(C);
 }
 for (int32 I=0; I<Count-1; ++I)
 {
  auto* C=AdditionalEffects[I].Get(); C->SetAsset(Loaded); C->SetWorldScale3D(BaseScale);
  C->SetWorldLocation(GetActorLocation()+FVector((I%8)*35,(I/8)*60,0));
  C->SetVariablePosition(TEXT("User.Beam End"),C->GetComponentLocation()+RelativeBeamEnd); C->Activate(true);
 }
 SetActorTickEnabled(true);
 UpdatePolicy();
}

void AGuLiPerformanceReviewActor::UpdatePolicy()
{
	if (PolicyEffectId<=0 || bCatalogImpacts || bCatalogMuzzles || !GetWorld()) return;
	const double Now=GetWorld()->GetTimeSeconds(); if (Now<PolicyNextCheck) return; PolicyNextCheck=Now+.05;
	FBox Bounds(ForceInit); Bounds+=GetActorLocation();
	Bounds+=GetActorLocation()+FVector(7*35,15*60,0);
	if (bLaser) Bounds+=GetActorLocation()+RelativeBeamEnd;
	FVector Extent(bLaser ? 600 : 1400);
	Bounds=Bounds.ExpandBy(Extent);
	FGuLiCommanderLODQuery Query; Query.Bounds=Bounds; Query.CurrentLevel=EGuLiCommanderLODLevel(PolicyDetail);
	Query.LastChangeWorldSeconds=PolicyLastTransition;
	auto* LOD=GetWorld()->GetSubsystem<UGuLiCommanderLODSubsystem>(); if (!LOD) return;
	const auto Decision=LOD->EvaluateWorldEffectBounds(Query,20000);
	if (Decision.bCanTransition) { PolicyDetail=uint8(Decision.TargetLevel); PolicyLastTransition=Now; }
	if (Decision.bVisible || !GuLiClientPresentation::OffscreenLifecycleEnabled()) PolicyOffscreenSince=-1;
	else if (PolicyOffscreenSince<0) PolicyOffscreenSince=Now;
	const bool bVisible=PolicyOffscreenSince<0 || Now-PolicyOffscreenSince<GuLiClientPresentation::OffscreenGraceSeconds;
	auto Apply=[&](UNiagaraComponent* C)
	{
		if (!C) return;
		if (bLaser) GuLiClientPresentation::ApplyEndpointDetail(C,EGuLiCommanderLODLevel(PolicyDetail));
		else if (auto* Registry=GetWorld()->GetSubsystem<UGuLiVfxRegistrySubsystem>())
			if (auto* Resource=Registry->LoadNiagaraForLOD(PolicyEffectId,EGuLiCommanderLODLevel(PolicyDetail),false,false))
				if (C->GetAsset()!=Resource) { C->SetAsset(Resource); if (bVisible && bActive) C->Activate(true); }
		if (!bVisible && bPolicyVisible) C->DeactivateImmediate();
		else if (bVisible && !bPolicyVisible && bLaser && bActive) C->Activate(true);
	};
	Apply(Effect); for (int32 I=0;I<FMath::Min(AdditionalEffects.Num(),EffectCount-1);++I) Apply(AdditionalEffects[I]);
	bPolicyVisible=bVisible;
}
void AGuLiPerformanceReviewActor::StopComparison()
{ Effect->DeactivateImmediate(); for (const auto& C:AdditionalEffects) if (C) C->DeactivateImmediate(); SetActorTickEnabled(false); bActive=false; }
void AGuLiPerformanceReviewActor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds); Elapsed+=DeltaSeconds;
	UpdatePolicy();
	if (bCatalogMuzzles)
	{
#if WITH_EDITOR
		if (Elapsed>=FMath::Max(CycleSeconds,ActiveSeconds+.01f)) Elapsed=FMath::Fmod(Elapsed,FMath::Max(CycleSeconds,ActiveSeconds+.01f));
		if (Elapsed<ActiveSeconds)
		{
			PendingImpactEvents+=FMath::Max(0.f,ImpactEventsPerSecond)*DeltaSeconds;
			const int32 Count=FMath::FloorToInt(PendingImpactEvents);
			if (auto* FX=GetWorld()->GetSubsystem<UGuLiCombatEffectPresentationSubsystem>())
				for (int32 I=0;I<Count;++I)
				{
					const int32 Identity=int32((GetUniqueID()%100000)*10000+(++ImpactSerial)%10000+1);
					const FVector Offset((ImpactSerial%8)*80,((ImpactSerial/8)%4)*120,0);
					FX->EmitReviewMuzzleInput(Identity,GetActorLocation()+Offset,GetActorRotation(),bHeavyMuzzle,MuzzleMode,MuzzleFollowVelocity);
				}
			PendingImpactEvents-=Count;
		}
#endif
		return;
	}
	if (bCatalogImpacts)
	{
#if WITH_EDITOR
		PendingImpactEvents+=FMath::Max(0.f,ImpactEventsPerSecond)*DeltaSeconds;
		const int32 Count=FMath::Min(4096,FMath::FloorToInt(PendingImpactEvents));
		if (Count>0)
		{
			if (auto* FX=GetWorld()->GetSubsystem<UGuLiCombatEffectPresentationSubsystem>()) FX->EmitReviewImpacts(GetActorLocation(),Count,ImpactSerial);
			ImpactSerial+=Count; PendingImpactEvents-=Count;
		}
#endif
		return;
	}
	if (Elapsed>=FMath::Max(CycleSeconds,ActiveSeconds+.01f))
	{ Elapsed=FMath::Fmod(Elapsed,FMath::Max(CycleSeconds,ActiveSeconds+.01f)); bActive=true; if (bPolicyVisible) { Effect->Activate(true); for (int32 I=0; I<FMath::Min(AdditionalEffects.Num(),FMath::Clamp(EffectCount,1,128)-1); ++I) AdditionalEffects[I]->Activate(true); } }
	if (Elapsed>=ActiveSeconds && bActive) { Effect->DeactivateImmediate(); for (const auto& C:AdditionalEffects) C->DeactivateImmediate(); bActive=false; }
	if (bActive && bLaser && bPolicyVisible)
 {
  Effect->SetVariablePosition(TEXT("User.Beam End"),GetActorLocation()+RelativeBeamEnd);
  for (int32 I=0; I<FMath::Min(AdditionalEffects.Num(),FMath::Clamp(EffectCount,1,128)-1); ++I)
   AdditionalEffects[I]->SetVariablePosition(TEXT("User.Beam End"),AdditionalEffects[I]->GetComponentLocation()+RelativeBeamEnd);
 }
}
void AGuLiPerformanceReviewActor::EndPlay(const EEndPlayReason::Type Reason)
{ StopComparison(); Super::EndPlay(Reason); }

static FAutoConsoleCommandWithWorldAndArgs ReviewCommand(TEXT("gs.Perf.Review"),
	TEXT("gs.Perf.Review start|stop [actor-name-prefix]. Opt-in saved visual comparison, per client World."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* World)
	{
		if (!World || Args.IsEmpty()) return;
		for (TActorIterator<AGuLiPerformanceReviewActor> It(World); It; ++It)
		{
			if (Args.Num()>1 && !It->GetName().StartsWith(Args[1]) && !It->GetActorNameOrLabel().StartsWith(Args[1])) continue;
			if (Args[0].Equals(TEXT("start"),ESearchCase::IgnoreCase)) It->StartComparison();
			else if (Args[0].Equals(TEXT("stop"),ESearchCase::IgnoreCase)) It->StopComparison();
		}
	}));
