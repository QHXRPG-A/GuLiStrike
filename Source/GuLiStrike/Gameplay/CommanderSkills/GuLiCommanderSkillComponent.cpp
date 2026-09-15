#include "Gameplay/CommanderSkills/GuLiCommanderSkillComponent.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Gameplay/Skills/GuLiSkillTargeting.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"

UGuLiCommanderSkillComponent::UGuLiCommanderSkillComponent() { SetIsReplicatedByDefault(true); }
AGuLiBattlePlayerState& UGuLiCommanderSkillComponent::PlayerState() const { return *CastChecked<AGuLiBattlePlayerState>(GetOwner()); }
AGuLiCommanderPlayerController* UGuLiCommanderSkillComponent::Commander() const { return Cast<AGuLiCommanderPlayerController>(GetOwner()->GetOwner()); }
void UGuLiCommanderSkillComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UGuLiCommanderSkillComponent, Catalog, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UGuLiCommanderSkillComponent, GlobalRuntime, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UGuLiCommanderSkillComponent, SelectedUnitRuntime, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UGuLiCommanderSkillComponent, RuntimeSelectionRevision, COND_OwnerOnly);
}
void UGuLiCommanderSkillComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetOwner()->HasAuthority())
	{
		PlayerState().OnCommanderPlayerStateChanged.AddDynamic(this, &ThisClass::HandlePlayerStateChanged);
		HandlePlayerStateChanged();
	}
}
void UGuLiCommanderSkillComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	PlayerState().OnCommanderPlayerStateChanged.RemoveDynamic(this, &ThisClass::HandlePlayerStateChanged);
	if (BoundSelection.IsValid()) BoundSelection->OnSelectionChanged.RemoveAll(this);
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
}
void UGuLiCommanderSkillComponent::RefreshSelectedUnitSkills(const FGuLiCommanderSelectionState& Selection)
{
	SelectedUnitRuntime.Reset(); RuntimeSelectionRevision = Selection.SelectionRevision;
	auto& Authority = *GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
	TSet<uint32> Seen;
	for (const auto& Cohort : Selection.Cohorts) for (auto Id : Cohort.MemberIds)
	{
		if (Seen.Contains(Id.Value)) continue;
		Seen.Add(Id.Value);
		FGuLiUnitSkillRuntimeView View; View.SoldierId = Id;
		if (Authority.QueryUnitSkillRuntime(Id, *Catalog, View.Runtime)) SelectedUnitRuntime.Add(View);
	}
	GetOwner()->ForceNetUpdate();
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
	auto* PC = Commander();
	if (!PC || !PC->IsLocalController() || !PC->CanIssueCommanderOrders() || !Catalog) return;
	auto& Sync = *PC->GetCommanderNetSyncComponent();
	if (Sync.HasUnresolvedSelectionIntent()) return;
	const auto& Selection = Sync.GetSelectionState();
	bool bHasSkill = false;
	for (TActorIterator<AGuLiSoldierStateReplicator> It(GetWorld()); It; ++It)
	{
		for (const auto& Cohort : Selection.Cohorts) for (auto Id : Cohort.MemberIds)
		{
			const auto* Soldier = It->FindSoldierState(Id);
			if (Soldier && Catalog->FindUnitSkill(Soldier->UnitTypeId)) { bHasSkill = true; break; }
		}
		break;
	}
	if (!bHasSkill) return;
	auto Request = MakeLocalRequest(bHasGroundPoint, GroundPoint); Request.SelectionRevision = Selection.SelectionRevision;
	ServerRequestSkill(Request);
}
void UGuLiCommanderSkillComponent::ActivateGlobalSkill(FName SkillId, bool bHasGroundPoint, FVector GroundPoint)
{
	auto* PC = Commander();
	if (!PC || !PC->IsLocalController() || !PC->CanIssueCommanderOrders() || !Catalog || !Catalog->GlobalSkills.Contains(SkillId)) return;
	auto Request = MakeLocalRequest(bHasGroundPoint, GroundPoint); Request.GlobalSkillId = SkillId;
	ServerRequestSkill(Request);
}
int32 UGuLiCommanderSkillComponent::GetGlobalSkillLevel(FName SkillId) const
{
	const auto* Runtime = GlobalRuntime.FindByPredicate([SkillId](const auto& Item) { return Item.SkillId == SkillId; });
	return Runtime ? Runtime->Level : 1;
}
bool UGuLiCommanderSkillComponent::SetServerGlobalSkillLevel(FName SkillId, int32 Level)
{
	if (!GetOwner()->HasAuthority() || !SynchronizeEpoch()) return false;
	auto* Runtime = GlobalRuntime.FindByPredicate([SkillId](const auto& Item) { return Item.SkillId == SkillId; });
	const auto* Definition = Catalog->FindSkill(SkillId);
	if (!Runtime || !Definition || Level < 1 || Level > Definition->MaximumLevel) return false;
	Runtime->Level = Level; GetOwner()->ForceNetUpdate(); return true;
}
void UGuLiCommanderSkillComponent::ServerRequestSkill_Implementation(const FGuLiActiveSkillRequest& Request)
{ ClientReceiveReply(ExecuteServerRequest(Request)); }
void UGuLiCommanderSkillComponent::ClientReceiveReply_Implementation(const FGuLiActiveSkillReply& Reply)
{ LastReply = Reply; OnSkillReply.Broadcast(Reply); }

