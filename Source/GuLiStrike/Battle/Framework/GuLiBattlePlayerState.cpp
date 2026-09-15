// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Gameplay/Ship/Build/GuLiShipBuildComponent.h"
#include "Gameplay/CommanderSkills/GuLiCommanderSkillComponent.h"

#include "Net/UnrealNetwork.h"
#include "Gameplay/Skills/GuLiSkillTags.h"
#include "Gameplay/Skills/GuLiArmySkillSubsystem.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Engine/World.h"

AGuLiBattlePlayerState::AGuLiBattlePlayerState()
{
	ShipBuild = CreateDefaultSubobject<UGuLiShipBuildComponent>(TEXT("ShipBuild"));
	CommanderSkills = CreateDefaultSubobject<UGuLiCommanderSkillComponent>(TEXT("CommanderSkills"));
}

void AGuLiBattlePlayerState::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
		if (auto* Skills = GetWorld()->GetSubsystem<UGuLiArmySkillSubsystem>())
			Skills->OnWeaponLoadoutCommitted().AddUObject(this, &ThisClass::HandleArmyWeaponLoadoutCommitted);
}

void AGuLiBattlePlayerState::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld())
		if (auto* Skills = GetWorld()->GetSubsystem<UGuLiArmySkillSubsystem>())
			Skills->OnWeaponLoadoutCommitted().RemoveAll(this);
	WeaponRequests.Reset(); WeaponRequestOrder.Reset();
	Super::EndPlay(EndPlayReason);
}

TArray<FGuLiWeaponChannelView> AGuLiBattlePlayerState::GetWeaponChannels(const EGuLiWeaponDomain Domain) const
{
	TArray<FGuLiWeaponChannelView> Views;
	const UWorld* World = GetWorld();
	const auto* State = World ? World->GetGameState<AGuLiBattleGameState>() : nullptr;
	const auto* Skills = World ? World->GetSubsystem<UGuLiArmySkillSubsystem>() : nullptr;
	const auto* Data = World ? World->GetSubsystem<UGuLiCommanderDataSubsystem>() : nullptr;
	if (Domain != EGuLiWeaponDomain::Army || !State || !Skills || !Data || !IsCommander()) return Views;
	for (const auto& Profile : Skills->GetResolvedSkills())
	{
		if (Profile.Team != Team) continue;
		auto& View = Views.AddDefaulted_GetRef();
		View.Binding = FGuLiWeaponBindingKey::Army(State->GetMatchEpoch(), Team, Profile.UnitTypeId, Profile.SlotId);
		View.SkillId = Profile.SkillId;
		View.bUnlocked = Profile.bUnlocked; View.bEquipped = Profile.bEquipped;
		View.Damage = Profile.Damage; View.AttackRatePerSecond = Profile.AttackRatePerSecond;
		View.RangeCentimeters = Profile.RangeCentimeters;
		View.LoadoutRevision = Skills->GetLoadoutRevision(); View.ProfileRevision = Profile.Revision;
		if (const auto* Definition = Data->GetSkillDefinitions().FindByPredicate(
			[&](const auto& Entry) { return Entry.SkillId == Profile.SkillId; })) View.DisplayName = Definition->DisplayName;
		for (const auto& Config : Data->GetUnitSkillConfigs())
			if (Config.UnitTypeId == Profile.UnitTypeId && Config.SlotId == Profile.SlotId)
				View.CompatibleSkillIds.AddUnique(Config.SkillId);
		View.CompatibleSkillIds.Sort([](const FName A, const FName B) { return A.LexicalLess(B); });
	}
	return Views;
}

void AGuLiBattlePlayerState::ServerRequestEquipWeapon_Implementation(const FGuid RequestId,
	const FGuLiWeaponBindingKey Binding, const FName SkillId, const int64 ExpectedLoadoutRevision)
{
	ClientReceiveWeaponChangeResult(ProcessEquipWeaponRequest(RequestId, Binding, SkillId, ExpectedLoadoutRevision));
}

