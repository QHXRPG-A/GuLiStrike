#include "Gameplay/CommanderSkills/GuLiCommanderSkillComponent.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Gameplay/Skills/GuLiArmySkillSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Network/GuLiHybridWorldSubsystem.h"

UGuLiCommanderSkillComponent::UGuLiCommanderSkillComponent() { SetIsReplicatedByDefault(false); }
AGuLiBattlePlayerState& UGuLiCommanderSkillComponent::PlayerState() const { return *CastChecked<AGuLiBattlePlayerState>(GetOwner()); }
AGuLiCommanderPlayerController* UGuLiCommanderSkillComponent::Commander() const { return Cast<AGuLiCommanderPlayerController>(GetOwner()->GetOwner()); }

void UGuLiCommanderSkillComponent::BeginPlay()
{
	Super::BeginPlay();
	if (auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>())
    {H->OnOwnershipStateApplied.AddUObject(this,&ThisClass::RebindHybridRequests);H->OnTransactionCompleted.AddDynamic(this,&ThisClass::CompleteHybridGlobal);}
	if (GetOwner()->HasAuthority())
	{
		if (auto* Army=GetWorld()->GetSubsystem<UGuLiArmySkillSubsystem>())
			Army->OnWeaponLoadoutCommitted().AddUObject(this,&ThisClass::HandleWeaponProfilesChanged);
		PlayerState().OnCommanderPlayerStateChanged.AddDynamic(this, &ThisClass::HandlePlayerStateChanged);
		HandlePlayerStateChanged();
	}
}
void UGuLiCommanderSkillComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()) {H->OnCommandCompleted.RemoveAll(this);H->OnOwnershipStateApplied.RemoveAll(this);H->OnTransactionCompleted.RemoveDynamic(this,&ThisClass::CompleteHybridGlobal);}
    HybridGlobalRequests.Reset();
    HybridPending.Reset();
	PlayerState().OnCommanderPlayerStateChanged.RemoveDynamic(this, &ThisClass::HandlePlayerStateChanged);
	if (BoundSelection.IsValid()) BoundSelection->OnSelectionChanged.RemoveAll(this);
	if (auto* Army=GetWorld()->GetSubsystem<UGuLiArmySkillSubsystem>()) Army->OnWeaponLoadoutCommitted().RemoveAll(this);
	Super::EndPlay(Reason);
}
void UGuLiCommanderSkillComponent::HandlePlayerStateChanged()
{
	if (!SynchronizeEpoch()) return;
	auto* PC = Commander();
	auto* Selection = PC ? PC->GetCommanderNetSyncComponent() : nullptr;
	if (BoundSelection != Selection)
	{
		if (BoundSelection.IsValid()) BoundSelection->OnSelectionChanged.RemoveAll(this);
		BoundSelection = Selection;
		if (Selection) Selection->OnSelectionChanged.AddUObject(this, &ThisClass::RefreshSelectedUnitSkills);
	}
	if (Selection) RefreshSelectedUnitSkills(Selection->GetSelectionState());
    RebindHybridRequests();
}
void UGuLiCommanderSkillComponent::HandleWeaponProfilesChanged(uint32 Revision)
{
	if (GetOwner()->HasAuthority() && Catalog && BoundSelection.IsValid())
		RefreshSelectedUnitSkills(BoundSelection->GetSelectionState());
}
void UGuLiCommanderSkillComponent::RefreshSelectedUnitSkills(const FGuLiCommanderSelectionState& Selection)
{
	SelectedUnitRuntime.Reset(); RuntimeSelectionRevision = Selection.SelectionRevision;
	if (!Catalog) return;
	auto& Authority = *GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
	TSet<uint32> Seen;
	for (const auto& Cohort : Selection.Cohorts) for (auto Id : Cohort.MemberIds)
	{
		if (Seen.Contains(Id.Value)) continue;
		Seen.Add(Id.Value);
		FGuLiUnitSkillRuntimeView View; View.SoldierId = Id;
		if (Authority.QueryUnitSkillRuntime(Id, *Catalog, View.Runtime)) SelectedUnitRuntime.Add(View);
	}
}
TArray<FGuLiUnitSkillRuntimeView> UGuLiCommanderSkillComponent::GetSelectedUnitSkills() const
{
	const auto* PC = Commander();
	return PC && PC->GetCommanderNetSyncComponent()->GetSelectionState().SelectionRevision == RuntimeSelectionRevision
		? SelectedUnitRuntime : TArray<FGuLiUnitSkillRuntimeView>();
}
void UGuLiCommanderSkillComponent::CopyMatchStateFrom(const UGuLiCommanderSkillComponent& Other)
{
	check(GetOwner()->HasAuthority());
	Catalog = Other.Catalog; RuntimeEpoch = Other.RuntimeEpoch;
	GlobalRuntime = Other.GlobalRuntime; Receipts = Other.Receipts;
    HybridPending=Other.HybridPending; HybridGlobalRequests=Other.HybridGlobalRequests;
}
bool UGuLiCommanderSkillComponent::InitializeCatalog()
{
	if (Catalog) return true;
	auto* Candidate = GetDefault<UGuLiCommanderSkillSettings>()->Catalog.LoadSynchronous();
	if (!Candidate) return false; // An empty catalogue is a supported project configuration.
	FString Error;
	if (!Candidate->Validate(Error)) { UE_LOG(LogTemp, Error, TEXT("Commander skill catalogue: %s"), *Error); return false; }
	Catalog = Candidate;
	return true;
}
bool UGuLiCommanderSkillComponent::SynchronizeEpoch()
{
	const auto* GameState = GetWorld()->GetGameState<AGuLiBattleGameState>();
	if (!GameState || !GameState->GetMatchEpoch() || !InitializeCatalog()) return false;
	if (RuntimeEpoch == GameState->GetMatchEpoch()) return true;
	RuntimeEpoch = GameState->GetMatchEpoch(); Receipts.Reset(); GlobalRuntime.Reset(); SelectedUnitRuntime.Reset();
	for (FName Id : Catalog->GlobalSkills)
	{
		auto& Runtime = GlobalRuntime.AddDefaulted_GetRef(); Runtime.SkillId = Id;
	}
	return true;
}
FGuLiActiveSkillRequest UGuLiCommanderSkillComponent::MakeLocalRequest(bool bHasGroundPoint, FVector GroundPoint) const
{
	FGuLiActiveSkillRequest Request;
	Request.RequestId = FGuid::NewGuid();
	Request.MatchEpoch = GetWorld()->GetGameState<AGuLiBattleGameState>()->GetMatchEpoch();
	Request.bHasGroundPoint = bHasGroundPoint; Request.GroundPoint = GroundPoint;
	return Request;
}
void UGuLiCommanderSkillComponent::ActivateSelectedUnits(bool bHasGroundPoint, FVector GroundPoint)
{
    RequestHybridUnitSkills(bHasGroundPoint,GroundPoint);
}

