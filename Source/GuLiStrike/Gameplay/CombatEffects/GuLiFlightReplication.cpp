#include "Gameplay/CombatEffects/GuLiCombatEffectReplicationComponent.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectRuntimeSubsystem.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectPresentationSubsystem.h"
#include "Engine/ActorChannel.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "EngineLogs.h"

namespace
{
	TAutoConsoleVariable<int32> CVarFlightDiagnostics(TEXT("guli.Net.FlightDiagnostics"), 0,
		TEXT("Log aggregate create/end payload bytes and per-connection queue depth once per second."));
	UGuLiCombatEffectReplicationComponent* FlightChannel(UWorld* World)
	{
		if (!World || World->GetNetMode() == NM_Client) return nullptr;
		auto* State = World->GetGameState();
		return State ? State->FindComponentByClass<UGuLiCombatEffectReplicationComponent>() : nullptr;
	}
}

bool UGuLiCombatEffectReplicationComponent::PublishFlight(UWorld* World, const FGuLiFlightEvent& Event)
{
	auto* Channel = FlightChannel(World);
	if (!Channel || !GuLiFlightWire::IsFlight(Event.State.Kind) || !Event.State.IsWellFormed()
		|| !Channel->IsCurrentEpoch(Event.State.MatchEpoch)) return false;
	TArray<uint8> Record; uint16 Bits;
	if (!GuLiFlightWire::EncodeRecord(Event, Record, Bits))
	{
		UE_LOG(LogNet, Error, TEXT("Flight recipe exceeds 1000-byte wire limit: %s"), *Event.State.EffectId.ToString());
		return false;
	}
	Channel->HandleEpoch(Event.State.MatchEpoch);
	const bool Finished = Event.State.Phase == EGuLiCombatEffectPhase::Finished;
	if (Finished)
	{
		// Only the producer of an accepted launch can end it. Repeated callbacks
		// (physics stop, Destroy, lifespan) cannot enqueue another impact.
		if (!Channel->ActiveFlights.Remove(Event.State.EffectId)) return false;
		++Channel->EndedFlights;
	}
	else
	{
		if (Channel->ActiveFlights.Contains(Event.State.EffectId)) return false;
		Channel->ActiveFlights.Add(Event.State.EffectId, Event); ++Channel->CreatedFlights;
	}
	Channel->PendingFlights.Add(Event);
	if (auto* Visuals = World->GetSubsystem<UGuLiCombatEffectPresentationSubsystem>()) Visuals->ApplyFlightEvent(Event);
	return true;
}

void UGuLiCombatEffectReplicationComponent::UpdateFlight(UWorld* World, const FGuLiCombatEffectState& State)
{
	if (auto* Channel = FlightChannel(World); Channel && Channel->Epoch == State.MatchEpoch)
		if (auto* Active = Channel->ActiveFlights.Find(State.EffectId)) Active->State = State;
}

bool UGuLiCombatEffectReplicationComponent::IsFlightActive(UWorld* World,const FGuid& Id)
{
	const auto* Channel = FlightChannel(World);
	return Channel && Channel->ActiveFlights.Contains(Id);
}

void UGuLiCombatEffectReplicationComponent::AttachFlightMuzzle(UWorld* World, const FGuLiCombatShotCue& Cue)
{
	auto* Channel = FlightChannel(World);
	if (!Channel || Channel->Epoch != Cue.MatchEpoch) return;
	for (auto& Event : Channel->PendingFlights)
		if (Event.State.EffectId == Cue.ShotId && Event.State.Phase != EGuLiCombatEffectPhase::Finished)
		{ Event.Muzzle = Cue; Event.bHasMuzzle = true; break; }
	if (auto* Active = Channel->ActiveFlights.Find(Cue.ShotId)) { Active->Muzzle = Cue; Active->bHasMuzzle = true; }
	if (auto* Visuals = World->GetSubsystem<UGuLiCombatEffectPresentationSubsystem>())
		if (const auto* Active = Channel->ActiveFlights.Find(Cue.ShotId)) Visuals->ApplyFlightEvent(*Active);
}

void UGuLiCombatEffectReplicationComponent::SendToConnection(UNetConnection* Connection, UFunction* Function, void* Parameters)
{
	auto* Driver = GetWorld()->GetNetDriver();
	auto* ActorChannel = Connection->FindActorChannelRef(GetOwner());
	const auto* ClassCache = Driver->NetCache->GetClassNetCache(GetClass());
	const auto* FieldCache = ClassCache ? ClassCache->GetFromField(Function) : nullptr;
	if (ActorChannel && FieldCache)
		Driver->ProcessRemoteFunctionForChannel(ActorChannel,ClassCache,FieldCache,this,Connection,Function,
			Parameters,nullptr,nullptr,true,UNetDriver::ERemoteFunctionSendPolicy::Default);
}