FGuLiWeaponChangeResult AGuLiBattlePlayerState::ProcessEquipWeaponRequest(const FGuid RequestId,
	const FGuLiWeaponBindingKey& Binding, const FName SkillId, const int64 ExpectedLoadoutRevision)
{
	FGuLiWeaponChangeResult Result;
	Result.RequestId = RequestId; Result.Binding = Binding; Result.RequestedSkillId = SkillId;
	UWorld* World = GetWorld();
	const auto* State = World ? World->GetGameState<AGuLiBattleGameState>() : nullptr;
	auto* Skills = World ? World->GetSubsystem<UGuLiArmySkillSubsystem>() : nullptr;
	if (State && WeaponRequestEpoch != State->GetMatchEpoch())
	{
		WeaponRequestEpoch = State->GetMatchEpoch();
		WeaponRequests.Reset(); WeaponRequestOrder.Reset(); NextWeaponRequestSeconds = 0.0;
	}
	if (const auto* Prior = WeaponRequests.Find(RequestId))
	{
		if (Prior->Result.Binding == Binding && Prior->Result.RequestedSkillId == SkillId
			&& Prior->ExpectedRevision == ExpectedLoadoutRevision)
			return Prior->Result;
		else
			Result.Message = TEXT("RequestId has already been used for a different equipment request.");
		return Result;
	}
	const FString TypeText = Binding.SubjectId.ToString();
	const int64 UnitTypeId = TypeText.IsNumeric() && TypeText.Len() <= 5 ? FCString::Atoi64(*TypeText) : 0;
	if (!RequestId.IsValid() || !Binding.IsWellFormed() || !State || !Skills
		|| Binding.MatchEpoch != State->GetMatchEpoch() || Binding.Team != Team
		|| Binding.Domain != EGuLiWeaponDomain::Army || UnitTypeId < 1 || UnitTypeId > MAX_uint16
		|| Binding.SubjectId != FName(*FString::FromInt(static_cast<int32>(UnitTypeId)))
		|| ExpectedLoadoutRevision < 1 || ExpectedLoadoutRevision > MAX_uint32)
		Result.Message = TEXT("Invalid, stale, foreign, or not-yet-supported equipment binding.");
	else if (World->GetTimeSeconds() < NextWeaponRequestSeconds)
		Result.Message = TEXT("Equipment requests are rate limited; retry after the current commit.");
	else
	{
		NextWeaponRequestSeconds = World->GetTimeSeconds() + 0.05;
		if (Skills->EquipWeapon(*this, static_cast<uint16>(UnitTypeId), Binding.SlotId, SkillId,
			static_cast<uint32>(ExpectedLoadoutRevision), Result.Message))
		{
			Result.Status = EGuLiWeaponChangeStatus::AwaitingCommit;
			Result.Message = TEXT("Accepted; awaiting the authority simulation step.");
		}
	}
	Result.LoadoutRevision = Skills ? Skills->GetLoadoutRevision() : 0;
	if (RequestId.IsValid())
	{
		while (WeaponRequestOrder.Num() >= 128)
		{
			const int32 EvictIndex = WeaponRequestOrder.IndexOfByPredicate([&](const FGuid Id)
				{ return WeaponRequests.FindChecked(Id).Result.Status != EGuLiWeaponChangeStatus::AwaitingCommit; });
			if (EvictIndex == INDEX_NONE) break;
			WeaponRequests.Remove(WeaponRequestOrder[EvictIndex]);
			WeaponRequestOrder.RemoveAt(EvictIndex, 1, EAllowShrinking::No);
		}
		if (WeaponRequestOrder.Num() < 128)
		{
			FWeaponRequestRecord Record; Record.ExpectedRevision = ExpectedLoadoutRevision; Record.Result = Result;
			WeaponRequests.Add(RequestId, MoveTemp(Record)); WeaponRequestOrder.Add(RequestId);
		}
	}
	return Result;
}

