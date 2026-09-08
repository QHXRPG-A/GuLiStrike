#include "Gameplay/CombatEffects/GuLiCombatEffectReplicationComponent.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectRuntimeSubsystem.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectPresentationSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"
#include "Engine/NetDriver.h"
#include "Engine/NetConnection.h"
#include "Engine/ActorChannel.h"

bool UGuLiCombatEffectReplicationComponent::CallRemoteFunction(UFunction* Function, void* Parameters, FOutParmRec* OutParms, FFrame* Stack)
{
	const bool bFreshCue = Function->GetFName()==GET_FUNCTION_NAME_CHECKED(ThisClass,MulticastShots)
		|| Function->GetFName()==GET_FUNCTION_NAME_CHECKED(ThisClass,MulticastCorrections);
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
	if (Runtime.IsValid()) { Runtime->OnState.RemoveAll(this); Runtime->OnShots.RemoveAll(this); Runtime->OnEpoch.RemoveAll(this); }
	Runtime.Reset(); ReliableQueue.Reset(); Corrections.Reset(); ShotQueue.Reset(); SnapshotQueue.Reset();
	Super::EndPlay(Reason);
}

void UGuLiCombatEffectReplicationComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UGuLiCombatEffectReplicationComponent, Epoch);
}

void UGuLiCombatEffectReplicationComponent::HandleEpoch(uint32 NewEpoch)
{
	if (!GetOwner()->HasAuthority() || NewEpoch == 0 || NewEpoch == Epoch) return;
	Epoch = NewEpoch;
	ReliableQueue.Reset(); Corrections.Reset(); ShotQueue.Reset(); SnapshotQueue.Reset(); SnapshotCursor = 0; SnapshotAccumulator = 0;
	OnRep_Epoch(); GetOwner()->ForceNetUpdate();
}

void UGuLiCombatEffectReplicationComponent::OnRep_Epoch()
{
	if (GetWorld()) if (auto* Visuals = GetWorld()->GetSubsystem<UGuLiCombatEffectPresentationSubsystem>()) Visuals->BeginEpoch(Epoch);
}

void UGuLiCombatEffectReplicationComponent::HandleState(const FGuLiCombatEffectState& State, bool bReliable)
{
	if (!GetOwner()->HasAuthority()) return;
	HandleEpoch(State.MatchEpoch);
	if (bReliable)
	{
		// Instant fields enter their harmless visual tail in the creation tick.
		// One complete latest non-terminal record preserves that burst without
		// transmitting its entire creation payload twice. Never coalesce the end.
		FGuLiCombatEffectState* Pending = State.Phase != EGuLiCombatEffectPhase::Finished
			? ReliableQueue.FindByPredicate([&](const auto& Item) { return Item.EffectId==State.EffectId && Item.Phase!=EGuLiCombatEffectPhase::Finished; }) : nullptr;
		if (Pending) *Pending=State; else ReliableQueue.Add(State);
		if (State.Phase == EGuLiCombatEffectPhase::Finished) Corrections.Remove(State.EffectId);
	}
	else Corrections.Add(State.EffectId, State);
	// Listen presentation consumes immediately; multicast application below runs on remote clients only.
	if (auto* Visuals = GetWorld()->GetSubsystem<UGuLiCombatEffectPresentationSubsystem>()) Visuals->ApplyState(State);
}

void UGuLiCombatEffectReplicationComponent::HandleShots(const TArray<FGuLiCombatShotCue>& Shots)
{
	if (!GetOwner()->HasAuthority()) return;
	ShotQueue.Append(Shots);
	if (auto* Visuals = GetWorld()->GetSubsystem<UGuLiCombatEffectPresentationSubsystem>()) Visuals->ApplyShots(Shots);
}

void UGuLiCombatEffectReplicationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	if (!GetOwner()->HasAuthority()) return;
	// Unreliable multicast RPCs are queued until the owning actor's next net update.
	// Request a timely actor update, but keep the payload below the connection
	// budget too: ForceNetUpdate cannot cure bandwidth saturation.
	if (!ReliableQueue.IsEmpty() || !Corrections.IsEmpty() || !ShotQueue.IsEmpty()
		|| SnapshotCursor < SnapshotQueue.Num()) GetOwner()->ForceNetUpdate();
	// Short-lived gunfire gets the first part of the bounded cosmetic budget.
	for (int32 Index = 0; Index < ShotQueue.Num(); Index += 16)
	{
		const TArray<FGuLiCombatShotCue> Batch(ShotQueue.GetData() + Index, FMath::Min(16, ShotQueue.Num() - Index));
		MulticastShots(Batch);
	}
	ShotQueue.Reset();
	// Bounded batches avoid an RPC per particle, per muzzle component, or per correction.
	for (int32 Index = 0; Index < ReliableQueue.Num(); Index += 16)
	{
		const TArray<FGuLiCombatEffectState> Batch(ReliableQueue.GetData() + Index, FMath::Min(16, ReliableQueue.Num() - Index));
		MulticastReliableStates(Batch);
	}
	ReliableQueue.Reset();
	TArray<FGuLiCombatEffectState> CorrectionBatch; Corrections.GenerateValueArray(CorrectionBatch); Corrections.Reset();
	for (int32 Index = 0; Index < CorrectionBatch.Num(); Index += 16)
	{
		TArray<FGuLiCombatEffectCorrection> Batch;
		for (int32 Offset=0; Offset<FMath::Min(16,CorrectionBatch.Num()-Index); ++Offset)
		{
			const auto& State=CorrectionBatch[Index+Offset];
			auto& Correction=Batch.AddDefaulted_GetRef();
			Correction.MatchEpoch=State.MatchEpoch; Correction.EffectId=State.EffectId; Correction.Sequence=State.Sequence;
			Correction.Location=State.Location; Correction.Velocity=State.Velocity;
			Correction.LastTargetLocation=State.LastTargetLocation; Correction.SampleTime=State.SampleTime;
		}
		MulticastCorrections(Batch);
	}
	// A single FastArray property exceeded UE's 64-KiB actor bunch limit in the real 500-unit fixture.
	// Stream reliable live snapshots in bounded RPCs, at most two 16-item batches per network tick.
	// Existing peers deduplicate by sequence; late peers suppress historical activation bursts.
	SnapshotAccumulator += DeltaTime;
	if (Runtime.IsValid() && SnapshotAccumulator >= 1.0f && SnapshotCursor >= SnapshotQueue.Num())
	{
		SnapshotAccumulator = 0; SnapshotCursor = 0;
		Runtime->BuildActiveSnapshot(SnapshotQueue);
		SnapshotQueue.RemoveAll([](const auto& State) { return State.Phase == EGuLiCombatEffectPhase::Dissipating; });
	}
	for (int32 BatchIndex=0; BatchIndex<2 && SnapshotCursor<SnapshotQueue.Num(); ++BatchIndex)
	{
		const int32 Count=FMath::Min(16,SnapshotQueue.Num()-SnapshotCursor);
		TArray<FGuLiCombatEffectState> Batch;
		for (int32 Index=0; Index<Count; ++Index)
		{
			FGuLiCombatEffectState Fresh;
			if (Runtime.IsValid() && Runtime->QueryEffect(SnapshotQueue[SnapshotCursor++].EffectId,Fresh)
				&& Fresh.Phase != EGuLiCombatEffectPhase::Dissipating) Batch.Add(Fresh);
		}
		if (!Batch.IsEmpty()) MulticastActiveSnapshot(Batch);
	}
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
void UGuLiCombatEffectReplicationComponent::MulticastCorrections_Implementation(const TArray<FGuLiCombatEffectCorrection>& InCorrections)
{
	if (GetOwner()->HasAuthority()) return;
	if (auto* Visuals=GetWorld()->GetSubsystem<UGuLiCombatEffectPresentationSubsystem>())
		for (const auto& Correction:InCorrections) Visuals->ApplyCorrection(Correction);
}
void UGuLiCombatEffectReplicationComponent::MulticastShots_Implementation(const TArray<FGuLiCombatShotCue>& Shots)
{
	if (GetOwner()->HasAuthority()) return;
	if (auto* Visuals = GetWorld()->GetSubsystem<UGuLiCombatEffectPresentationSubsystem>()) Visuals->ApplyShots(Shots);
}