void UGuLiCommanderSkillComponent::ActivateGlobalSkill(FName SkillId, bool bHasGroundPoint, FVector GroundPoint)
{RequestGlobalSkill(SkillId,bHasGroundPoint,GroundPoint);}
FGuid UGuLiCommanderSkillComponent::RequestGlobalSkill(FName SkillId,bool bHasGroundPoint,FVector GroundPoint)
{
	auto* PC = Commander();
	if (!PC || !PC->IsLocalController() || !PC->CanIssueCommanderOrders() || !Catalog || !Catalog->GlobalSkills.Contains(SkillId)) return {};
	auto Request = MakeLocalRequest(bHasGroundPoint, GroundPoint); Request.GlobalSkillId = SkillId;
	ServerRequestSkill(Request);
    return Request.RequestId;
}
int32 UGuLiCommanderSkillComponent::GetGlobalSkillLevel(FName SkillId) const
{
	const auto Values=GetGlobalSkills();const auto* Runtime = Values.FindByPredicate([SkillId](const auto& Item) { return Item.SkillId == SkillId; });
	return Runtime ? Runtime->Level : 1;
}
bool UGuLiCommanderSkillComponent::SetServerGlobalSkillLevel(FName SkillId, int32 Level)
{
	if (!GetOwner()->HasAuthority() || !SynchronizeEpoch()) return false;
	auto* Runtime = GlobalRuntime.FindByPredicate([SkillId](const auto& Item) { return Item.SkillId == SkillId; });
	const auto* Definition = Catalog->FindSkill(SkillId);
	if (!Runtime || !Definition || Level < 1 || Level > Definition->MaximumLevel) return false;
    return Definition->ExecutorClass
        && Definition->ExecutorClass->GetDefaultObject<UGuLiCommanderSkillExecutor>()->SetHybridLevel(*GetWorld(),PlayerState().GetTeam(),SkillId,Level).IsValid();
}
void UGuLiCommanderSkillComponent::ServerRequestSkill(const FGuLiActiveSkillRequest& Request)
{
    if (Request.GlobalSkillId.IsNone())
    {
        if (!RouteHybridUnitSkillRequest(Request).IsValid())
        {
            FGuLiActiveSkillReply Reply;Reply.RequestId=Request.RequestId;Reply.MatchEpoch=Request.MatchEpoch;
            Reply.Code=EGuLiActiveSkillResultCode::InvalidRequest;Reply.Error=TEXT("Unit skill request is stale, conflicting, or not ready");
            ClientReceiveReply(Reply);
        }
        return;
    }
    ClientReceiveReply(RequestHybridGlobal(Request));
}
#if WITH_EDITOR
bool UGuLiCommanderSkillComponent::CopyServerRequestReceipt(FGuid RequestId, FGuLiActiveSkillRequest& OutRequest) const
{
	if (!GetOwner()->HasAuthority()) return false;
	const auto* Receipt = Receipts.Find(RequestId);
	if (!Receipt) return false;
	OutRequest = Receipt->Request;
	return true;
}
#endif
void UGuLiCommanderSkillComponent::ClientReceiveReply(const FGuLiActiveSkillReply& Reply)
{
    LastReply=Reply;
    // Publish the new deadline before HUD subscribers read their shared display state.
    if (!Reply.Units.IsEmpty() && Reply.MatchEpoch==RuntimeEpoch && BoundSelection.IsValid())
        RefreshSelectedUnitSkills(BoundSelection->GetSelectionState());
    OnSkillReply.Broadcast(Reply);
}
