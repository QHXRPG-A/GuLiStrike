#include "Gameplay/Ship/Build/GuLiShipBuildComponent.h"
#include "Network/GuLiNetworkSettings.h"
#include "Gameplay/Ship/Build/GuLiShipAssemblyComponent.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

UGuLiShipBuildComponent::UGuLiShipBuildComponent() { SetIsReplicatedByDefault(false); }

const FGuLiShipCompiledBuildRules& UGuLiShipBuildComponent::GetRules() const
{
	if (CompiledCatalog != Catalog || CompiledRevision != Catalog->Revision)
	{
		Catalog->Compile(Rules);
		CompiledCatalog = Catalog; CompiledRevision = Catalog->Revision;
	}
	return Rules;
}
TArray<FGuLiShipChoiceAvailability> UGuLiShipBuildComponent::QueryChoices() const
{
	TArray<FGuLiShipChoiceAvailability> Choices;
	if (!Catalog) return Choices;
	const auto& Compiled = GetRules();
	for (const auto& Node : Catalog->Upgrades)
	{
		auto& Choice = Choices.AddDefaulted_GetRef(); Choice.NodeId = Node.NodeId;
		Choice.bAvailable = Catalog->CanChoose(Compiled, State, Node.NodeId, Choice.Reason);
	}
	return Choices;
}

bool UGuLiShipBuildComponent::RestoreShip(UGuLiShipAssemblyComponent& Assembly, FString& Error)
{
	const auto* GameState = GetWorld()->GetGameState<AGuLiBattleGameState>();
	const auto* Pawn = Cast<APawn>(Assembly.GetOwner());
	if (!GetOwner()->HasAuthority() || !GameState || !GameState->GetMatchEpoch() || !Pawn
		|| Pawn->GetPlayerState() != GetOwner() || !Assembly.Catalog)
	{
		Error = TEXT("Ship restoration requires the owning PlayerState, initialized match and catalogue."); return false;
	}
	FGuLiShipRunBuildState Candidate = State;
	if (Candidate.MatchEpoch != GameState->GetMatchEpoch())
	{
		if (!UGuLiNetworkSettings::IsHybrid(GetWorld())) Candidate = {};
		Candidate.MatchEpoch = GameState->GetMatchEpoch(); Candidate.BuildRevision = FMath::Max<int64>(1,Candidate.BuildRevision);
	}
	if (Assembly.GetAppliedMatchEpoch() == Candidate.MatchEpoch && Assembly.GetAppliedBuildRevision() == Candidate.BuildRevision)
	{
		CurrentAssembly = &Assembly; return true;
	}
	if (!Assembly.PrepareBuild(Candidate, Error)) return false;
	if (State.MatchEpoch != Candidate.MatchEpoch) Requests.Reset();
	State = Candidate; Catalog = Assembly.Catalog; CurrentAssembly = &Assembly;
	Assembly.CommitPreparedBuild();

	return true;
}

FGuLiShipChoiceResult UGuLiShipBuildComponent::CommitConfirmedChoice(FGuid RequestId, FName NodeId, int64 MatchEpoch, int64 ExpectedBuildRevision)
{
	FGuLiShipChoiceResult Result; Result.RequestId = RequestId; Result.BuildRevision = State.BuildRevision;
	if (UGuLiNetworkSettings::IsHybrid(GetWorld()))
	{ Result.Reason=TEXT("Hybrid upgrade choices require a committed coordinator result."); return Result; }
	if (!GetOwner()->HasAuthority() || !RequestId.IsValid() || bCommitting)
	{
		Result.Reason = TEXT("Choice requires a unique server request outside an active commit."); return Result;
	}
	if (const auto* Previous = Requests.Find(RequestId))
	{
		if (Previous->NodeId == NodeId && Previous->Epoch == MatchEpoch && Previous->Revision == ExpectedBuildRevision) return Previous->Result;
		Result.Reason = TEXT("Request ID was already used for different choice content."); return Result;
	}
	auto* Assembly = CurrentAssembly.Get();
	const auto* GameState = GetWorld()->GetGameState<AGuLiBattleGameState>();
	if (!Assembly || !Assembly->IsAvailableForChoice() || !GameState || MatchEpoch != GameState->GetMatchEpoch() || MatchEpoch != State.MatchEpoch || ExpectedBuildRevision != State.BuildRevision)
		Result.Reason = TEXT("Choice refers to an unavailable Ship, old match, or stale build revision.");
	else
	{
		if (Catalog->CanChoose(GetRules(), State, NodeId, Result.Reason))
		{
			FGuLiShipRunBuildState Candidate = State; Candidate.ChosenNodeIds.Add(NodeId); ++Candidate.BuildRevision;
			if (Assembly->PrepareBuild(Candidate, Result.Reason))
			{
				bCommitting = true;
				State = Candidate;
				Assembly->CommitPreparedBuild();
				bCommitting = false;
				Result.bCommitted = true; Result.bBusinessEffects=true;Result.BuildRevision = State.BuildRevision;

			}
		}
	}
	Requests.Add(RequestId, {NodeId, MatchEpoch, ExpectedBuildRevision, Result});
	return Result;
}
void UGuLiShipBuildComponent::CopyMatchStateFrom(const UGuLiShipBuildComponent& Other)
{
	check(GetOwner()->HasAuthority());
	State = Other.State; Catalog = Other.Catalog;
	Requests = Other.Requests;
}
FGuid UGuLiShipBuildComponent::RequestConfirmedChoice(FGuid RequestId,FName NodeId,int64 MatchEpoch,int64 ExpectedBuildRevision)
{
    if (!RequestId.IsValid()) RequestId=FGuid::NewGuid();
    if (UGuLiNetworkSettings::IsHybrid(GetWorld()))
    {auto* Assembly=CurrentAssembly.Get();return Assembly?Assembly->RequestHybridChoice(RequestId,NodeId,MatchEpoch,ExpectedBuildRevision):FGuid{};}
    const bool AlreadyResolved=Requests.Contains(RequestId);
    const auto Result=CommitConfirmedChoice(RequestId,NodeId,MatchEpoch,ExpectedBuildRevision);
    if (!Requests.Contains(RequestId)) return {};
    if (!AlreadyResolved) OnChoiceResolved.Broadcast(Result);
    return RequestId;
}
bool UGuLiShipBuildComponent::FindChoiceResult(FGuid RequestId,FGuLiShipChoiceResult& Result) const
{
    if (UGuLiNetworkSettings::IsHybrid(GetWorld()))
    {const auto* Assembly=CurrentAssembly.Get();return Assembly && Assembly->FindHybridChoiceResult(RequestId,Result);}
    const auto* Found=Requests.Find(RequestId);if (!Found) return false;Result=Found->Result;return true;
}
bool UGuLiShipBuildComponent::NotifyHybridChoiceResolved(const UGuLiShipAssemblyComponent& Assembly,const FGuLiShipChoiceResult& Result)
{if (CurrentAssembly.Get()!=&Assembly) return false;OnChoiceResolved.Broadcast(Result);return true;}
bool UGuLiShipBuildComponent::AdoptExistingShip(UGuLiShipAssemblyComponent& Assembly)
{
    if (!GetOwner()->HasAuthority() || bCommitting || !Assembly.Catalog) return false;
    State=Assembly.GetAppliedBuild(); Catalog=Assembly.Catalog; CurrentAssembly=&Assembly; Requests.Reset();
     return true;
}
