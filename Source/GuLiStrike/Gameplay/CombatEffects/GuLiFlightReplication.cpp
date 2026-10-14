#include "Gameplay/CombatEffects/GuLiCombatEffectReplicationComponent.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectPresentationSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Network/GuLiNetworkSettings.h"
#include "Network/GuLiHybridWorldSubsystem.h"

namespace
{
	UGuLiCombatEffectReplicationComponent* FlightChannel(UWorld* World)
	{
		if (!World) return nullptr;
		auto* State = World->GetGameState();
		return State ? State->FindComponentByClass<UGuLiCombatEffectReplicationComponent>() : nullptr;
	}
}

void UGuLiCombatEffectReplicationComponent::RebaseReceivedState(UWorld* World,uint64 OwnerTick,FGuLiCombatEffectState& State)
{
    const auto* Hybrid=World?World->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;
    if (!Hybrid) return;
    const float Offset=static_cast<float>(Hybrid->GetLogicalSeconds()-double(OwnerTick)/60.);
    State.StartTime+=Offset;State.SampleTime+=Offset;State.ActivationTime+=Offset;State.EndTime+=Offset;
}

void UGuLiCombatEffectReplicationComponent::RebasePublishedState(UWorld* World,uint64 ObjectTick,FGuLiCombatEffectState& State)
{
    const auto* Hybrid=World?World->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;
    if (!Hybrid) return;
    const float Offset=static_cast<float>(double(ObjectTick)/60.-Hybrid->GetLogicalSeconds());
    State.StartTime+=Offset;State.SampleTime+=Offset;State.ActivationTime+=Offset;State.EndTime+=Offset;
}

bool UGuLiCombatEffectReplicationComponent::ApplyHybridFlight(UWorld* World,const FGuLiFlightEvent& Event,bool Retired)
{
	const auto* State=World?World->GetGameState():nullptr;
	auto* Channel=State?State->FindComponentByClass<UGuLiCombatEffectReplicationComponent>():nullptr;
	if (!Channel || !UGuLiNetworkSettings::IsHybrid(World) || !GuLiFlightWire::IsFlight(Event.State.Kind)
		|| !Channel->IsCurrentEpoch(Event.State.MatchEpoch) || !Event.State.IsWellFormed()) return false;
	Channel->HandleEpoch(Event.State.MatchEpoch);
    if (Channel->bReviewReceipts && Event.State.bAcceptanceAdditional)
    {
        const auto Id=Event.State.EffectId;
        if (!Channel->AdditionalReceipts.Contains(Id))
        {
            if (Channel->ReceiptOrder.Num()<4096) Channel->ReceiptOrder.Add(Id);
            else
            {
                Channel->AdditionalReceipts.Remove(Channel->ReceiptOrder[Channel->NextReceipt]);
                Channel->ReceiptOrder[Channel->NextReceipt]=Id;Channel->NextReceipt=(Channel->NextReceipt+1)%4096;
            }
        }
        auto& Receipt=Channel->AdditionalReceipts.FindOrAdd(Id);
        Receipt.X=FMath::Max(Receipt.X,int32(Event.State.Sequence));
        Receipt.Y|=Event.State.Phase==EGuLiCombatEffectPhase::Finished?2:1;
    }
    // Record valid delivery even when the metadata channel already knows the
    // final lifecycle. Late start samples must not recreate a retired visual.
    if (Retired && Event.State.Phase!=EGuLiCombatEffectPhase::Finished) return true;
    if (Event.State.Phase==EGuLiCombatEffectPhase::Finished)
        Channel->FinishedFlights+=Channel->ActiveFlights.Remove(Event.State.EffectId);
    else
    {
        if (!Channel->ActiveFlights.Contains(Event.State.EffectId)) ++Channel->ObservedFlights;
        Channel->ActiveFlights.Add(Event.State.EffectId,Event);
    }
	if (auto* Visuals=World->GetSubsystem<UGuLiCombatEffectPresentationSubsystem>()) Visuals->ApplyFlightEvent(Event);
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
	if (auto* Active = Channel->ActiveFlights.Find(Cue.ShotId)) { Active->Muzzle = Cue; Active->bHasMuzzle = true; }
	if (auto* Visuals = World->GetSubsystem<UGuLiCombatEffectPresentationSubsystem>())
		if (const auto* Active = Channel->ActiveFlights.Find(Cue.ShotId)) Visuals->ApplyFlightEvent(*Active);
}

bool UGuLiCombatEffectReplicationComponent::QueryFlight(const FGuid& Id,FGuLiCombatEffectState& Out) const
{
    const auto* Found=ActiveFlights.Find(Id);
    if (!Found || !IsCurrentEpoch(Found->State.MatchEpoch)) return false;
    Out=Found->State; return true;
}
void UGuLiCombatEffectReplicationComponent::AppendAdditionalFlightSnapshot(TArray<FGuLiCombatEffectState>& Out) const
{
    for (const auto& Pair:ActiveFlights)
        if (Pair.Value.State.bAcceptanceAdditional && IsCurrentEpoch(Pair.Value.State.MatchEpoch)) Out.Add(Pair.Value.State);
    Out.Sort([](const auto& A,const auto& B){return A.EffectId<B.EffectId;});
}
int32 UGuLiCombatEffectReplicationComponent::GetActiveFlightCount() const
{
	const auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
	const auto* Battle=GetWorld()->GetGameState();
	const float Now=Hybrid?Hybrid->GetLogicalSeconds():Battle?Battle->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
	int32 Count=0;
	for (const auto& Pair:ActiveFlights)
		if (!Pair.Value.State.bAcceptanceAdditional || (Now>=Pair.Value.State.StartTime && Now<Pair.Value.State.EndTime)) ++Count;
	return Count;
}

int32 UGuLiCombatEffectReplicationComponent::GetAdditionalActiveFlightCount() const
{
	const auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
	const auto* Battle=GetWorld()->GetGameState();
	// Wire recipes and the real pool use float seconds. Quantize the read clock
    // identically at an end/start boundary so a sub-tick rounding gap cannot
    // count neither the expired flight nor its already-admitted successor.
	const float Now=Hybrid?Hybrid->GetLogicalSeconds():Battle?Battle->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
	int32 Count = 0;
	for (const auto& Pair : ActiveFlights)
		if (Pair.Value.State.bAcceptanceAdditional && Now>=Pair.Value.State.StartTime && Now<Pair.Value.State.EndTime) ++Count;
	return Count;
}

FString UGuLiCombatEffectReplicationComponent::GetFlightDiagnostics() const
{
    return FString::Printf(TEXT("active=%d observed=%llu finished=%llu route=owner_p2p"),
        ActiveFlights.Num(),ObservedFlights,FinishedFlights);
}
