#include "Gameplay/CombatEffects/GuLiCombatEffectReplicationComponent.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectRuntimeSubsystem.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectPresentationSubsystem.h"
#include "Gameplay/CombatEffects/GuLiUnitFeedbackSubsystem.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"
#include "Engine/NetDriver.h"
#include "Engine/NetConnection.h"
#include "Engine/ActorChannel.h"
#include "Battle/Framework/GuLiBattleGameState.h"

void UGuLiCombatEffectReplicationComponent::PublishRogueUpgrade(UWorld* World,const FGuLiRogueUpgradeCue& Cue)
{
	auto* State=World ? World->GetGameState() : nullptr;
	auto* Channel=State ? State->FindComponentByClass<UGuLiCombatEffectReplicationComponent>() : nullptr;
	if (!Channel || !State->HasAuthority() || !Cue.Session.IsValid() || !Cue.MatchEpoch) return;
	for (int32 First=0; First<Cue.Soldiers.Num(); First+=GuLiRogueUpgrade::NetworkBatchSize)
	{
		FGuLiRogueUpgradeCue Batch;
		Batch.Session=Cue.Session; Batch.MatchEpoch=Cue.MatchEpoch; Batch.Team=Cue.Team; Batch.UnitTypeId=Cue.UnitTypeId;
		Batch.System=Cue.System; Batch.Scale=Cue.Scale; Batch.Color=Cue.Color;
		Batch.StartTime=State->GetServerWorldTimeSeconds(); Batch.BatchIndex=First/GuLiRogueUpgrade::NetworkBatchSize;
		Batch.Soldiers.Append(Cue.Soldiers.GetData()+First,FMath::Min(GuLiRogueUpgrade::NetworkBatchSize,Cue.Soldiers.Num()-First));
		Channel->MulticastRogueUpgrade(Batch);
	}
}
void UGuLiCombatEffectReplicationComponent::MulticastRogueUpgrade_Implementation(const FGuLiRogueUpgradeCue& Cue)
{
	if (auto* Visuals=GetWorld()->GetSubsystem<UGuLiCombatEffectPresentationSubsystem>()) Visuals->ApplyRogueUpgrade(Cue);
}

bool UGuLiCombatEffectReplicationComponent::CallRemoteFunction(UFunction* Function, void* Parameters, FOutParmRec* OutParms, FFrame* Stack)
{
	const bool bFreshCue = Function->GetFName()==GET_FUNCTION_NAME_CHECKED(ThisClass,MulticastWingmanFeedback);
	UNetDriver* Driver=GetWorld() ? GetWorld()->GetNetDriver() : nullptr;
	if (!bFreshCue || !GetOwner()->HasAuthority() || !Driver)
		return Super::CallRemoteFunction(Function,Parameters,OutParms,Stack);
	// Default legacy multicast queues until the next Actor property update. Under
	// bursty roster traffic that can exceed a tracer's useful lifetime. Use UE's
	// immediate *unreliable* policy only on established public GameState channels.
	// Respect each connection's budget and drop fresh cosmetics when saturated;
	// never make them reliable, open a channel, or create a second damage path.
	const FClassNetCache* ClassCache=Driver->NetCache->GetClassNetCache(GetClass());
	const FFieldNetCache* FieldCache=ClassCache ? ClassCache->GetFromField(Function) : nullptr;
	if (!FieldCache) return false;
	for (UNetConnection* Connection:Driver->ClientConnections)
	{
		if (!Connection || Connection->GetConnectionState()!=USOCK_Open || !Connection->ViewTarget) continue;
		// IsNetReady() requires a completely empty send budget. Checking it after
		// a synchronized launch wave starves every subsequent cosmetic batch even
		// when average bandwidth is available. Allow at most 100ms / 8KiB of burst
		// debt, then drop; this is bounded and far below the 350ms freshness window.
		const int32 MaximumBurstBits=FMath::Min(Connection->CurrentNetSpeed*8/10,8*8192);
		if (Connection->QueuedBits>MaximumBurstBits) continue;
		UActorChannel* Channel=Connection->FindActorChannelRef(GetOwner());
		if (!Channel || Channel->OpenPacketId.First==INDEX_NONE || Channel->Closing) continue;
		Driver->ProcessRemoteFunctionForChannel(Channel,ClassCache,FieldCache,this,Connection,Function,
			Parameters,OutParms,Stack,true,UNetDriver::ERemoteFunctionSendPolicy::ForceSend);
	}
	return true;
}

UGuLiCombatEffectReplicationComponent::UGuLiCombatEffectReplicationComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.05f;
}

void UGuLiCombatEffectReplicationComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetOwner()->HasAuthority())
	{
		Runtime = GetWorld()->GetSubsystem<UGuLiCombatEffectRuntimeSubsystem>();
		if (Runtime.IsValid())
		{
			Runtime->OnState.AddUObject(this, &ThisClass::HandleState);
			Runtime->OnShots.AddUObject(this, &ThisClass::HandleShots);
			Runtime->OnEpoch.AddUObject(this, &ThisClass::HandleEpoch);
			HandleEpoch(Runtime->GetEffectEpoch());
		}
	}
	else SetComponentTickEnabled(false);
	OnRep_Epoch();
}

void UGuLiCombatEffectReplicationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	WingmanFeedbackQueue.Reset();
	if (Runtime.IsValid()) { Runtime->OnState.RemoveAll(this); Runtime->OnShots.RemoveAll(this); Runtime->OnEpoch.RemoveAll(this); }
	Runtime.Reset(); ReliableQueue.Reset(); ActiveFlights.Reset(); PendingFlights.Reset(); FlightPeers.Reset();
	Super::EndPlay(Reason);
}

void UGuLiCombatEffectReplicationComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UGuLiCombatEffectReplicationComponent, Epoch);
}

bool UGuLiCombatEffectReplicationComponent::IsCurrentEpoch(uint32 MatchEpoch) const
{
	const auto* Battle = GetWorld() ? GetWorld()->GetGameState<AGuLiBattleGameState>() : nullptr;
	return MatchEpoch != 0 && Battle && MatchEpoch == Battle->GetMatchEpoch();
}

void UGuLiCombatEffectReplicationComponent::HandleEpoch(uint32 NewEpoch)
{
	if (!GetOwner()->HasAuthority() || NewEpoch == Epoch || !IsCurrentEpoch(NewEpoch)) return;
	Epoch = NewEpoch;
	WingmanFeedbackQueue.Reset();
	ReliableQueue.Reset(); ActiveFlights.Reset(); PendingFlights.Reset(); FlightPeers.Reset();
	OnRep_Epoch(); GetOwner()->ForceNetUpdate();
}

void UGuLiCombatEffectReplicationComponent::OnRep_Epoch()
{
	if (GetWorld()) if (auto* Visuals = GetWorld()->GetSubsystem<UGuLiCombatEffectPresentationSubsystem>()) Visuals->BeginEpoch(Epoch);
}

void UGuLiCombatEffectReplicationComponent::HandleState(const FGuLiCombatEffectState& State, bool bReliable)
{
    if (!GetOwner()->HasAuthority() || !IsCurrentEpoch(State.MatchEpoch)) return;
    HandleEpoch(State.MatchEpoch);
    if (GuLiFlightWire::IsFlight(State.Kind))
    {
        if (!bReliable) { UpdateFlight(GetWorld(), State); return; }
        FGuLiFlightEvent Event; Event.State = State;
        PublishFlight(GetWorld(), Event);
        return;
    }
    if (bReliable) ReliableQueue.Add(State);
    if (auto* Visuals = GetWorld()->GetSubsystem<UGuLiCombatEffectPresentationSubsystem>()) Visuals->ApplyState(State);
}

void UGuLiCombatEffectReplicationComponent::HandleShots(const TArray<FGuLiCombatShotCue>& Shots)
{
    if (!GetOwner()->HasAuthority()) return;
    for (const auto& Cue : Shots)
    {
        if (ActiveFlights.Contains(Cue.ShotId)) { AttachFlightMuzzle(GetWorld(), Cue); continue; }
        // Legacy instantaneous traces are also a single launch/end pair. They do
        // not create a second damage event or a client damage callback.
        if (Cue.bMuzzleOnly) continue;
        FGuLiFlightEvent Event; auto& State = Event.State;
        State.MatchEpoch = Cue.MatchEpoch; State.EffectId = Cue.ShotId; State.Sequence = 1;
        State.Kind = EGuLiCombatEffectKind::LinearProjectile; State.Source = Cue.Source;
        State.SourceTeam = EGuLiTeam::Red;
        FGuLiCombatTargetSnapshot Source;
        if (auto* Ledger = GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>(); Ledger && Ledger->TryGetSourceSnapshot(Cue.Source, Source)) State.SourceTeam = Source.Team;
        State.Location = State.LaunchLocation = Cue.Start; State.LastTargetLocation = Cue.End;
        State.LaunchDirection = (FVector(Cue.End)-FVector(Cue.Start)).GetSafeNormal();
        if (FVector(State.LaunchDirection).IsNearlyZero()) State.LaunchDirection = FVector::ForwardVector;
        State.StartTime = State.SampleTime = State.ActivationTime = Cue.ServerTime;
        State.EndTime = Cue.ServerTime + .05f;
        State.Motion.Speed = FMath::Clamp(float(FVector::Distance(Cue.Start,Cue.End))/.05f, 1.f, 1000000.f);
        State.Velocity = FVector(State.LaunchDirection)*State.Motion.Speed;
        Event.Muzzle = Cue; Event.bHasMuzzle = true;
        PublishFlight(GetWorld(), Event);
        State.Sequence = 2; State.Phase = EGuLiCombatEffectPhase::Finished;
        State.EndReason = EGuLiCombatEffectEndReason::Impact; State.Location = Cue.End;
        Event.bHasMuzzle = false;
        PublishFlight(GetWorld(), Event);
    }
}

void UGuLiCombatEffectReplicationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaTime, TickType, TickFunction);
    if (!GetOwner()->HasAuthority()) return;
    for (int32 Index=0; Index<WingmanFeedbackQueue.Num(); Index+=16)
        MulticastWingmanFeedback(TArray<FGuLiWingmanFeedbackCue>(WingmanFeedbackQueue.GetData()+Index,FMath::Min(16,WingmanFeedbackQueue.Num()-Index)));
    WingmanFeedbackQueue.Reset();
    for (int32 Index=0; Index<ReliableQueue.Num(); Index+=16)
        MulticastReliableStates(TArray<FGuLiCombatEffectState>(ReliableQueue.GetData()+Index,FMath::Min(16,ReliableQueue.Num()-Index)));
    ReliableQueue.Reset();
    FlushFlightStreams();
}

void UGuLiCombatEffectReplicationComponent::PublishWingmanFeedback(UWorld* World,
	const FGuLiWingmanHandle& Wingman, const FVector& Location, bool bDestroyed, uint16 HealthPermille)
{
	if (!World || World->GetNetMode() == NM_Client || !Wingman.IsValid() || Location.ContainsNaN() || HealthPermille > 1000) return;
	if (auto* Visuals = World->GetSubsystem<UGuLiUnitFeedbackSubsystem>())
		Visuals->ApplyWingmanFeedback(Wingman, Location, bDestroyed, HealthPermille / 1000.0f);
	AGameStateBase* State = World->GetGameState();
	auto* Replication = State ? State->FindComponentByClass<UGuLiCombatEffectReplicationComponent>() : nullptr;
	if (!Replication) return;
	auto* Pending = Replication->WingmanFeedbackQueue.FindByPredicate([&](const auto& Cue) { return Cue.Wingman == Wingman; });
	if (!Pending)
	{
		if (Replication->WingmanFeedbackQueue.Num() >= 256) return;
		Pending = &Replication->WingmanFeedbackQueue.AddDefaulted_GetRef();
		Pending->Wingman = Wingman;
	}
	Pending->Location = Location;
	Pending->ServerTime = World->GetTimeSeconds();
	Pending->bDestroyed |= bDestroyed;
	Pending->HealthPermille = Pending->bDestroyed ? 0 : HealthPermille;
}

void UGuLiCombatEffectReplicationComponent::MulticastWingmanFeedback_Implementation(const TArray<FGuLiWingmanFeedbackCue>& Cues)
{
	if (GetOwner()->HasAuthority()) return;
	const AGameStateBase* State = GetWorld()->GetGameState();
	const float Now = State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	if (auto* Visuals = GetWorld()->GetSubsystem<UGuLiUnitFeedbackSubsystem>())
		for (const auto& Cue : Cues)
			if (Now - Cue.ServerTime <= 1.0f && Cue.ServerTime - Now <= 1.0f && Cue.HealthPermille <= 1000)
				Visuals->ApplyWingmanFeedback(Cue.Wingman, FVector(Cue.Location), Cue.bDestroyed, Cue.HealthPermille / 1000.0f);
}

void UGuLiCombatEffectReplicationComponent::MulticastActiveSnapshot_Implementation(const TArray<FGuLiCombatEffectState>& States)
{
	if (GetOwner()->HasAuthority()) return;
	if (auto* Visuals=GetWorld()->GetSubsystem<UGuLiCombatEffectPresentationSubsystem>())
		for (const auto& State:States) Visuals->ApplyState(State,true);
}

void UGuLiCombatEffectReplicationComponent::ApplyStates(const TArray<FGuLiCombatEffectState>& States)
{
	if (GetOwner()->HasAuthority()) return;
	if (auto* Visuals = GetWorld()->GetSubsystem<UGuLiCombatEffectPresentationSubsystem>()) for (const auto& State : States) Visuals->ApplyState(State);
}

void UGuLiCombatEffectReplicationComponent::MulticastReliableStates_Implementation(const TArray<FGuLiCombatEffectState>& States) { ApplyStates(States); }