void UGuLiCombatEffectReplicationComponent::FlushFlightStreams()
{
	auto* Driver = GetWorld()->GetNetDriver();
	if (!Driver) { PendingFlights.Reset(); return; }
	const float Now = GetWorld()->GetTimeSeconds();
	for (auto It=FlightPeers.CreateIterator(); It; ++It)
		if (!It.Key().IsValid() || It.Key()->GetConnectionState()!=USOCK_Open
			|| !Driver->ClientConnections.Contains(It.Key().Get())) It.RemoveCurrent();
	for (UNetConnection* Connection : Driver->ClientConnections)
	{
		if (!Connection || Connection->GetConnectionState()!=USOCK_Open || !Connection->ViewTarget) continue;
		auto* ActorChannel = Connection->FindActorChannelRef(GetOwner());
		if (!ActorChannel || ActorChannel->OpenPacketId.First==INDEX_NONE || ActorChannel->Closing) continue;
		auto* Peer = FlightPeers.Find(Connection);
		if (Peer && Peer->Channel.Get()!=ActorChannel) { FlightPeers.Remove(Connection); Peer = nullptr; }
		if (!Peer)
		{
			Peer = &FlightPeers.Add(Connection);
			Peer->Channel = ActorChannel;
			for (const auto& Pair : ActiveFlights)
			{
				FGuLiFlightEvent Bootstrap = Pair.Value;
				if (Runtime.IsValid()) Runtime->QueryEffect(Pair.Key, Bootstrap.State);
				// QueryEffect clears its output on a miss; external Ship/logical
				// missile producers maintain the separate registry instead.
				if (!Bootstrap.State.EffectId.IsValid()) Bootstrap.State = Pair.Value.State;
				if (Bootstrap.State.EndTime <= Now) continue;
				Bootstrap.bBootstrap = true; Bootstrap.bHasMuzzle = false;
				if (Bootstrap.State.Kind == EGuLiCombatEffectKind::LinearProjectile)
				{
					if (Bootstrap.ShipVisualClass.IsNull())
						Bootstrap.State.Location = FVector(Bootstrap.State.LaunchLocation)+FVector(Bootstrap.State.Velocity)*(Now-Bootstrap.State.StartTime);
					Bootstrap.State.LaunchLocation = Bootstrap.State.Location;
					Bootstrap.State.StartTime = Bootstrap.State.ActivationTime = Now;
				}
				if (Bootstrap.State.Kind == EGuLiCombatEffectKind::LinearProjectile) Bootstrap.State.SampleTime = Now;
				Peer->Queue.Add(MoveTemp(Bootstrap)); ++BootstrapFlights;
			}
			TArray<FGuLiCombatEffectState> Fields;
			if (Runtime.IsValid()) Runtime->BuildActiveSnapshot(Fields, true);
			Fields.RemoveAll([](const auto& State){ return GuLiFlightWire::IsFlight(State.Kind); });
			for (int32 Index=0; Index<Fields.Num(); Index+=16)
			{
				struct FParameters { TArray<FGuLiCombatEffectState> States; } Parameters;
				Parameters.States.Append(Fields.GetData()+Index,FMath::Min(16,Fields.Num()-Index));
				SendToConnection(Connection,FindFunctionChecked(GET_FUNCTION_NAME_CHECKED(ThisClass,MulticastActiveSnapshot)),&Parameters);
			}
		}
		Peer->Queue.Append(PendingFlights);
		// Backpressure leaves reliable events here until the connection has credit.
		// Neither ForceSend nor cosmetic burst debt bypasses the transport budget.
		for (int32 Batch=0; Batch<8 && Peer->Cursor<Peer->Queue.Num() && Connection->IsNetReady(); ++Batch)
		{
			struct FParameters { TArray<uint8> Payload; } Parameters;
			const int32 Count = GuLiFlightWire::EncodeBatch(MakeArrayView(Peer->Queue).Slice(Peer->Cursor,Peer->Queue.Num()-Peer->Cursor),Parameters.Payload);
			if (!Count) { UE_LOG(LogNet, Error, TEXT("Unable to encode accepted flight event")); break; }
			SendToConnection(Connection,FindFunctionChecked(GET_FUNCTION_NAME_CHECKED(ThisClass,MulticastFlightBatch)),&Parameters);
			Peer->Cursor += Count; SentFlightBytes += Parameters.Payload.Num(); ++SentFlightBatches;
		}
		if (Peer->Cursor == Peer->Queue.Num()) { Peer->Queue.Reset(); Peer->Cursor = 0; }
		else if (Peer->Cursor >= 256) { Peer->Queue.RemoveAt(0,Peer->Cursor,EAllowShrinking::No); Peer->Cursor = 0; }
	}
	PendingFlights.Reset();
	// Diagnostics are aggregate and dormant by default; no per-projectile log spam.
	if (CVarFlightDiagnostics.GetValueOnGameThread())
	{
		if (FPlatformTime::Seconds()>=NextFlightDiagnosticTime)
		{ NextFlightDiagnosticTime = FPlatformTime::Seconds()+1; UE_LOG(LogNet, Log, TEXT("GuLiFlights %s"), *GetFlightDiagnostics()); }
	}
}

