#include "Gameplay/Stronghold/GuLiStrongholdTransitPresentationComponent.h"
#include "Gameplay/Data/GuLiSpellFieldDataSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"

UGuLiStrongholdTransitPresentationComponent::UGuLiStrongholdTransitPresentationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}
double UGuLiStrongholdTransitPresentationComponent::ServerTime() const
{
	const auto* GS = GetWorld()->GetGameState();
	return GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}
void UGuLiStrongholdTransitPresentationComponent::ApplyState(const FGuLiStrongholdTransitState& InState)
{
	check(GetNetMode() != NM_DedicatedServer);
	const bool bNewRoute = State.JourneyId != InState.JourneyId || State.StartServerTime != InState.StartServerTime;
	State = InState;
	if (!Orb)
	{
		const auto& Config = *GetWorld()->GetSubsystem<UGuLiSpellFieldDataSubsystem>()->FindStrongholdGate(State.GateFieldId);
		Orb = NewObject<UStaticMeshComponent>(GetOwner());
		Orb->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere")));
		Orb->SetMaterial(0,Config.EnergyMaterial.LoadSynchronous());
		Orb->SetCollisionEnabled(ECollisionEnabled::NoCollision); Orb->SetCanEverAffectNavigation(false); Orb->SetCastShadow(false);
		Orb->SetWorldScale3D(FVector(10)); Orb->RegisterComponent();
		Trail = NewObject<UNiagaraComponent>(GetOwner());
		Trail->SetAutoActivate(false); Trail->SetAsset(Config.TrailSystem.LoadSynchronous()); Trail->RegisterComponent();
		Trail->SetVariableFloat(TEXT("User.Throttle"),1);
		Flash = NewObject<UNiagaraComponent>(GetOwner());
		Flash->SetAutoActivate(false); Flash->SetAsset(Config.FlashSystem.LoadSynchronous());
		Flash->SetAgeUpdateMode(ENiagaraAgeUpdateMode::DesiredAge); Flash->RegisterComponent();
	}
	if (bNewRoute) Trail->DeactivateImmediate();
	SetComponentTickEnabled(true); UpdatePresentation();
}
void UGuLiStrongholdTransitPresentationComponent::UpdatePresentation()
{
	const double Now = ServerTime();
	Orb->SetVisibility(State.IsPhased());
	if (State.IsPhased())
	{
		const FVector Location = State.SamplePosition(Now);
		const FVector Ahead = State.SamplePosition(Now+.01);
		Orb->SetWorldLocation(Location);
		Trail->SetWorldLocationAndRotation(Location,(Ahead-Location).Rotation());
		const FRotationMatrix Direction((Ahead-Location).Rotation());
		Trail->SetVariableVec3(TEXT("User.Forward"),Direction.GetScaledAxis(EAxis::X));
		Trail->SetVariableVec3(TEXT("User.Right"),Direction.GetScaledAxis(EAxis::Y));
		if (!Trail->IsActive()) Trail->Activate(true);
	}
	else Trail->Deactivate();
	const double FlashAge = Now-State.ExitServerTime;
	if (State.Phase == EGuLiTransitPhase::ExitFlash && FlashAge >= 0 && FlashAge < State.FlashSeconds)
	{
		Flash->SetWorldLocation(State.ExitPosition);
		if (FlashJourney != State.JourneyId || FlashStart != State.ExitServerTime)
		{
			FlashJourney = State.JourneyId; FlashStart = State.ExitServerTime;
			Flash->Activate(true);
		}
		Flash->SetDesiredAge(FlashAge);
	}
	else Flash->DeactivateImmediate();
	if (!State.IsPhased() && (State.Phase != EGuLiTransitPhase::ExitFlash || FlashAge >= State.FlashSeconds))
		SetComponentTickEnabled(false);
}
void UGuLiStrongholdTransitPresentationComponent::TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* Function)
{ Super::TickComponent(Dt,TickType,Function); UpdatePresentation(); }
void UGuLiStrongholdTransitPresentationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Orb) Orb->DestroyComponent();
	if (Trail) Trail->DestroyComponent();
	if (Flash) Flash->DestroyComponent();
	Super::EndPlay(Reason);
}