void AGuLiBattlePlayerState::HandleArmyWeaponLoadoutCommitted(const uint32 Revision)
{
	const auto* State = GetWorld() ? GetWorld()->GetGameState<AGuLiBattleGameState>() : nullptr;
	const auto* Skills = GetWorld() ? GetWorld()->GetSubsystem<UGuLiArmySkillSubsystem>() : nullptr;
	if (!HasAuthority() || !State || !Skills) return;
	TArray<FGuLiWeaponChangeResult> Replies;
	for (auto& Pair : WeaponRequests)
	{
		auto& Result = Pair.Value.Result;
		if (Result.Status != EGuLiWeaponChangeStatus::AwaitingCommit) continue;
		const auto* Profile = Skills->FindResolvedSkill(Result.Binding.Team,
			static_cast<uint16>(FCString::Atoi(*Result.Binding.SubjectId.ToString())), Result.Binding.SlotId);
		const bool bApplied = Result.Binding.MatchEpoch == State->GetMatchEpoch() && Profile
			&& Profile->bUnlocked && (Result.RequestedSkillId.IsNone() ? !Profile->bEquipped
				: Profile->bEquipped && Profile->SkillId == Result.RequestedSkillId);
		Result.Status = bApplied ? EGuLiWeaponChangeStatus::Committed : EGuLiWeaponChangeStatus::Rejected;
		Result.LoadoutRevision = Revision;
		Result.Message = bApplied ? TEXT("Equipment committed.") : TEXT("Equipment invalidated before commit.");
		Replies.Add(Result);
	}
	// Notify after walking the cache: a local Blueprint callback may issue another request.
	for (const auto& Reply : Replies) ClientReceiveWeaponChangeResult(Reply);
}

void AGuLiBattlePlayerState::ClientReceiveWeaponChangeResult_Implementation(const FGuLiWeaponChangeResult& Result)
{
	LastWeaponChangeResult = Result;
	OnWeaponChangeResult.Broadcast(Result);
}

bool AGuLiBattlePlayerState::ExecuteArmySkillCommand(const FGuLiArmySkillCommand& Command, FString& OutError)
{
	if (!HasAuthority()) { OutError = TEXT("Army commands execute only on the server."); return false; }
	auto* Skills = GetWorld()->GetSubsystem<UGuLiArmySkillSubsystem>();
	if (!Skills) { OutError = TEXT("Army skill runtime is unavailable."); return false; }
	return Skills->ExecuteCommand(*this, Command, OutError);
}


void AGuLiBattlePlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AGuLiBattlePlayerState, PlayerGuid);
	DOREPLIFETIME(AGuLiBattlePlayerState, Team);
	DOREPLIFETIME(AGuLiBattlePlayerState, CommanderRole);
	DOREPLIFETIME(AGuLiBattlePlayerState, SlotIndex);
	DOREPLIFETIME(AGuLiBattlePlayerState, bBattleReady);
	DOREPLIFETIME(AGuLiBattlePlayerState, bSyncReady);
	DOREPLIFETIME_CONDITION(AGuLiBattlePlayerState, ResourcePrivateState, COND_OwnerOnly);
}

bool AGuLiBattlePlayerState::SetServerResourcePrivateState(FGuLiTeamResourcePrivateState NewState)
{
	check(HasAuthority());
	NewState.SortByStableId();
	if (ResourcePrivateState.HasSamePayload(NewState))
	{
		return false;
	}
	const FGuLiResourceAmounts PreviousInventory = ResourcePrivateState.Inventory;
	NewState.Revision = ResourcePrivateState.Revision == MAX_uint32
		? 1u
		: ResourcePrivateState.Revision + 1u;
	ResourcePrivateState = MoveTemp(NewState);
	ForceNetUpdate();
	OnResourcePrivateStateChanged.Broadcast();
	if (PreviousInventory.Blue != ResourcePrivateState.Inventory.Blue
		|| PreviousInventory.Red != ResourcePrivateState.Inventory.Red
		|| PreviousInventory.Revision != ResourcePrivateState.Inventory.Revision)
	{
		OnResourceInventoryChanged.Broadcast(ResourcePrivateState.Inventory);
	}
	return true;
}

void AGuLiBattlePlayerState::OnRep_ResourcePrivateState(
	FGuLiTeamResourcePrivateState PreviousState)
{
	OnResourcePrivateStateChanged.Broadcast();
	if (PreviousState.Inventory.Blue != ResourcePrivateState.Inventory.Blue
		|| PreviousState.Inventory.Red != ResourcePrivateState.Inventory.Red
		|| PreviousState.Inventory.Revision != ResourcePrivateState.Inventory.Revision)
	{
		OnResourceInventoryChanged.Broadcast(ResourcePrivateState.Inventory);
	}
}

