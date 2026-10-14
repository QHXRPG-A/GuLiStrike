#include "Commander/Presentation/GuLiNetworkOverlayStats.h"
#include "Network/GuLiHybridSessionSubsystem.h"
#include "Gameplay/Data/GuLiGameText.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Dom/JsonObject.h"

void FGuLiNetworkOverlayStats::Refresh(APlayerController* Controller,double Now)
{
    if (SampleWallSeconds>=0 && Now-SampleWallSeconds<1) return;
    const auto* World=Controller?Controller->GetWorld():nullptr;
    auto* Session=World && World->GetGameInstance()?World->GetGameInstance()->GetSubsystem<UGuLiHybridSessionSubsystem>():nullptr;
    Lines.Reset();
    if (!Session || !Session->GetRoomId().IsValid())
    {
        Lines.Add(GuLiGameText::Text(TEXT("UI.Performance.NetPending")));
        SampleSession.Reset();SampleRoom={};SampleWallSeconds=Now;return;
    }
    const auto Snapshot=Session->GetNetworkCaptureSnapshot();
    const TCHAR* Keys[]={TEXT("datagram_received_bytes"),TEXT("datagram_sent_bytes"),TEXT("datagram_received_packets"),TEXT("datagram_sent_packets")};
    double Totals[4]={};const double Elapsed=Now-SampleWallSeconds;
    bool HaveRates=SampleSession.Get()==Session && SampleRoom==Session->GetRoomId() && SampleWallSeconds>=0 && Elapsed>0;
    for (int32 I=0;I<4;++I)
    {
        Snapshot->TryGetNumberField(Keys[I],Totals[I]);
        HaveRates&=Totals[I]>=PreviousTotals[I];
    }
    if (HaveRates)
    {
        const double Received=(Totals[0]-PreviousTotals[0])/Elapsed/1000.;
        const double Sent=(Totals[1]-PreviousTotals[1])/Elapsed/1000.;
        Lines.Add(GuLiGameText::Format(TEXT("UI.Performance.NetThroughput"),{
            FString::Printf(TEXT("%.1f"),Received),FString::Printf(TEXT("%.1f"),Sent),FString::Printf(TEXT("%.1f"),Received+Sent)}));
        Lines.Add(FString::Printf(TEXT("P2P datagrams: receive %.0f/s, send %.0f/s"),
            (Totals[2]-PreviousTotals[2])/Elapsed,(Totals[3]-PreviousTotals[3])/Elapsed));
    }
    else Lines.Add(GuLiGameText::Text(TEXT("UI.Performance.NetPending")));
    double Queued=0,Oldest=0;const TArray<TSharedPtr<FJsonValue>>* Peers=nullptr;
    if (Snapshot->TryGetArrayField(TEXT("peers"),Peers)) for (const auto& Value:*Peers)
    {
        const auto Peer=Value->AsObject();double Bytes=0,Age=0;if (!Peer) continue;
        Peer->TryGetNumberField(TEXT("queued_bytes"),Bytes);Peer->TryGetNumberField(TEXT("oldest_queue_ms"),Age);
        Queued+=Bytes;Oldest=FMath::Max(Oldest,Age);
    }
    Lines.Add(FString::Printf(TEXT("P2P application queue: %.1f KB, oldest %.0f ms"),Queued/1000.,Oldest));
    Lines.Add(Session->IsConnected()?TEXT("Coordinator: connected"):TEXT("Coordinator: reconnecting; current owners continue"));
    for (int32 I=0;I<4;++I) PreviousTotals[I]=Totals[I];
    SampleSession=Session;SampleRoom=Session->GetRoomId();SampleWallSeconds=Now;
}
