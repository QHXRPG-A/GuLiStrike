#include "Gameplay/Building/GuLiBuildingProductionComponent.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Network/GuLiHybridManifest.h"
#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "Network/GuLiHybridAdapterRegistry.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Pawn.h"
#include "Network/GuLiNetworkSettings.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "Gameplay/Performance/GuLiHotspotOptions.h"
#include "Gameplay/Navigation/Grid/GuLiGroundGridSubsystem.h"
#if !UE_BUILD_SHIPPING
#include "Development/GuLiHybridReviewSubsystem.h"
#endif

UGuLiBuildingProductionComponent::UGuLiBuildingProductionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}
void UGuLiBuildingProductionComponent::BeginPlay()
{
	Super::BeginPlay();
	if (auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>())
	{
        const auto* Binding=UGuLiHybridActorComponent::Find(GetOwner());
        if (!Binding||!Hybrid->SubscribeObjectReceipt(this,Binding->GetObjectKey(),[this](const auto& R){HandleHybridTransaction(R);}))
            Hybrid->OnTransactionCompleted.AddDynamic(this,&ThisClass::HandleHybridTransaction);
    }
}
void UGuLiBuildingProductionComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>())
		{Hybrid->UnsubscribeObjectReceipt(this);Hybrid->OnTransactionCompleted.RemoveDynamic(this,&ThisClass::HandleHybridTransaction);}
	for (const auto& J:HybridJobs) ReleaseGridBirth(J.Target,!J.bSubmitted);
	HybridJobs.Reset(); Super::EndPlay(Reason);
}
void UGuLiBuildingProductionComponent::InitializeProduction()
{
	const auto& Life = *GetOwner()->FindComponentByClass<UGuLiBuildingLifecycleComponent>();
	ObservedTeam = Life.GetTeam();
}
#if !UE_BUILD_SHIPPING
bool UGuLiBuildingProductionComponent::PrepareDevelopmentProductionCycle()
{
    const auto* Life=GetOwner()->FindComponentByClass<UGuLiBuildingLifecycleComponent>();
    if (!Life || !Life->IsCompleted() || !UGuLiHybridActorComponent::HasSimulation(GetOwner())
        || !GuLiResources::IsPlayableTeam(Life->GetTeam()) || !Life->GetDefinition().HasProductionCapability()
        || Life->GetDefinition().ProductionSeconds<=0) return false;
    // Prime only the prerequisite timer. The next ordinary logical advance
    // still performs navigation, admission, payment, population and birth.
    if (HybridJobs.IsEmpty())
    {ObservedTeam=Life->GetTeam();CycleSeconds=Life->GetDefinition().ProductionSeconds;}
    return true;
}
#endif
void UGuLiBuildingProductionComponent::AdvanceHybridProduction(float Dt)
{
	if (!UGuLiNetworkSettings::IsHybrid(GetWorld()) || !UGuLiHybridActorComponent::HasSimulation(GetOwner())
		|| !FMath::IsFinite(Dt) || Dt<=0) return;
	const auto* Binding=UGuLiHybridActorComponent::Find(GetOwner());
	auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
#if !UE_BUILD_SHIPPING
    // Opt-in functional fixture: pause unrelated factories without changing
    // production numbers, population rules, pending receipts or saved state.
    if (const auto* Review=GetWorld()->GetSubsystem<UGuLiHybridReviewSubsystem>();
        Review && !Review->IsProductionEnabled(Binding->GetObjectKey().StableId)) return;
#endif
	const auto& Life=*GetOwner()->FindComponentByClass<UGuLiBuildingLifecycleComponent>();
	const auto& D=Life.GetDefinition();
	if (!D.HasProductionCapability()) return;
	if (ObservedTeam!=Life.GetTeam()) { ObservedTeam=Life.GetTeam(); CycleSeconds=0; }
	bProducing=Life.IsCompleted() && GuLiResources::IsPlayableTeam(ObservedTeam)
		&& (Life.GetState().Origin!=EGuLiBuildingOrigin::Gift || Binding->GetState().bSupplied);
	if (!bProducing) return;
    if (const auto* Grid=GetWorld()->GetSubsystem<UGuLiGroundGridSubsystem>();Grid && Grid->IsReady())
    {AdvanceGridProduction(Dt);return;}
	// A transferred owner state can contain a completed job whose receipt is still in flight.
	HybridJobs.RemoveAll([&](const auto& Job)
	{
		FGuLiHybridObjectState State;
        const bool Found=Hybrid->GetObjectView(Job.Target,State);
        return Found && State.SpawnEvent==Job.Event && State.Parent==Binding->GetObjectKey();
	});
	if (HybridJobs.IsEmpty())
	{
		CycleSeconds+=Dt;
		if (CycleSeconds<D.ProductionSeconds || D.ProductionSeconds<=0 || D.ProductionCount<=0 || D.ProductionCount>256) return;
		CycleSeconds=FMath::Fmod(CycleSeconds,D.ProductionSeconds);
		const auto* Soldier=GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>()->FindSoldierDefinition(D.ProductionUnitId);
		auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		if (!Soldier || !Nav) return;
        const auto* ManifestHost=GetWorld()->GetSubsystem<UGuLiHybridActorWorldSubsystem>();
        FGuLiHybridAdapterFamily Family;FString Error;
        if (!ManifestHost || !FGuLiHybridAdapterRegistry::ResolveDefinition(ManifestHost->GetManifest(),
            FString::Printf(TEXT("unit/%d"),D.ProductionUnitId),Family,Error))
        {Hybrid->Suspend(TEXT("Production definition contract unavailable: ")+Error);return;}
        const auto* Pawn=Soldier->ActorClass?Cast<APawn>(Soldier->ActorClass->GetDefaultObject()):nullptr;
        const auto* Capsule=Pawn?Pawn->FindComponentByClass<UCapsuleComponent>():nullptr;
        const auto* NavData=Pawn?Nav->GetNavDataForProps(Pawn->GetNavAgentPropertiesRef()):Nav->GetNavDataForAgentName(TEXT("CommanderSoldier"));
        if (!NavData || (!Soldier->UsesMass() && !Pawn)) {Hybrid->Suspend(TEXT("Production unit has no navigation carrier"));return;}
		const float Radius=Capsule?Capsule->GetUnscaledCapsuleRadius():Soldier->GetMassAvoidanceRadius(150.f);
        const float HalfHeight=Capsule?Capsule->GetUnscaledCapsuleHalfHeight():Radius;
		const float Spacing=FMath::Max(320.f,Radius*2+20);
		for (int32 Index=0;Index<D.ProductionCount;++Index)
		{
			const FVector Local(D.CollisionExtent.X+800+(Index/6)*Spacing,(Index%6-2.5)*Spacing,0);
			FNavLocation Projected;
			if (!Nav->ProjectPointToNavigation(Life.GetGroundLocation()+GetOwner()->GetActorRotation().RotateVector(Local),
				Projected,FVector(Radius,Radius,5000),NavData)) continue;
			FCollisionQueryParams Query(SCENE_QUERY_STAT(HybridBarracksSpawn),false,GetOwner());
			if (GetWorld()->OverlapBlockingTestByChannel(Projected.Location+FVector(0,0,HalfHeight+1),FQuat::Identity,
				ECC_Pawn,FCollisionShape::MakeCapsule(Radius,HalfHeight),Query)) continue;
			auto& Job=HybridJobs.AddDefaulted_GetRef(); Job.Event=FGuid::NewGuid(); Job.Location=Projected.Location+FVector(0,0,Pawn?HalfHeight:0);
			Job.FacingYaw=GetOwner()->GetActorRotation().Yaw;
			Job.Target=FGuLiHybridManifest::MakeStableKey(GetWorld()->GetOutermost()->GetName(),Family.Kind,
				TEXT("production/")+Job.Event.ToString(EGuidFormats::Digits));
		}
	}
	for (auto& Job : HybridJobs) if (!Job.bSubmitted)
	{
		FGuLiHybridObjectRequest Request; Request.Source=Binding->GetObjectKey(); Request.Target=Job.Target; Request.Event=Job.Event;
		Request.Operation=EGuLiHybridObjectOperation::Spawn; Request.Location=Job.Location; Request.FacingYaw=Job.FacingYaw;
		Request.Rule=FName(*FString::Printf(TEXT("produce/building/%d"),D.DefinitionId));
		Job.bSubmitted=Hybrid->SubmitObjectOperation(Request).IsValid();
	}
}
void UGuLiBuildingProductionComponent::HandleHybridTransaction(const FGuLiHybridTransactionResult& Result)
{
    if (Result.Status==EGuLiHybridTransactionStatus::Deferred || Result.Status==EGuLiHybridTransactionStatus::Paused
        || Result.Status==EGuLiHybridTransactionStatus::Interrupted) return;
	HybridJobs.RemoveAll([&](const auto& Job)
    {
        if (Job.Event!=Result.Event) return false;
        if (const auto* G=GetWorld()->GetSubsystem<UGuLiGroundGridSubsystem>();G && G->IsReady())
        {
            FGuLiHybridObjectState Born;
            if (Result.IsCommitted() && (!GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()->GetObjectView(Job.Target,Born) || Born.LifeState==0)) return false;
            if (!Result.IsCommitted()) {++GridRemaining;GridRetryAt=UGuLiHybridActorComponent::BattleSeconds(GetOwner())+5.+GetTypeHash(Job.Target)%60/60.;}
        }
        ReleaseGridBirth(Job.Target);return true;
    });
}
void UGuLiBuildingProductionComponent::ResetResourceRequests(uint64)
{
    auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    const auto* Binding=UGuLiHybridActorComponent::Find(GetOwner());
    if (!H || !Binding) return;
    const auto* Grid=GetWorld()->GetSubsystem<UGuLiGroundGridSubsystem>();
    const bool bGrid=Grid && Grid->IsReady();
    HybridJobs.RemoveAll([&](const auto& Job)
    {
        FGuLiHybridObjectState State;
        const bool Completed=H->GetObjectView(Job.Target,State) && State.SpawnEvent==Job.Event
            && State.Parent==Binding->GetObjectKey() && (!bGrid || State.LifeState!=0);
        if (Completed && bGrid) ReleaseGridBirth(Job.Target);
        return Completed;
    });
    for (auto& Job:HybridJobs)
    {
        FGuLiHybridObjectState Registered;
        if (bGrid && H->GetObjectView(Job.Target,Registered) && Registered.SpawnEvent==Job.Event
            && Registered.Parent==Binding->GetObjectKey()) continue;
        ReleaseGridBirth(Job.Target);
        Job.bSubmitted=false;Job.Event=FGuid::NewGuid();
        Job.Target=FGuLiHybridManifest::MakeStableKey(GetWorld()->GetOutermost()->GetName(),Job.Target.Kind,
            TEXT("production/")+Job.Event.ToString(EGuidFormats::Digits));
    }
}
