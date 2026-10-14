#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Gameplay/Building/GuLiBuildingRegistrySubsystem.h"
#include "Gameplay/Building/GuLiBuildingCatalog.h"
#include "Gameplay/Building/GuLiBuildingConstructionVisualComponent.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Network/GuLiNetworkSettings.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

UGuLiBuildingLifecycleComponent::UGuLiBuildingLifecycleComponent()
{
	SetIsReplicatedByDefault(false);
	PrimaryComponentTick.bCanEverTick=false;
	PrimaryComponentTick.bStartWithTickEnabled=false;
	PrimaryComponentTick.TickInterval=.1f;
}
void UGuLiBuildingLifecycleComponent::BeginPlay()
{
	Super::BeginPlay();
	// Local presentation is automatic for every lifecycle owner, including future Actor classes.
	if (UGuLiNetworkSettings::CanPresent(GetWorld())
		&& !GetOwner()->FindComponentByClass<UGuLiBuildingConstructionVisualComponent>())
	{
		auto* Visual = NewObject<UGuLiBuildingConstructionVisualComponent>(GetOwner(), NAME_None, RF_Transient);
		GetOwner()->AddInstanceComponent(Visual);
		Visual->RegisterComponent();
	}
	if (GetOwner()->HasAuthority())
		if (auto* Health = GetOwner()->FindComponentByClass<UGuLiCombatHealthComponent>())
			Health->OnDeath.AddDynamic(this, &ThisClass::HandleDeath);
	if (State.InstanceId) RegisterInstance();
	if (auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>())
		Hybrid->OnTransactionCompleted.AddDynamic(this,&ThisClass::HandleHybridTransaction);
}
void UGuLiBuildingLifecycleComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (auto* Registry = GetWorld()->GetSubsystem<UGuLiBuildingRegistrySubsystem>()) Registry->Unregister(*this);
	if (auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()) Hybrid->OnTransactionCompleted.RemoveDynamic(this,&ThisClass::HandleHybridTransaction);
	Super::EndPlay(Reason);
}