void AGuLiBattlePlayerState::CopyProperties(APlayerState* PlayerState)
{
	Super::CopyProperties(PlayerState);

	if (AGuLiBattlePlayerState* NewPlayerState = Cast<AGuLiBattlePlayerState>(PlayerState))
	{
		NewPlayerState->PlayerGuid = PlayerGuid;
		NewPlayerState->Team = Team;
		NewPlayerState->CommanderRole = CommanderRole;
		NewPlayerState->SlotIndex = SlotIndex;
		// 身份跨通用/指挥官派生类迁移，公共与士兵流握手都必须重新完成。
		NewPlayerState->bBattleReady = false;
		NewPlayerState->bSyncReady = false;
		NewPlayerState->ShipBuild->CopyMatchStateFrom(*ShipBuild);
		NewPlayerState->CommanderSkills->CopyMatchStateFrom(*CommanderSkills);
	}
}

// FindInactivePlayer 会在恢复的 PS 上调用本函数，参数可能是本次登录刚创建、尚无自定义身份的 PS。
// 不能用这个空身份覆盖已经找回的 GUID/席位；无论是否复制身份，两种就绪资格都必须重新确认。
void AGuLiBattlePlayerState::OverrideWith(APlayerState* PlayerState)
{
	Super::OverrideWith(PlayerState);

	if (const AGuLiBattlePlayerState* IncomingPlayerState = Cast<AGuLiBattlePlayerState>(PlayerState))
	{
		if (IncomingPlayerState->PlayerGuid.IsValid())
		{
			PlayerGuid = IncomingPlayerState->PlayerGuid;
			Team = IncomingPlayerState->Team;
			CommanderRole = IncomingPlayerState->CommanderRole;
			SlotIndex = IncomingPlayerState->SlotIndex;
		}
		// A newly logged-in empty state must not erase the recovered match build.
		if (IncomingPlayerState->ShipBuild->GetBuildState().MatchEpoch != 0)
			ShipBuild->CopyMatchStateFrom(*IncomingPlayerState->ShipBuild);
		if (!IncomingPlayerState->CommanderSkills->GetGlobalSkills().IsEmpty())
			CommanderSkills->CopyMatchStateFrom(*IncomingPlayerState->CommanderSkills);
	}
	bBattleReady = false;
	bSyncReady = false;
	NotifyStateChanged();
}

void AGuLiBattlePlayerState::EnsureServerPlayerGuid()
{
	if (!HasAuthority())
	{
		return;
	}

	if (!PlayerGuid.IsValid())
	{
		PlayerGuid = FGuid::NewGuid();
		NotifyStateChanged();
		ForceNetUpdate();
	}
}

void AGuLiBattlePlayerState::SetServerRoleAssignment(
	EGuLiTeam NewTeam,
	EGuLiCommanderRole NewRole,
	uint8 NewSlotIndex)
{
	if (!HasAuthority())
	{
		return;
	}

	Team = NewTeam;
	CommanderRole = NewRole;
	SlotIndex = NewSlotIndex;
	bBattleReady = false;
	bSyncReady = false;
	NotifyStateChanged();
	ForceNetUpdate();
}

// 公共握手仅开放通用玩法资格；此函数故意不写 bSyncReady。
void AGuLiBattlePlayerState::SetServerBattleReady(bool bNewBattleReady)
{
	if (!HasAuthority() || bBattleReady == bNewBattleReady)
	{
		return;
	}

	bBattleReady = bNewBattleReady;
	NotifyStateChanged();
	ForceNetUpdate();
}

void AGuLiBattlePlayerState::SetServerSoldierStreamReady(bool bNewSyncReady)
{
	if (!HasAuthority() || bSyncReady == bNewSyncReady)
	{
		return;
	}

	bSyncReady = bNewSyncReady;
	NotifyStateChanged();
	ForceNetUpdate();
}

void AGuLiBattlePlayerState::SetServerObserver()
{
	SetServerRoleAssignment(EGuLiTeam::Unassigned, EGuLiCommanderRole::Observer, InvalidSlotIndex);
}

void AGuLiBattlePlayerState::NotifyStateChanged()
{
	OnCommanderPlayerStateChanged.Broadcast();
}

// 客户端副本更新后的本地消费入口；不能在这里向服务器反向提交权威分配。
void AGuLiBattlePlayerState::OnRep_Assignment()
{
	NotifyStateChanged();
}

void AGuLiBattlePlayerState::OnRep_BattleReady()
{
	NotifyStateChanged();
}

void AGuLiBattlePlayerState::OnRep_SyncReady()
{
	NotifyStateChanged();
}
