#include "Commander/Presentation/GuLiNetworkOverlayStats.h"

#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Engine/Channel.h"
#include "Engine/Engine.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/Data/GuLiGameText.h"

namespace
{
	FString Decimal(double Value) { return FString::Printf(TEXT("%.1f"), Value); }
	FString Integer(double Value) { return FString::Printf(TEXT("%.0f"), Value); }

	int32 CountUnackedReliableBunches(const UNetConnection& Connection)
	{
		int32 Count = 0;
		for (const UChannel* Channel : Connection.OpenChannels)
		{
			if (Channel) Count += FMath::Max(0, Channel->NumOutRec);
		}
		return Count;
	}

	FString TransportLine(const TCHAR* TextId, const UNetConnection& Connection)
	{
		// QueuedBits is bandwidth debt, not bytes in an application queue. Do not call
		// IsNetReady(Saturate) here: diagnostics must never alter connection credit.
		return GuLiGameText::Format(TextId, {
			Decimal(Connection.CurrentNetSpeed / 1000.0),
			FString::FromInt(CountUnackedReliableBunches(Connection)),
			Decimal(FMath::Max(0, Connection.QueuedBits) / 8000.0) });
	}

#if WITH_EDITOR
	// Single-process PIE only: identify this client's peer, never sum other clients
	// or add a diagnostic RPC just to expose server internals in packaged games.
	UNetConnection* FindPIEServerPeer(APlayerController& Controller)
	{
		const UWorld* ClientWorld = Controller.GetWorld();
		if (!GEngine || !ClientWorld || ClientWorld->WorldType != EWorldType::PIE) return nullptr;
		const auto* ClientState = ClientWorld->GetGameState<AGuLiBattleGameState>();
		const auto* ClientPlayer = Controller.GetPlayerState<AGuLiBattlePlayerState>();
		if (!ClientState || !ClientState->GetMatchId().IsValid()
			|| !ClientPlayer || !ClientPlayer->GetPlayerGuid().IsValid()) return nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!World || World == ClientWorld || World->WorldType != EWorldType::PIE
				|| (World->GetNetMode() != NM_DedicatedServer && World->GetNetMode() != NM_ListenServer)) continue;
			const auto* ServerState = World->GetGameState<AGuLiBattleGameState>();
			if (!ServerState || ServerState->GetMatchId() != ClientState->GetMatchId()) continue;
			for (auto It = World->GetPlayerControllerIterator(); It; ++It)
			{
				APlayerController* Peer = It->Get();
				const auto* Player = Peer ? Peer->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
				if (Player && Player->GetPlayerGuid() == ClientPlayer->GetPlayerGuid())
				{
					UNetConnection* Connection = Peer->GetNetConnection();
					if (Connection && Connection->GetConnectionState() == USOCK_Open) return Connection;
				}
			}
		}
		return nullptr;
	}
#endif
}