FGuLiActiveSkillReply UGuLiCommanderSkillComponent::ExecuteServerRequest(const FGuLiActiveSkillRequest& Request)
{
	FGuLiActiveSkillReply Reply; Reply.RequestId = Request.RequestId; Reply.MatchEpoch = Request.MatchEpoch;
	auto* PC = Commander();
	if (!GetOwner()->HasAuthority() || !PC || !PC->CanIssueCommanderOrders() || !Request.RequestId.IsValid()
		|| Request.GroundPoint.ContainsNaN() || Request.Command > EGuLiActiveSkillCommand::Cancel || bExecuting || !SynchronizeEpoch()
		|| Request.MatchEpoch != RuntimeEpoch)
	{ Reply.Error = TEXT("Skill request has invalid authority, identity, command, or match."); return Reply; }
	if (const auto* Receipt = Receipts.Find(Request.RequestId))
	{
		if (Receipt->Request.HasSameContent(Request)) return Receipt->Reply;
		Reply.Error = TEXT("Request ID was already used for different skill content."); return Reply;
	}
	TGuardValue<bool> ExecutingGuard(bExecuting, true);
	bool bGroundValid = false; FVector Ground = FVector::ZeroVector;
	if (Request.bHasGroundPoint) bGroundValid = GuLiSkillTargeting::ResolveGround(*GetWorld(), Request.GroundPoint, Ground);
	if (Request.GlobalSkillId.IsNone())
	{
		const auto& Selection = PC->GetCommanderNetSyncComponent()->GetSelectionState();
		if (Request.Command != EGuLiActiveSkillCommand::Activate || Request.CastId.IsValid()) Reply.Error = TEXT("Unit skills accept one activation command.");
		else if (Selection.SelectionRevision != Request.SelectionRevision)
		{ Reply.Code = EGuLiActiveSkillResultCode::StaleSelection; Reply.Error = TEXT("Selection changed before the skill request."); }
		else
		{
			GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>()->ExecuteSelectedUnitSkills(
				PlayerState(), Selection, *Catalog, Request.RequestId, bGroundValid, Ground, Reply.Units);
			Reply.Code = Reply.Units.ContainsByPredicate([](const auto& Item) { return Item.Code == EGuLiActiveSkillResultCode::Succeeded; })
				? EGuLiActiveSkillResultCode::Succeeded : EGuLiActiveSkillResultCode::Ineligible;
			RefreshSelectedUnitSkills(Selection);
		}
	}
	else
	{
		const auto* Definition = Catalog->FindSkill(Request.GlobalSkillId);
		auto* Runtime = GlobalRuntime.FindByPredicate([&Request](const auto& Item) { return Item.SkillId == Request.GlobalSkillId; });
		const bool bStarting = Request.Command == EGuLiActiveSkillCommand::Activate;
		if (!Definition || !Runtime) { Reply.Code = EGuLiActiveSkillResultCode::NoSkill; Reply.Error = TEXT("Global skill is not granted."); }
		else if (bStarting && Runtime->ReadyAt > GetWorld()->GetTimeSeconds()) Reply.Code = EGuLiActiveSkillResultCode::Cooldown;
		else if ((!bStarting && (Definition->TargetMode != EGuLiActiveSkillTargetMode::TwoPoint || !Request.CastId.IsValid() || Runtime->ActiveCastId != Request.CastId))
			|| (bStarting && Request.CastId.IsValid())) Reply.Error = TEXT("Skill continuation has no matching owned cast.");
		else if (Request.Command != EGuLiActiveSkillCommand::Cancel && Definition->TargetMode != EGuLiActiveSkillTargetMode::Self && !bGroundValid)
			Reply.Code = EGuLiActiveSkillResultCode::InvalidGround;
		else
		{
			FGuLiActiveSkillExecutionContext Context;
			Context.Commander = &PlayerState(); Context.Scope = EGuLiActiveSkillScope::Global;
			Context.SkillId = Definition->SkillId; Context.RequestId = Request.RequestId; Context.CastId = Request.CastId;
			Context.Level = Runtime->Level; Context.Command = Request.Command; Context.GroundPoint = Ground;
			if (PC->GetPawn()) Context.SourceTransform = PC->GetPawn()->GetActorTransform();
			if (Definition->RangeCentimeters > 0 && Definition->TargetMode != EGuLiActiveSkillTargetMode::Self
				&& Request.Command != EGuLiActiveSkillCommand::Cancel
				&& FVector::DistSquared(Context.SourceTransform.GetLocation(), Ground) > FMath::Square(Definition->RangeCentimeters))
				Reply.Code = EGuLiActiveSkillResultCode::OutOfRange;
			else
			{
				Reply.Global = Definition->ExecutorClass->GetDefaultObject<UGuLiCommanderSkillExecutor>()->Execute(Context, Definition->Configuration);
				Reply.Code = Reply.Global.bSucceeded ? EGuLiActiveSkillResultCode::Succeeded : EGuLiActiveSkillResultCode::ExecutionFailed;
				Reply.Error = Reply.Global.Error;
				if (Reply.Global.bSucceeded)
				{
					if (bStarting) Runtime->ReadyAt = GetWorld()->GetTimeSeconds() + Definition->CooldownSeconds;
					Runtime->ActiveCastId = Request.Command == EGuLiActiveSkillCommand::Cancel ? FGuid() : Reply.Global.CastId;
					GetOwner()->ForceNetUpdate();
				}
			}
		}
	}
	Receipts.Add(Request.RequestId, {Request, Reply});
	return Reply;
}