void UGuLiCombatEffectReplicationComponent::MulticastFlightBatch_Implementation(const TArray<uint8>& Payload)
{
	if (GetOwner()->HasAuthority()) return;
	TArray<FGuLiFlightEvent> Events;
	if (!GuLiFlightWire::DecodeBatch(Payload, Events)) { UE_LOG(LogNet, Warning, TEXT("Rejected invalid flight batch")); return; }
	FlightReceiveCounters.PayloadBytes += Payload.Num();
	FlightReceiveCounters.Events += Events.Num();
	if (const auto* State = GetWorld()->GetGameState())
	{
		const double ServerNow = State->GetServerWorldTimeSeconds();
		for (const FGuLiFlightEvent& Event : Events)
		{
			// Bootstrap is an active-state reconstruction, not a freshly produced event.
			// Keep signed age: the client's estimated server clock can be ahead/behind.
			if (!Event.bBootstrap && IsCurrentEpoch(Event.State.MatchEpoch)
				&& FMath::IsFinite(ServerNow) && FMath::IsFinite(Event.State.SampleTime))
			{
				++FlightReceiveCounters.AgeSamples;
				FlightReceiveCounters.AgeMilliseconds += (ServerNow - Event.State.SampleTime) * 1000.0;
			}
		}
	}
	if (auto* Visuals = GetWorld()->GetSubsystem<UGuLiCombatEffectPresentationSubsystem>())
		for (const auto& Event : Events) Visuals->ApplyFlightEvent(Event);
}

FGuLiFlightPeerStats UGuLiCombatEffectReplicationComponent::GetFlightPeerStats(UNetConnection* Connection) const
{
	FGuLiFlightPeerStats Result;
	if (!Connection || !GetOwner() || !GetOwner()->HasAuthority()) return Result;
	const FPeerStream* Peer = FlightPeers.Find(Connection);
	if (!Peer) return Result;
	Result.bAvailable = true;
	Result.QueuedEvents = FMath::Max(0, Peer->Queue.Num() - Peer->Cursor);
	if (Result.QueuedEvents > 0 && GetWorld())
	{
		// Event age is not time spent in this queue; it also includes producer delay.
		Result.HeadEventAgeMilliseconds = FMath::Max(0.0,
			(GetWorld()->GetTimeSeconds() - static_cast<double>(Peer->Queue[Peer->Cursor].State.SampleTime)) * 1000.0);
	}
	return Result;
}

FString UGuLiCombatEffectReplicationComponent::GetFlightDiagnostics() const
{
	int32 Queued = 0; for (const auto& Pair : FlightPeers) Queued += Pair.Value.Queue.Num()-Pair.Value.Cursor;
	int32 Commander=0,Ground=0,Ship=0,Wingman=0;
	for (const auto& Pair : ActiveFlights)
		switch (Pair.Value.State.Source.Kind)
		{
		case EGuLiTargetKind::CommanderSoldier: ++Commander; break;
		case EGuLiTargetKind::GroundActor: ++Ground; break;
		case EGuLiTargetKind::Ship: ++Ship; break;
		case EGuLiTargetKind::Wingman: ++Wingman; break;
		default: break;
		}
	return FString::Printf(TEXT("active=%d created=%llu ended=%llu payloadBytes=%llu batches=%llu bootstrap=%llu queued=%d policy=events_only domains=%d/%d/%d/%d"),
		ActiveFlights.Num(),CreatedFlights,EndedFlights,SentFlightBytes,SentFlightBatches,BootstrapFlights,Queued,Commander,Ground,Ship,Wingman);
}