const FGuLiBuildingDefinition& UGuLiBuildingLifecycleComponent::GetDefinition() const
{
	const auto* Definition = UGuLiBuildingCatalog::LoadDefaultCatalog()->FindById(State.DefinitionId);
	check(Definition);
	return *Definition;
}
EGuLiTeam UGuLiBuildingLifecycleComponent::GetTeam() const
{
	return CastChecked<IGuLiBuildingOwner>(GetOwner())->GetBuildingTeam();
}
FVector UGuLiBuildingLifecycleComponent::GetGroundLocation() const
{
	return CastChecked<IGuLiBuildingOwner>(GetOwner())->GetBuildingGroundLocation();
}
void UGuLiBuildingLifecycleComponent::InitializeBuilding(int32 DefinitionId, int32 TerritoryIndex,
	EGuLiBuildingOrigin Origin, bool bCompleted, const FGuid& Builder)
{
	check(GetOwner()->HasAuthority() && State.InstanceId == 0);
	const auto* Binding=UGuLiHybridActorComponent::Find(GetOwner());
	State.InstanceId = Binding ? Binding->GetState().Ordinal : GetWorld()->GetSubsystem<UGuLiBuildingRegistrySubsystem>()->AllocateInstanceId();
	check(State.InstanceId);
	State.DefinitionId = DefinitionId; State.TerritoryIndex = TerritoryIndex; State.Origin = Origin;
	State.BuilderGuid = Builder;
	State.Phase = bCompleted ? EGuLiBuildingPhase::Completed : EGuLiBuildingPhase::UnderConstruction;
	State.WorkDone = bCompleted ? GetDefinition().ConstructionWork : 0;

	if (GetDefinition().Category == EGuLiBuildingCategory::Stronghold) GetOwner()->SetCanBeDamaged(false);
	if (HasBegunPlay()) RegisterInstance();
	OnConstructionStateChanged.Broadcast();

}
void UGuLiBuildingLifecycleComponent::RegisterInstance()
{
	if (State.Phase == EGuLiBuildingPhase::Destroyed || State.Phase == EGuLiBuildingPhase::ConvertedToUnit) return;
	GetWorld()->GetSubsystem<UGuLiBuildingRegistrySubsystem>()->Register(*this);
}
float UGuLiBuildingLifecycleComponent::GetConstructionProgress() const
{
	if (!State.InstanceId) return 0.0f;
	if (State.Phase == EGuLiBuildingPhase::Completed) return 1.0f;
	const float Required = GetDefinition().ConstructionWork;
	return Required > 0.0f ? FMath::Clamp(State.WorkDone / Required, 0.0f, 1.0f) : 0.0f;
}
void UGuLiBuildingLifecycleComponent::OnRep_State()
{
	if (!State.InstanceId) return; // Initial component replication precedes its authored identity.
	if (State.Phase == EGuLiBuildingPhase::Destroyed || State.Phase == EGuLiBuildingPhase::ConvertedToUnit)
		GetWorld()->GetSubsystem<UGuLiBuildingRegistrySubsystem>()->Unregister(*this);
	else RegisterInstance();
	OnConstructionStateChanged.Broadcast();
}
void UGuLiBuildingLifecycleComponent::AddConstructionWork(float Work)
{
	check(GetOwner()->HasAuthority() && Work >= 0);
	if (!UGuLiHybridActorComponent::HasSimulation(GetOwner())) return;
	if (State.Phase != EGuLiBuildingPhase::UnderConstruction) return;
	State.WorkDone = FMath::Min(State.WorkDone + Work, GetDefinition().ConstructionWork);
	if (UGuLiNetworkSettings::IsHybrid(GetWorld()))
	{
		// Work is a simulation fact. The formal completed state waits for its ledger receipt.
		OnConstructionStateChanged.Broadcast();
		return;
	}
	if (State.WorkDone >= GetDefinition().ConstructionWork)
	{
		State.Phase = EGuLiBuildingPhase::Completed;
		ActiveContributors.Reset(); State.bHasActiveBuilders = false;
		ConstructionSlots.Reset(); ++SlotGeneration; bSlotsPending=false;
		GetWorld()->GetSubsystem<UGuLiBuildingRegistrySubsystem>()->OnCompleted.Broadcast(*this);
	}
	OnConstructionStateChanged.Broadcast();

}
void UGuLiBuildingLifecycleComponent::ApplyHybridState(const FGuLiHybridObjectState& Record,bool Effects)
{
	if (!UGuLiNetworkSettings::IsHybrid(GetWorld()) || !State.InstanceId || State.InstanceId!=Record.Ordinal) return;
	const auto Before=State.Phase;
	const auto* Replacement=Record.Capabilities.Find(TEXT("replacement"));
	const bool Converted=Record.LifeState==3 && Replacement && !Replacement->Data.IsEmpty();
	State.Phase=Converted ? EGuLiBuildingPhase::ConvertedToUnit : Record.LifeState>=2 ? EGuLiBuildingPhase::Destroyed
		: Record.bCompleted ? EGuLiBuildingPhase::Completed : EGuLiBuildingPhase::UnderConstruction;
	if (State.Phase!=EGuLiBuildingPhase::UnderConstruction)
	{
		HybridCompletionEvent.Invalidate(); PendingConstructionWork=0; ActiveContributors.Reset(); State.bHasActiveBuilders=false;
		if (!ConstructionSlots.IsEmpty() || bSlotsPending || Before==EGuLiBuildingPhase::UnderConstruction)
		{ ConstructionSlots.Reset(); ++SlotGeneration; bSlotsPending=false; }
		if (State.Phase==EGuLiBuildingPhase::Completed) State.WorkDone=GetDefinition().ConstructionWork;
	}
	SetComponentTickEnabled(false);
	auto* Registry=GetWorld()->GetSubsystem<UGuLiBuildingRegistrySubsystem>();
	if (State.Phase==EGuLiBuildingPhase::Destroyed || Converted)
	{
		Registry->Unregister(*this); GetOwner()->SetActorEnableCollision(false); GetOwner()->SetActorHiddenInGame(true);
		if (Effects && !Converted && Before!=State.Phase) Registry->OnDestroyed.Broadcast(*this);
	}
	else
	{
		RegisterInstance(); GetOwner()->SetActorHiddenInGame(false); GetOwner()->SetActorEnableCollision(true);
		if (Effects && Before!=State.Phase && State.Phase==EGuLiBuildingPhase::Completed) Registry->OnCompleted.Broadcast(*this);
	}
	if (Before!=State.Phase) OnConstructionStateChanged.Broadcast();
}
void UGuLiBuildingLifecycleComponent::AdvanceHybridConstruction(uint64 LogicalTick)
{
	if (LogicalTick%6 || !UGuLiNetworkSettings::IsHybrid(GetWorld()) || !UGuLiHybridActorComponent::HasSimulation(GetOwner())
		|| State.Phase!=EGuLiBuildingPhase::UnderConstruction) return;
	const float Work=PendingConstructionWork; PendingConstructionWork=0;
	if (Work>0) AddConstructionWork(Work);
	if (State.WorkDone<GetDefinition().ConstructionWork || HybridCompletionEvent.IsValid()) return;
	const auto* Binding=UGuLiHybridActorComponent::Find(GetOwner());
	auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
	FGuLiHybridObjectRequest Request; Request.Source=Binding->GetObjectKey(); Request.Target=Request.Source;
	Request.Operation=EGuLiHybridObjectOperation::Complete;
	Request.Rule=FName(*FString::Printf(TEXT("complete/building/%d"),State.DefinitionId));
	Request.ExpectedRelationRevision=static_cast<int64>(Binding->GetState().RelationRevision);
	HybridCompletionEvent=Hybrid->SubmitObjectOperation(Request);
}
void UGuLiBuildingLifecycleComponent::HandleHybridTransaction(const FGuLiHybridTransactionResult& Result)
{
	if (Result.Event==HybridCompletionEvent && Result.Status!=EGuLiHybridTransactionStatus::Deferred
		&& Result.Status!=EGuLiHybridTransactionStatus::Paused) HybridCompletionEvent.Invalidate();
}
void UGuLiBuildingLifecycleComponent::RefreshTeam()
{
    OnConstructionStateChanged.Broadcast();
}
void UGuLiBuildingLifecycleComponent::MarkConvertedToUnit()
{
    if (State.Phase != EGuLiBuildingPhase::Completed) return;
    State.Phase = EGuLiBuildingPhase::ConvertedToUnit;
    GetWorld()->GetSubsystem<UGuLiBuildingRegistrySubsystem>()->Unregister(*this);
    OnConstructionStateChanged.Broadcast();
}

void UGuLiBuildingLifecycleComponent::HandleDeath()
{
	if (State.Phase == EGuLiBuildingPhase::Destroyed || State.Phase == EGuLiBuildingPhase::ConvertedToUnit) return;
	State.Phase = EGuLiBuildingPhase::Destroyed;
	ActiveContributors.Reset(); State.bHasActiveBuilders = false;
	ConstructionSlots.Reset(); ++SlotGeneration; bSlotsPending=false; PendingConstructionWork=0; SetComponentTickEnabled(false);
	OnConstructionStateChanged.Broadcast();
	auto& Registry = *GetWorld()->GetSubsystem<UGuLiBuildingRegistrySubsystem>();
	Registry.OnDestroyed.Broadcast(*this); Registry.Unregister(*this);
	GetOwner()->SetActorEnableCollision(false);
	GetOwner()->SetActorHiddenInGame(true);

}