void FGuLiNetworkOverlayStats::Refresh(APlayerController* Controller, const double Now)
{
	if (SampleWallSeconds >= 0.0 && Now - SampleWallSeconds < 1.0) return;
	UWorld* World = Controller ? Controller->GetWorld() : nullptr;
	UNetDriver* Driver = World ? World->GetNetDriver() : nullptr;
	UNetConnection* Connection = Driver ? Driver->ServerConnection.Get() : nullptr;
	Lines.Reset();
	if (!World || World->GetNetMode() != NM_Client || !Connection || Connection->GetConnectionState() != USOCK_Open)
	{
		Lines.Add(GuLiGameText::Text(World && World->GetNetMode() == NM_Client
			? TEXT("UI.Performance.NetPending") : TEXT("UI.Performance.NetLocal")));
		SampleConnection.Reset();
		SampleFlights.Reset();
		SampleWallSeconds = Now;
		return;
	}

	// Engine totals count connection packets (including UE's packet overhead), not
	// OS-wide traffic. Unsigned subtraction handles the int32 counters wrapping.
	const uint32 Totals[] = { static_cast<uint32>(Connection->InTotalBytes), static_cast<uint32>(Connection->OutTotalBytes),
		static_cast<uint32>(Connection->InTotalPackets), static_cast<uint32>(Connection->OutTotalPackets),
		static_cast<uint32>(Connection->InTotalPacketsLost), static_cast<uint32>(Connection->OutTotalPacketsLost) };
	static_assert(UE_ARRAY_COUNT(Totals) == UE_ARRAY_COUNT(PreviousTotals));
	constexpr int32 CounterCount = UE_ARRAY_COUNT(Totals);
	const double Elapsed = Now - SampleWallSeconds;
	bool bHaveRates = SampleConnection.Get() == Connection && SampleWallSeconds >= 0.0 && Elapsed > 0.0;
	double Rates[CounterCount] = {};
	for (int32 Index = 0; Index < CounterCount; ++Index)
	{
		const uint32 Delta = Totals[Index] - PreviousTotals[Index];
		// A reset on the same connection must not appear as a multi-GB/s spike.
		bHaveRates &= Delta <= MAX_int32;
		if (bHaveRates) Rates[Index] = Delta / Elapsed;
		PreviousTotals[Index] = Totals[Index];
	}
	if (bHaveRates)
	{
		Lines.Add(GuLiGameText::Format(TEXT("UI.Performance.NetThroughput"),
			{ Decimal(Rates[0] / 1000.0), Decimal(Rates[1] / 1000.0), Decimal((Rates[0] + Rates[1]) / 1000.0) }));
		Lines.Add(GuLiGameText::Format(TEXT("UI.Performance.NetPackets"),
			{ Integer(Rates[2]), Integer(Rates[3]), Decimal(Rates[4]), Decimal(Rates[5]) }));
	}
	else Lines.Add(GuLiGameText::Text(TEXT("UI.Performance.NetPending")));
	Lines.Add(TransportLine(TEXT("UI.Performance.NetTransport"), *Connection));

	const auto* State = World->GetGameState();
	auto* Flights = State ? State->FindComponentByClass<UGuLiCombatEffectReplicationComponent>() : nullptr;
	if (Flights)
	{
		const FGuLiFlightReceiveCounters& Current = Flights->GetFlightReceiveCounters();
		if (bHaveRates && SampleFlights.Get() == Flights && Current.PayloadBytes >= PreviousFlights.PayloadBytes
			&& Current.Events >= PreviousFlights.Events && Current.AgeSamples >= PreviousFlights.AgeSamples)
		{
			const uint64 AgeSamples = Current.AgeSamples - PreviousFlights.AgeSamples;
			const FString Age = AgeSamples > 0
				? Integer((Current.AgeMilliseconds - PreviousFlights.AgeMilliseconds) / AgeSamples) : TEXT("--");
			Lines.Add(GuLiGameText::Format(TEXT("UI.Performance.NetFlightReceive"), {
				Decimal((Current.Events - PreviousFlights.Events) / Elapsed),
				Decimal((Current.PayloadBytes - PreviousFlights.PayloadBytes) / Elapsed / 1000.0), Age }));
		}
		else Lines.Add(GuLiGameText::Text(TEXT("UI.Performance.NetFlightPending")));
		PreviousFlights = Current;
	}
	else Lines.Add(GuLiGameText::Text(TEXT("UI.Performance.NetFlightPending")));
	SampleFlights = Flights;
	SampleConnection = Connection;
	SampleWallSeconds = Now;

#if WITH_EDITOR
	if (World->WorldType == EWorldType::PIE)
	{
		if (UNetConnection* Peer = FindPIEServerPeer(*Controller))
		{
			Lines.Add(TransportLine(TEXT("UI.Performance.NetPIETransport"), *Peer));
			const auto* ServerState = Peer->GetWorld() ? Peer->GetWorld()->GetGameState() : nullptr;
			const auto* ServerFlights = ServerState ? ServerState->FindComponentByClass<UGuLiCombatEffectReplicationComponent>() : nullptr;
			const FGuLiFlightPeerStats Queue = ServerFlights ? ServerFlights->GetFlightPeerStats(Peer) : FGuLiFlightPeerStats();
			if (Queue.bAvailable)
			{
				Lines.Add(GuLiGameText::Format(TEXT("UI.Performance.NetPIEFlightQueue"), {
					FString::FromInt(Queue.QueuedEvents), Queue.QueuedEvents > 0 ? Integer(Queue.HeadEventAgeMilliseconds) : TEXT("--") }));
			}
			else Lines.Add(GuLiGameText::Text(TEXT("UI.Performance.NetPIEQueuePending")));
		}
		else Lines.Add(GuLiGameText::Text(TEXT("UI.Performance.NetPIEUnavailable")));
	}
#endif
}
