// Copyright Epic Games, Inc. All Rights Reserved.

#include "Development/GM/GuLiGMPanelModel.h"
#include "Gameplay/Stronghold/GuLiStrongholdDiagnostics.h"

#include "Battle/Framework/GuLiBattlePlayerController.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Commander/Mass/GuLiSoldierCombat.h"
#include "Commander/Presentation/GuLiCommanderCameraPawn.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Gameplay/Skills/GuLiArmySkillSubsystem.h"
#include "Gameplay/Skills/GuLiSkillGMUtilities.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Gameplay/Tuning/GuLiRuntimeTuningSubsystem.h"

namespace GuLiGMPanel::Private
{
	bool ParsePositiveUnit(const FString& Text, uint16& OutUnit)
	{
		int32 Value = 0;
		if (!LexTryParseString(Value, *Text.TrimStartAndEnd()) || Value <= 0 || Value > MAX_uint16)
		{
			return false;
		}
		OutUnit = static_cast<uint16>(Value);
		return true;
	}

	bool ParsePositiveId(const FString& Text, uint32& OutId)
	{
		OutId = 0u;
		return LexTryParseString(OutId, *Text.TrimStartAndEnd()) && OutId != 0u;
	}

	bool ParseFiniteFloat(const FString& Text, float& OutValue)
	{
		double Parsed = 0.0;
		if (!GuLiRuntimeTuning::TryParseFiniteNumber(Text.TrimStartAndEnd(), Parsed)
			|| Parsed < -static_cast<double>(MAX_flt)
			|| Parsed > static_cast<double>(MAX_flt))
		{
			return false;
		}
		OutValue = static_cast<float>(Parsed);
		return FMath::IsFinite(OutValue);
	}

	bool ParseSlot(const FString& SlotText, FName& OutSlot)
	{
		const FString Trimmed = SlotText.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			OutSlot = NAME_None;
			return false;
		}
		OutSlot = FName(*Trimmed);
		return !OutSlot.IsNone();
	}

	bool IsPlayableTeam(const EGuLiTeam Team)
	{
		return Team == EGuLiTeam::Red || Team == EGuLiTeam::Blue;
	}

	AGuLiBattlePlayerState* FindCommander(UWorld* World, const EGuLiTeam Team)
	{
		if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_Client)
		{
			return nullptr;
		}
		if (const AGameStateBase* GameState = World->GetGameState())
		{
			for (APlayerState* PlayerState : GameState->PlayerArray)
			{
				AGuLiBattlePlayerState* Candidate = Cast<AGuLiBattlePlayerState>(PlayerState);
				if (Candidate && Candidate->IsCommander() && Candidate->GetTeam() == Team)
				{
					return Candidate;
				}
			}
		}
		return nullptr;
	}

	FActionResult ExecuteArmyCommand(
		const FModel& Model,
		const EGuLiTeam Team,
		const FGuLiArmySkillCommand& Command,
		const FString& Operation)
	{
		const FAccessPolicy Access = Model.GetAccessPolicy();
		if (!Access.bCanMutateAuthorityState)
		{
			return FActionResult::Failure(Operation + TEXT("被拒绝"), Access.AuthorityUnavailableReason);
		}
		AGuLiBattlePlayerState* Commander = FindCommander(Model.GetWorld(), Team);
		if (!Commander)
		{
			return FActionResult::Failure(
				Operation + TEXT("被拒绝"),
				FString::Printf(TEXT("队伍 %s 没有已就绪指挥官；技能 GM 必须经过真实 ServerOnly GAS 能力。"), LexToString(Team)));
		}
		FString Error;
		if (!Commander->ExecuteArmySkillCommand(Command, Error))
		{
			return FActionResult::Failure(Operation + TEXT("被拒绝"), Error);
		}
		return FActionResult::Success(
			Operation + TEXT("已接受"),
			TEXT("accepted changes publish on the next authority step"),
			true);
	}

	const TCHAR* NavigationStateToString(const EGuLiSoldierNavigationState State)
	{
		switch (State)
		{
		case EGuLiSoldierNavigationState::Idle: return TEXT("Idle");
		case EGuLiSoldierNavigationState::Normal: return TEXT("Normal");
		case EGuLiSoldierNavigationState::CenterlineRecovery: return TEXT("CenterlineRecovery");
		case EGuLiSoldierNavigationState::PersonalPathRecovery: return TEXT("PersonalPathRecovery");
		case EGuLiSoldierNavigationState::Arrived: return TEXT("Arrived");
		case EGuLiSoldierNavigationState::Blocked: return TEXT("Blocked");
		default: return TEXT("Unknown");
		}
	}

	const TCHAR* NavigationFailureToString(const EGuLiSoldierNavigationFailure Failure)
	{
		switch (Failure)
		{
		case EGuLiSoldierNavigationFailure::None: return TEXT("None");
		case EGuLiSoldierNavigationFailure::NavigationUnavailable: return TEXT("NavigationUnavailable");
		case EGuLiSoldierNavigationFailure::SurfaceMoveFailed: return TEXT("SurfaceMoveFailed");
		case EGuLiSoldierNavigationFailure::ExcessiveHeightDelta: return TEXT("ExcessiveHeightDelta");
		case EGuLiSoldierNavigationFailure::PersonalPathFailed: return TEXT("PersonalPathFailed");
		case EGuLiSoldierNavigationFailure::FinalSlotInvalidated: return TEXT("FinalSlotInvalidated");
		default: return TEXT("Unknown");
		}
	}

	FString RuntimeResultDetail(const FGuLiRuntimeTuningResult& Result)
	{
		if (!Result.bSuccess)
		{
			return Result.Error;
		}
		return FString::Printf(
			TEXT("key=%s baseline=%.17g old=%.17g effective=%.17g source=%s changed=%d applied=%d"),
			*Result.Key.ToString(),
			Result.Baseline,
			Result.PreviousEffective,
			Result.Effective,
			FGuLiRuntimeTuningRegistry::LexToString(Result.Source),
			Result.bChanged ? 1 : 0,
			Result.AppliedInstanceCount);
	}
}

bool GuLiGMPanel::FConfirmationGate::ConsumeOrArm(
	const EConfirmationAction Action,
	const double NowSeconds)
{
	if (Action == EConfirmationAction::None)
	{
		Cancel();
		return false;
	}
	if (IsArmed(Action, NowSeconds))
	{
		Cancel();
		return true;
	}
	ArmedAction = Action;
	ExpiresAtSeconds = NowSeconds + ConfirmationLifetimeSeconds;
	return false;
}

bool GuLiGMPanel::FConfirmationGate::IsArmed(
	const EConfirmationAction Action,
	const double NowSeconds) const
{
	return Action != EConfirmationAction::None
		&& ArmedAction == Action
		&& NowSeconds <= ExpiresAtSeconds;
}

void GuLiGMPanel::FConfirmationGate::Cancel()
{
	ArmedAction = EConfirmationAction::None;
	ExpiresAtSeconds = 0.0;
}

GuLiGMPanel::FActionResult GuLiGMPanel::FActionResult::Success(
	FString InSummary,
	FString InDetail,
	const bool bInPending,
	const int32 InAppliedCount)
{
	FActionResult Result;
	Result.bSuccess = true;
	Result.bPending = bInPending;
	Result.AppliedCount = InAppliedCount;
	Result.Summary = MoveTemp(InSummary);
	Result.Detail = MoveTemp(InDetail);
	return Result;
}

GuLiGMPanel::FActionResult GuLiGMPanel::FActionResult::Failure(
	FString InSummary,
	FString InDetail)
{
	FActionResult Result;
	Result.Summary = MoveTemp(InSummary);
	Result.Detail = MoveTemp(InDetail);
	return Result;
}

FString GuLiGMPanel::FRuntimeTuningRow::ToSearchText() const
{
	return FString::Printf(TEXT("%s %s"), *Key.ToString(), *Source);
}

FString GuLiGMPanel::FRuntimeTuningRow::ToDisplayText() const
{
	return FString::Printf(
		TEXT("%s\n生效 %.6g  基线 %.6g  来源 %s  范围 [%.6g, %.6g]%s  当前应用对象 %d"),
		*Key.ToString(),
		Effective,
		Baseline,
		*Source,
		Minimum,
		Maximum,
		bIntegral ? TEXT("  整数") : TEXT(""),
		AppliedInstanceCount);
}

FString GuLiGMPanel::FSkillProfileRow::ToSearchText() const
{
	return FString::Printf(
		TEXT("%s %u %s %s %s"),
		LexToString(Team),
		UnitTypeId,
		*SlotId.ToString(),
		*SkillId.ToString(),
		*ExecutorId.ToString());
}

FString GuLiGMPanel::FSkillProfileRow::ToDisplayText() const
{
	return FString::Printf(
		TEXT("%s  Unit %u  %s  [%s]\n%s / %s  伤害 %.6g  攻速 %.6g/s  射程 %.6gcm  Rev %u  来源: Get 详情"),
		LexToString(Team),
		UnitTypeId,
		*SlotId.ToString(),
		bPendingChanges ? TEXT("pending") : TEXT("committed"),
		*SkillId.ToString(),
		*ExecutorId.ToString(),
		Damage,
		AttackRatePerSecond,
		RangeCentimeters,
		Revision);
}

FString GuLiGMPanel::FSkillSourceRow::ToSearchText() const
{
	return FString::Printf(TEXT("%s %s %s"), LexToString(Team), *Label, *SourceId.ToString());
}

FString GuLiGMPanel::FSkillSourceRow::ToDisplayText() const
{
	return FString::Printf(
		TEXT("%s  %s\n%s  modifier=%d replacement=%d unlock=%d"),
		LexToString(Team),
		*Label,
		*SourceId.ToString(EGuidFormats::DigitsWithHyphensLower),
		ModifierCount,
		ReplacementCount,
		UnlockCount);
}

int32 GuLiGMPanel::GetPageCount(const int32 ItemCount)
{
	return FMath::Max(1, FMath::DivideAndRoundUp(FMath::Max(0, ItemCount), ItemsPerPage));
}

int32 GuLiGMPanel::ClampPageIndex(const int32 RequestedPageIndex, const int32 ItemCount)
{
	return FMath::Clamp(RequestedPageIndex, 0, GetPageCount(ItemCount) - 1);
}

GuLiGMPanel::FPanelLayout GuLiGMPanel::CalculatePanelLayout(const FVector2D& ViewportSize)
{
	const FVector2D SafeViewport(
		FMath::Max(1.0f, ViewportSize.X),
		FMath::Max(1.0f, ViewportSize.Y));
	const float Margin = FMath::Clamp(FMath::Min(SafeViewport.X, SafeViewport.Y) * 0.02f, 12.0f, 32.0f);
	const float MaximumWidth = FMath::Max(1.0f, SafeViewport.X - Margin * 2.0f);
	const float MaximumHeight = FMath::Max(1.0f, SafeViewport.Y - Margin * 2.0f);

	FPanelLayout Layout;
	Layout.Width = FMath::Min(FMath::Clamp(SafeViewport.X * 0.42f, 520.0f, 920.0f), MaximumWidth);
	Layout.Height = FMath::Min(FMath::Clamp(SafeViewport.Y * 0.84f, 480.0f, 980.0f), MaximumHeight);
	Layout.RightMargin = Margin;
	return Layout;
}

GuLiGMPanel::FAccessPolicy GuLiGMPanel::ResolveAccessPolicy(
	const ENetMode NetMode,
	const bool bHasGameWorld)
{
	FAccessPolicy Policy;
#if UE_BUILD_SHIPPING
	Policy.AuthorityUnavailableReason = TEXT("Shipping 构建不提供 GM 面板。");
	return Policy;
#else
	if (!bHasGameWorld)
	{
		Policy.AuthorityUnavailableReason = TEXT("当前没有可用的游戏 World。");
		return Policy;
	}
	const bool bAuthority = NetMode == NM_Standalone || NetMode == NM_ListenServer || NetMode == NM_DedicatedServer;
	Policy.bCanReadRuntimeRegistry = bAuthority;
	Policy.bCanReadSkillProfiles = true;
	Policy.bCanReadSkillSources = bAuthority;
	Policy.bCanMutateAuthorityState = bAuthority;
	Policy.bCanReadAuthorityDiagnostics = bAuthority;
	Policy.bCanRunLocalBenchmark = NetMode != NM_DedicatedServer;
	Policy.bCanToggleLocalCameraDebug = NetMode != NM_DedicatedServer;
	if (!bAuthority)
	{
		Policy.AuthorityUnavailableReason = TEXT("客户端不发送 GM RPC；全局 Registry、技能来源账本和权威导航诊断未复制。");
	}
	return Policy;
#endif
}

GuLiGMPanel::EToggleAction GuLiGMPanel::ResolveToggleAction(
	const bool bIsLocalController,
	const bool bHasViewportContext,
	const bool bPanelOpen)
{
	if (bPanelOpen)
	{
		return EToggleAction::Close;
	}
	return bIsLocalController && bHasViewportContext ? EToggleAction::Open : EToggleAction::None;
}

GuLiGMPanel::FInputRestorePolicy GuLiGMPanel::ResolveInputRestorePolicy(const bool bCommanderRole)
{
	FInputRestorePolicy Policy;
	Policy.bGameAndUI = bCommanderRole;
	Policy.bShowCursor = bCommanderRole;
	Policy.bEnableClickEvents = bCommanderRole;
	Policy.bEnableMouseOverEvents = bCommanderRole;
	return Policy;
}

const TCHAR* GuLiGMPanel::LexToString(const EGuLiTeam Team)
{
	switch (Team)
	{
	case EGuLiTeam::Red: return TEXT("Red");
	case EGuLiTeam::Blue: return TEXT("Blue");
	default: return TEXT("Unassigned");
	}
}

const TCHAR* GuLiGMPanel::LexToString(const ESkillAttribute Attribute)
{
	switch (Attribute)
	{
	case ESkillAttribute::Damage: return TEXT("damage");
	case ESkillAttribute::AttackRate: return TEXT("rate");
	case ESkillAttribute::Range: return TEXT("range");
	default: return TEXT("unknown");
	}
}

const TCHAR* GuLiGMPanel::LexToString(const EModifierOperation Operation)
{
	return Operation == EModifierOperation::Percent ? TEXT("percent") : TEXT("flat");
}

GuLiGMPanel::FModel::FModel(AGuLiBattlePlayerController* InController)
	: Controller(InController)
{
}

void GuLiGMPanel::FModel::SetController(AGuLiBattlePlayerController* InController)
{
	Controller = InController;
}

AGuLiBattlePlayerController* GuLiGMPanel::FModel::GetController() const
{
	return Controller.Get();
}

UWorld* GuLiGMPanel::FModel::GetWorld() const
{
	return Controller.IsValid() ? Controller->GetWorld() : nullptr;
}

GuLiGMPanel::FAccessPolicy GuLiGMPanel::FModel::GetAccessPolicy() const
{
	const UWorld* World = GetWorld();
	return ResolveAccessPolicy(World ? World->GetNetMode() : NM_MAX, World && World->IsGameWorld());
}

FString GuLiGMPanel::FModel::DescribeContext() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return TEXT("World: unavailable");
	}
	const TCHAR* NetMode = TEXT("Unknown");
	switch (World->GetNetMode())
	{
	case NM_Standalone: NetMode = TEXT("Standalone"); break;
	case NM_ListenServer: NetMode = TEXT("ListenServer"); break;
	case NM_Client: NetMode = TEXT("Client / read-only"); break;
	case NM_DedicatedServer: NetMode = TEXT("DedicatedServer"); break;
	default: break;
	}
	return FString::Printf(TEXT("World: %s    NetMode: %s"), *World->GetName(), NetMode);
}

TArray<GuLiGMPanel::FRuntimeTuningRow> GuLiGMPanel::FModel::QueryRuntimeTuning(
	FString& OutUnavailableReason) const
{
	OutUnavailableReason.Reset();
	TArray<FRuntimeTuningRow> Rows;
	const FAccessPolicy Access = GetAccessPolicy();
	if (!Access.bCanReadRuntimeRegistry)
	{
		OutUnavailableReason = Access.AuthorityUnavailableReason;
		return Rows;
	}
	const UWorld* World = GetWorld();
	const UGuLiRuntimeTuningSubsystem* Tuning = World ? World->GetSubsystem<UGuLiRuntimeTuningSubsystem>() : nullptr;
	if (!Tuning)
	{
		OutUnavailableReason = TEXT("RuntimeTuning Subsystem 尚未就绪。");
		return Rows;
	}
	int32 SoldierInstanceCount = 0;
	if (const UGuLiBattleAuthoritySubsystem* Authority = World->GetSubsystem<UGuLiBattleAuthoritySubsystem>())
	{
		SoldierInstanceCount = Authority->GetNavigationStats().Alive;
	}
	int32 ShipInstanceCount = 0;
	for (TActorIterator<AGuLiStrikeShip> It(World); It; ++It)
	{
		if (!It->IsActorBeingDestroyed() && It->HasAuthority())
		{
			++ShipInstanceCount;
		}
	}
	for (const FGuLiRuntimeTuningEntryView& Entry : Tuning->ListValues())
	{
		FRuntimeTuningRow& Row = Rows.AddDefaulted_GetRef();
		Row.Key = Entry.Key;
		Row.Minimum = Entry.Minimum;
		Row.Maximum = Entry.Maximum;
		Row.Baseline = Entry.Baseline;
		Row.Effective = Entry.Effective;
		Row.Source = FGuLiRuntimeTuningRegistry::LexToString(Entry.Source);
		Row.bIntegral = Entry.bIntegral;
		Row.AppliedInstanceCount = Entry.Key.ToString().StartsWith(TEXT("soldier."), ESearchCase::IgnoreCase)
			? SoldierInstanceCount
			: ShipInstanceCount;
	}
	Rows.Sort([](const FRuntimeTuningRow& A, const FRuntimeTuningRow& B)
	{
		return A.Key.LexicalLess(B.Key);
	});
	return Rows;
}

GuLiGMPanel::FActionResult GuLiGMPanel::FModel::GetRuntimeTuning(const FString& Key) const
{
	const FAccessPolicy Access = GetAccessPolicy();
	if (!Access.bCanReadRuntimeRegistry)
	{
		return FActionResult::Failure(TEXT("gs.GM.Get 不可用"), Access.AuthorityUnavailableReason);
	}
	UWorld* World = GetWorld();
	UGuLiRuntimeTuningSubsystem* Tuning = World ? World->GetSubsystem<UGuLiRuntimeTuningSubsystem>() : nullptr;
	if (!Tuning)
	{
		return FActionResult::Failure(TEXT("gs.GM.Get 失败"), TEXT("RuntimeTuning Subsystem 尚未就绪。"));
	}
	const FGuLiRuntimeTuningResult Result = Tuning->GetValue(Key);
	return Result.bSuccess
		? FActionResult::Success(TEXT("gs.GM.Get 成功"), Private::RuntimeResultDetail(Result), false, Result.AppliedInstanceCount)
		: FActionResult::Failure(TEXT("gs.GM.Get 被拒绝"), Result.Error);
}

GuLiGMPanel::FActionResult GuLiGMPanel::FModel::SetRuntimeTuning(
	const FString& Key,
	const FString& Value) const
{
	const FAccessPolicy Access = GetAccessPolicy();
	if (!Access.bCanMutateAuthorityState)
	{
		return FActionResult::Failure(TEXT("gs.GM.Set 被拒绝"), Access.AuthorityUnavailableReason);
	}
	UWorld* World = GetWorld();
	UGuLiRuntimeTuningSubsystem* Tuning = World ? World->GetSubsystem<UGuLiRuntimeTuningSubsystem>() : nullptr;
	if (!Tuning)
	{
		return FActionResult::Failure(TEXT("gs.GM.Set 失败"), TEXT("RuntimeTuning Subsystem 尚未就绪。"));
	}
	const FGuLiRuntimeTuningResult Result = Tuning->SetValue(Key, Value);
	return Result.bSuccess
		? FActionResult::Success(TEXT("gs.GM.Set 成功"), Private::RuntimeResultDetail(Result), false, Result.AppliedInstanceCount)
		: FActionResult::Failure(TEXT("gs.GM.Set 被拒绝"), Result.Error);
}

GuLiGMPanel::FActionResult GuLiGMPanel::FModel::ResetRuntimeTuning(const FString& KeyOrAll) const
{
	const FAccessPolicy Access = GetAccessPolicy();
	if (!Access.bCanMutateAuthorityState)
	{
		return FActionResult::Failure(TEXT("gs.GM.Reset 被拒绝"), Access.AuthorityUnavailableReason);
	}
	UWorld* World = GetWorld();
	UGuLiRuntimeTuningSubsystem* Tuning = World ? World->GetSubsystem<UGuLiRuntimeTuningSubsystem>() : nullptr;
	if (!Tuning)
	{
		return FActionResult::Failure(TEXT("gs.GM.Reset 失败"), TEXT("RuntimeTuning Subsystem 尚未就绪。"));
	}
	const TArray<FGuLiRuntimeTuningResult> Results = Tuning->ResetValues(KeyOrAll);
	if (Results.IsEmpty())
	{
		return FActionResult::Failure(TEXT("gs.GM.Reset 失败"), TEXT("没有可返回的调参结果。"));
	}
	bool bSuccess = true;
	int32 Applied = 0;
	TArray<FString> Lines;
	for (const FGuLiRuntimeTuningResult& Result : Results)
	{
		bSuccess &= Result.bSuccess;
		Applied += Result.AppliedInstanceCount;
		Lines.Add(Private::RuntimeResultDetail(Result));
	}
	const FString Detail = FString::Join(Lines, TEXT("\n"));
	return bSuccess
		? FActionResult::Success(TEXT("gs.GM.Reset 成功"), Detail, false, Applied)
		: FActionResult::Failure(TEXT("gs.GM.Reset 部分或全部失败"), Detail);
}

TArray<GuLiGMPanel::FSkillProfileRow> GuLiGMPanel::FModel::QuerySkillProfiles(
	FString& OutUnavailableReason) const
{
	OutUnavailableReason.Reset();
	TArray<FSkillProfileRow> Rows;
	const FAccessPolicy Access = GetAccessPolicy();
	if (!Access.bCanReadSkillProfiles)
	{
		OutUnavailableReason = TEXT("技能 Profile 尚不可用。");
		return Rows;
	}
	const UWorld* World = GetWorld();
	const UGuLiArmySkillSubsystem* Skills = World ? World->GetSubsystem<UGuLiArmySkillSubsystem>() : nullptr;
	if (!Skills)
	{
		OutUnavailableReason = TEXT("ArmySkill Subsystem 尚未就绪。");
		return Rows;
	}
	for (const FGuLiResolvedSkillProfile& Profile : Skills->GetResolvedSkills())
	{
		FSkillProfileRow& Row = Rows.AddDefaulted_GetRef();
		Row.Team = Profile.Team;
		Row.UnitTypeId = Profile.UnitTypeId;
		Row.SlotId = Profile.SlotId;
		Row.SkillId = Profile.SkillId;
		Row.ExecutorId = Profile.ExecutorId;
		Row.Damage = Profile.Damage;
		Row.AttackRatePerSecond = Profile.AttackRatePerSecond;
		Row.RangeCentimeters = Profile.RangeCentimeters;
		Row.Revision = Profile.Revision;
		Row.bPendingChanges = Skills->HasPendingChanges();
	}
	Rows.Sort([](const FSkillProfileRow& A, const FSkillProfileRow& B)
	{
		if (A.Team != B.Team) return static_cast<uint8>(A.Team) < static_cast<uint8>(B.Team);
		if (A.UnitTypeId != B.UnitTypeId) return A.UnitTypeId < B.UnitTypeId;
		return A.SlotId.LexicalLess(B.SlotId);
	});
	return Rows;
}

TArray<GuLiGMPanel::FSkillSourceRow> GuLiGMPanel::FModel::QuerySkillSources(
	const EGuLiTeam Team,
	FString& OutUnavailableReason) const
{
	OutUnavailableReason.Reset();
	TArray<FSkillSourceRow> Rows;
	const FAccessPolicy Access = GetAccessPolicy();
	if (!Access.bCanReadSkillSources)
	{
		OutUnavailableReason = Access.AuthorityUnavailableReason;
		return Rows;
	}
	if (!Private::IsPlayableTeam(Team))
	{
		OutUnavailableReason = TEXT("Team 必须是 Red 或 Blue。");
		return Rows;
	}
	const UWorld* World = GetWorld();
	const UGuLiArmySkillSubsystem* Skills = World ? World->GetSubsystem<UGuLiArmySkillSubsystem>() : nullptr;
	if (!Skills)
	{
		OutUnavailableReason = TEXT("ArmySkill Subsystem 尚未就绪。");
		return Rows;
	}
	for (const FGuLiSkillSource& Source : Skills->GetSources(Team))
	{
		FSkillSourceRow& Row = Rows.AddDefaulted_GetRef();
		Row.Team = Team;
		Row.SourceId = Source.SourceInstanceId;
		Row.Label = Source.DebugLabel;
		Row.ModifierCount = Source.Modifiers.Num();
		Row.ReplacementCount = Source.Replacements.Num();
		Row.UnlockCount = Source.Unlocks.Num();
	}
	Rows.Sort([](const FSkillSourceRow& A, const FSkillSourceRow& B)
	{
		const int32 LabelOrder = A.Label.Compare(B.Label, ESearchCase::IgnoreCase);
		return LabelOrder != 0
			? LabelOrder < 0
			: A.SourceId.ToString(EGuidFormats::Digits).Compare(B.SourceId.ToString(EGuidFormats::Digits)) < 0;
	});
	return Rows;
}

GuLiGMPanel::FActionResult GuLiGMPanel::FModel::ExplainSkill(
	const EGuLiTeam Team,
	const FString& UnitTypeId,
	const FString& SlotId) const
{
	uint16 Unit = 0;
	FName Slot = NAME_None;
	if (!Private::IsPlayableTeam(Team)
		|| !Private::ParsePositiveUnit(UnitTypeId, Unit)
		|| !Private::ParseSlot(SlotId, Slot))
	{
		return FActionResult::Failure(TEXT("gs.GM.Skill.Get 被拒绝"), TEXT("Team 必须是 Red/Blue，UnitTypeId 必须是 1..65535，SlotId 不能为空。"));
	}
	const UWorld* World = GetWorld();
	const UGuLiArmySkillSubsystem* Skills = World ? World->GetSubsystem<UGuLiArmySkillSubsystem>() : nullptr;
	if (!Skills)
	{
		return FActionResult::Failure(TEXT("gs.GM.Skill.Get 失败"), TEXT("ArmySkill Subsystem 尚未就绪。"));
	}
	FString Detail;
	if (!GetAccessPolicy().bCanReadSkillSources)
	{
		const FGuLiResolvedSkillProfile* Profile = Skills->FindResolvedSkill(Team, Unit, Slot);
		if (!Profile)
		{
			return FActionResult::Failure(TEXT("gs.GM.Skill.Get 未找到"), TEXT("客户端尚未收到该 committed final Profile。"));
		}
		Detail = FString::Printf(
			TEXT("Replicated committed final only: Team=%s Unit=%u Slot=%s Skill=%s Executor=%s Damage=%.6g Rate=%.6g/s Range=%.6gcm Revision=%u\n全局 Registry、base/source/override 账本未复制，不可用。"),
			LexToString(Team), Unit, *Slot.ToString(), *Profile->SkillId.ToString(), *Profile->ExecutorId.ToString(),
			Profile->Damage, Profile->AttackRatePerSecond, Profile->RangeCentimeters, Profile->Revision);
	}
	else
	{
		Detail = Skills->ExplainResolvedSkill(Team, Unit, Slot);
	}
	return FActionResult::Success(TEXT("gs.GM.Skill.Get 成功"), MoveTemp(Detail));
}

GuLiGMPanel::FActionResult GuLiGMPanel::FModel::SetSkillNumericOverride(
	const FSkillNumericRequest& Request) const
{
	uint16 Unit = 0;
	float Value = 0.0f;
	FName Slot = NAME_None;
	if (!Private::IsPlayableTeam(Request.Team)
		|| !Private::ParsePositiveUnit(Request.UnitTypeId, Unit)
		|| !Private::ParseSlot(Request.SlotId, Slot)
		|| !Private::ParseFiniteFloat(Request.Value, Value)
		|| Value < 0.0f)
	{
		return FActionResult::Failure(
			TEXT("gs.GM.Skill.Set 被拒绝"),
			TEXT("Team/UnitTypeId/SlotId 必须有效，Value 必须是有限非负数。"));
	}
	FGuLiArmySkillCommand Command;
	Command.Command = EGuLiArmySkillCommand::SetNumericOverride;
	Command.NumericOverride.UnitTypeId = Unit;
	Command.NumericOverride.SlotId = Slot;
	switch (Request.Attribute)
	{
	case ESkillAttribute::Damage:
		Command.NumericOverride.bOverrideDamage = true;
		Command.NumericOverride.Damage = Value;
		break;
	case ESkillAttribute::AttackRate:
		Command.NumericOverride.bOverrideAttackRate = true;
		Command.NumericOverride.AttackRatePerSecond = Value;
		break;
	case ESkillAttribute::Range:
		Command.NumericOverride.bOverrideRange = true;
		Command.NumericOverride.RangeCentimeters = Value;
		break;
	}
	return Private::ExecuteArmyCommand(*this, Request.Team, Command, TEXT("gs.GM.Skill.Set"));
}

GuLiGMPanel::FActionResult GuLiGMPanel::FModel::ClearSkillNumericOverride(
	const EGuLiTeam Team,
	const FString& UnitTypeId,
	const FString& SlotId) const
{
	uint16 Unit = 0;
	FName Slot = NAME_None;
	if (!Private::IsPlayableTeam(Team)
		|| !Private::ParsePositiveUnit(UnitTypeId, Unit)
		|| !Private::ParseSlot(SlotId, Slot))
	{
		return FActionResult::Failure(TEXT("gs.GM.Skill.Reset 被拒绝"), TEXT("Team/UnitTypeId/SlotId 必须有效。"));
	}
	FGuLiArmySkillCommand Command;
	Command.Command = EGuLiArmySkillCommand::ClearNumericOverride;
	Command.NumericOverride.UnitTypeId = Unit;
	Command.NumericOverride.SlotId = Slot;
	return Private::ExecuteArmyCommand(*this, Team, Command, TEXT("gs.GM.Skill.Reset"));
}

GuLiGMPanel::FActionResult GuLiGMPanel::FModel::UpsertSkillSource(
	const FSkillSourceRequest& Request) const
{
	uint16 Unit = 0;
	float Value = 0.0f;
	FName Slot = NAME_None;
	const FString Label = Request.Label.TrimStartAndEnd();
	if (!Private::IsPlayableTeam(Request.Team)
		|| !Private::ParsePositiveUnit(Request.UnitTypeId, Unit)
		|| !Private::ParseSlot(Request.SlotId, Slot)
		|| !Private::ParseFiniteFloat(Request.Value, Value)
		|| Label.IsEmpty())
	{
		return FActionResult::Failure(
			TEXT("gs.GM.Skill.Source 被拒绝"),
			TEXT("Team、UnitTypeId、SlotId、Label 和有限 Value 都必须有效。"));
	}
	FGuLiArmySkillCommand Command;
	Command.Command = EGuLiArmySkillCommand::UpsertSource;
	Command.Source.SourceInstanceId = GuLiSkillGM::MakeSourceId(Request.Team, Label);
	Command.Source.DebugLabel = Label;
	FGuLiSkillModifier& Modifier = Command.Source.Modifiers.AddDefaulted_GetRef();
	Modifier.Target.UnitTypeIds.Add(Unit);
	Modifier.Target.SlotId = Slot;
	Modifier.Operation = Request.Operation == EModifierOperation::Percent
		? EGuLiSkillModifierOperation::AddPercent
		: EGuLiSkillModifierOperation::AddFlat;
	Modifier.Magnitude = Value;
	switch (Request.Attribute)
	{
	case ESkillAttribute::Damage: Modifier.Attribute = EGuLiSkillAttribute::Damage; break;
	case ESkillAttribute::AttackRate: Modifier.Attribute = EGuLiSkillAttribute::AttackRate; break;
	case ESkillAttribute::Range: Modifier.Attribute = EGuLiSkillAttribute::Range; break;
	}
	const FString RequiredSkill = Request.RequiredSkillId.TrimStartAndEnd();
	if (!RequiredSkill.IsEmpty() && RequiredSkill != TEXT("*"))
	{
		Modifier.Target.RequiredSkillId = FName(*RequiredSkill);
	}
	const FString RequiredTag = Request.RequiredGameplayTag.TrimStartAndEnd();
	if (!RequiredTag.IsEmpty() && RequiredTag != TEXT("*"))
	{
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*RequiredTag), false);
		if (!Tag.IsValid())
		{
			return FActionResult::Failure(
				TEXT("gs.GM.Skill.Source 被拒绝"),
				FString::Printf(TEXT("未知 GameplayTag: %s"), *RequiredTag));
		}
		Modifier.Target.RequiredTags.AddTag(Tag);
	}
	return Private::ExecuteArmyCommand(*this, Request.Team, Command, TEXT("gs.GM.Skill.Source"));
}

GuLiGMPanel::FActionResult GuLiGMPanel::FModel::UpsertSkillReplacement(
	const FSkillReplacementRequest& Request) const
{
	uint16 Unit = 0;
	int32 Priority = 0;
	FName Slot = NAME_None;
	const FString Label = Request.Label.TrimStartAndEnd();
	const FString SkillId = Request.SkillId.TrimStartAndEnd();
	if (!Private::IsPlayableTeam(Request.Team)
		|| !Private::ParsePositiveUnit(Request.UnitTypeId, Unit)
		|| !Private::ParseSlot(Request.SlotId, Slot)
		|| !LexTryParseString(Priority, *Request.Priority.TrimStartAndEnd())
		|| Label.IsEmpty()
		|| SkillId.IsEmpty())
	{
		return FActionResult::Failure(
			TEXT("gs.GM.Skill.Replace 被拒绝"),
			TEXT("Team、UnitTypeId、SlotId、Label、SkillId 和整数 Priority 都必须有效。"));
	}
	FGuLiArmySkillCommand Command;
	Command.Command = EGuLiArmySkillCommand::UpsertSource;
	Command.Source.SourceInstanceId = GuLiSkillGM::MakeSourceId(Request.Team, Label);
	Command.Source.DebugLabel = Label;
	FGuLiSkillSlotReplacement& Replacement = Command.Source.Replacements.AddDefaulted_GetRef();
	Replacement.Target.UnitTypeIds.Add(Unit);
	Replacement.Target.SlotId = Slot;
	Replacement.SkillId = FName(*SkillId);
	Replacement.Priority = Priority;
	return Private::ExecuteArmyCommand(*this, Request.Team, Command, TEXT("gs.GM.Skill.Replace"));
}

GuLiGMPanel::FActionResult GuLiGMPanel::FModel::RemoveSkillSource(
	const EGuLiTeam Team,
	const FString& LabelText) const
{
	const FString Label = LabelText.TrimStartAndEnd();
	if (!Private::IsPlayableTeam(Team) || Label.IsEmpty())
	{
		return FActionResult::Failure(TEXT("gs.GM.Skill.Remove 被拒绝"), TEXT("Team 必须是 Red/Blue，Label 不能为空。"));
	}
	FGuLiArmySkillCommand Command;
	Command.Command = EGuLiArmySkillCommand::RemoveSource;
	Command.SourceInstanceId = GuLiSkillGM::MakeSourceId(Team, Label);
	return Private::ExecuteArmyCommand(*this, Team, Command, TEXT("gs.GM.Skill.Remove"));
}

GuLiGMPanel::FActionResult GuLiGMPanel::FModel::InspectSkillSoldier(const FString& SoldierId) const
{
	const FAccessPolicy Access = GetAccessPolicy();
	if (!Access.bCanReadAuthorityDiagnostics)
	{
		return FActionResult::Failure(TEXT("gs.GM.Skill.Soldier 不可用"), Access.AuthorityUnavailableReason);
	}
	uint32 Id = 0u;
	if (!Private::ParsePositiveId(SoldierId, Id))
	{
		return FActionResult::Failure(TEXT("gs.GM.Skill.Soldier 被拒绝"), TEXT("SoldierId 必须是正整数。"));
	}
	const UWorld* World = GetWorld();
	const UGuLiBattleAuthoritySubsystem* Authority = World ? World->GetSubsystem<UGuLiBattleAuthoritySubsystem>() : nullptr;
	FGuLiSoldierCombatDebug Combat;
	FGuLiSoldierNavigationDebug Navigation;
	if (!Authority || !Authority->TryGetSoldierCombatDebug(FGuLiSoldierId(Id), Combat))
	{
		return FActionResult::Failure(TEXT("gs.GM.Skill.Soldier 未找到"), FString::Printf(TEXT("Soldier %u 在当前权威 World 不可用。"), Id));
	}
	const bool bHasNavigation = Authority->TryGetSoldierNavigationDebug(FGuLiSoldierId(Id), Navigation);
	return FActionResult::Success(
		TEXT("gs.GM.Skill.Soldier 成功"),
		FString::Printf(
			TEXT("soldier=%u team=%s type=%u skill=%s executor=%s target=%u hp=%.6g/%.6g damage=%.6g rate=%.6g range_cm=%.6g cooldown=%.3fs stop=%s moving=%d nav=%s shots=%llu revision=%u position=%s"),
			Id,
			LexToString(Combat.Team),
			Combat.UnitTypeId,
			*Combat.SkillId.ToString(),
			*Combat.ExecutorId.ToString(),
			Combat.TargetId.Value,
			Combat.Health,
			Combat.MaxHealth,
			Combat.Damage,
			Combat.AttackRate,
			Combat.RangeCentimeters,
			Combat.CooldownRemaining,
			GuLiSoldierCombat::LexToString(Combat.StopReason),
			bHasNavigation && Navigation.bMoving ? 1 : 0,
			bHasNavigation ? Private::NavigationStateToString(Navigation.State) : TEXT("unavailable"),
			Combat.ShotsFired,
			Combat.ProfileRevision,
			*Combat.Location.ToCompactString()));
}

GuLiGMPanel::FActionResult GuLiGMPanel::FModel::SpawnDebugSoldier(const FSpawnRequest& Request) const
{
	const FAccessPolicy Access = GetAccessPolicy();
	if (!Access.bCanMutateAuthorityState)
	{
		return FActionResult::Failure(TEXT("gs.GM.Skill.Spawn 被拒绝"), Access.AuthorityUnavailableReason);
	}
	uint16 Unit = 0;
	float X = 0.0f;
	float Y = 0.0f;
	float Z = 0.0f;
	if (!Private::IsPlayableTeam(Request.Team)
		|| !Private::ParsePositiveUnit(Request.UnitTypeId, Unit)
		|| !Private::ParseFiniteFloat(Request.X, X)
		|| !Private::ParseFiniteFloat(Request.Y, Y)
		|| !Private::ParseFiniteFloat(Request.Z, Z))
	{
		return FActionResult::Failure(
			TEXT("gs.GM.Skill.Spawn 被拒绝"),
			TEXT("Team/UnitTypeId 必须有效，X/Y/Z 必须是有限数值（cm）。"));
	}
	UWorld* World = GetWorld();
	UGuLiBattleAuthoritySubsystem* Authority = World ? World->GetSubsystem<UGuLiBattleAuthoritySubsystem>() : nullptr;
	FGuLiSoldierId SpawnedId;
	if (!Authority || !Authority->SpawnDebugSoldier(Request.Team, Unit, FVector(X, Y, Z), SpawnedId))
	{
		return FActionResult::Failure(TEXT("gs.GM.Skill.Spawn 失败"), TEXT("请检查兵种、容量与当前地图 NavMesh 位置。"));
	}
	return FActionResult::Success(
		TEXT("gs.GM.Skill.Spawn 成功"),
		FString::Printf(TEXT("team=%s unit=%u soldier=%u position=%s"), LexToString(Request.Team), Unit, SpawnedId.Value, *FVector(X, Y, Z).ToCompactString()),
		false,
		1);
}

GuLiGMPanel::FActionResult GuLiGMPanel::FModel::RunBenchmark(
	const int32 PopulationCount,
	const FString& StepsText) const
{
	if (!GetAccessPolicy().bCanRunLocalBenchmark)
	{
		return FActionResult::Failure(TEXT("gs.GM.Skill.Bench 不可用"), TEXT("当前没有可执行本地 Bench 的游戏客户端。"));
	}
	int32 Steps = 0;
	if ((PopulationCount != 500 && PopulationCount != 10000)
		|| !LexTryParseString(Steps, *StepsText.TrimStartAndEnd())
		|| Steps < 1
		|| Steps > 1800)
	{
		return FActionResult::Failure(
			TEXT("gs.GM.Skill.Bench 被拒绝"),
			TEXT("Population 只能是 500/10000，Steps 必须是 1..1800。"));
	}
	FGuLiCombatBenchmarkResult Result;
	if (!GuLiSoldierCombat::RunBenchmark(PopulationCount, Steps, Result))
	{
		return FActionResult::Failure(TEXT("gs.GM.Skill.Bench 失败"));
	}
	return FActionResult::Success(
		TEXT("gs.GM.Skill.Bench 完成"),
		FString::Printf(
			TEXT("population=%d steps=%d mean_ms=%.6f p95_ms=%.6f max_ms=%.6f shots=%lld queries=%lld candidates=%lld excludes=rendering,navigation,network,world"),
			Result.PopulationCount,
			Result.Steps,
			Result.MeanMilliseconds,
			Result.P95Milliseconds,
			Result.MaximumMilliseconds,
			Result.Shots,
			Result.TargetQueries,
			Result.CandidateChecks));
}

GuLiGMPanel::FActionResult GuLiGMPanel::FModel::InspectNavigationSoldier(const FString& SoldierId) const
{
	const FAccessPolicy Access = GetAccessPolicy();
	if (!Access.bCanReadAuthorityDiagnostics)
	{
		return FActionResult::Failure(TEXT("gs.GM.Commander.Nav.Soldier 不可用"), Access.AuthorityUnavailableReason);
	}
	uint32 Id = 0u;
	if (!Private::ParsePositiveId(SoldierId, Id))
	{
		return FActionResult::Failure(TEXT("gs.GM.Commander.Nav.Soldier 被拒绝"), TEXT("SoldierId 必须是正整数。"));
	}
	const UWorld* World = GetWorld();
	const UGuLiBattleAuthoritySubsystem* Authority = World ? World->GetSubsystem<UGuLiBattleAuthoritySubsystem>() : nullptr;
	FGuLiSoldierNavigationDebug Navigation;
	FGuLiSoldierCombatDebug Combat;
	if (!Authority
		|| !Authority->TryGetSoldierNavigationDebug(FGuLiSoldierId(Id), Navigation)
		|| !Authority->TryGetSoldierCombatDebug(FGuLiSoldierId(Id), Combat))
	{
		return FActionResult::Failure(TEXT("gs.GM.Commander.Nav.Soldier 未找到"), FString::Printf(TEXT("Soldier %u 不可用。"), Id));
	}
	return FActionResult::Success(
		TEXT("gs.GM.Commander.Nav.Soldier 成功"),
		FString::Printf(
			TEXT("soldier=%u team=%s state=%s moving=%d order=%u completed=%u failed_order=%u failure=%s failure_time=%.3fs location=%s last_nav=%s final=%s distance=%.1fcm waypoint=%s path_index=%d no_progress=%.3fs surface_failures=%d/%d personal_queries=%d combat_target=%u cooldown=%.3fs combat_stop=%s"),
			Id,
			LexToString(Navigation.Team),
			Private::NavigationStateToString(Navigation.State),
			Navigation.bMoving ? 1 : 0,
			Navigation.ActiveOrderId,
			Navigation.LastCompletedOrderId,
			Navigation.LastFailedOrderId,
			Private::NavigationFailureToString(Navigation.Failure),
			Navigation.FailureSimulationSeconds,
			*Navigation.Location.ToCompactString(),
			*Navigation.LastValidNavLocation.ToCompactString(),
			Navigation.bHasFinalSlot ? *Navigation.FinalSlot.ToCompactString() : TEXT("none"),
			Navigation.DistanceToFinalSlotCentimeters,
			*Navigation.CurrentWaypoint.ToCompactString(),
			Navigation.PathPointIndex,
			Navigation.NoProgressSeconds,
			Navigation.ConsecutiveSurfaceFailures,
			Navigation.TotalSurfaceFailures,
			Navigation.PersonalPathRetries,
			Combat.TargetId.Value,
			Combat.CooldownRemaining,
			GuLiSoldierCombat::LexToString(Combat.StopReason)));
}

GuLiGMPanel::FActionResult GuLiGMPanel::FModel::QueryNavigationStats() const
{
	const FAccessPolicy Access = GetAccessPolicy();
	if (!Access.bCanReadAuthorityDiagnostics)
	{
		return FActionResult::Failure(TEXT("gs.GM.Commander.Nav.Stats 不可用"), Access.AuthorityUnavailableReason);
	}
	const UWorld* World = GetWorld();
	const UGuLiBattleAuthoritySubsystem* Authority = World ? World->GetSubsystem<UGuLiBattleAuthoritySubsystem>() : nullptr;
	if (!Authority)
	{
		return FActionResult::Failure(TEXT("gs.GM.Commander.Nav.Stats 失败"), TEXT("Commander Authority 尚未就绪。"));
	}
	const FGuLiNavigationStats Stats = Authority->GetNavigationStats();
	return FActionResult::Success(
		TEXT("gs.GM.Commander.Nav.Stats 成功"),
		FString::Printf(
			TEXT("alive=%d active=%d idle=%d arrived=%d centerline=%d personal=%d blocked=%d\nsurface=%llu failed=%llu paths=%llu personal_paths=%llu pending_plans=%d projections=%llu planning_paths=%llu partial_moves=%llu\navoidance refresh=%llu candidates=%llu overlaps=%llu max_bucket=%d predictive=%llu forced=%llu candidates=%llu colliders=%llu max_bucket=%d\nplan_ms=%.3f max_plan_ms=%.3f slowest=%u no_progress=%.3fs"),
			Stats.Alive,
			Stats.Active,
			Stats.Idle,
			Stats.Arrived,
			Stats.CenterlineRecovery,
			Stats.PersonalPathRecovery,
			Stats.Blocked,
			Stats.SurfaceMoveCalls,
			Stats.SurfaceMoveFailures,
			Stats.PathQueries,
			Stats.PersonalPathQueries,
			Stats.PendingMovePlanningTasks,
			Stats.MoveCandidateProjectionQueries,
			Stats.MovePlanningPathQueries,
			Stats.PartiallyAcceptedMoveCommands,
			Stats.ManualAvoidanceRefreshes,
			Stats.ManualAvoidanceCandidatePairs,
			Stats.ManualAvoidanceOverlapPairs,
			Stats.MaximumManualAvoidanceBucketOccupancy,
			Stats.PredictiveAvoidanceSolves,
			Stats.ForcedPredictiveAvoidanceSolves,
			Stats.PredictiveAvoidanceCandidates,
			Stats.PredictiveAvoidanceColliderEvaluations,
			Stats.MaximumPredictiveAvoidanceBucketOccupancy,
			Stats.LastDestinationPlanningMilliseconds,
			Stats.MaximumDestinationPlanningMilliseconds,
			Stats.SlowestSoldierId.Value,
			Stats.SlowestNoProgressSeconds));
}

GuLiGMPanel::FActionResult GuLiGMPanel::FModel::QueryStrongholds() const
{
	UWorld* World = GetWorld();
	return World ? FActionResult::Success(TEXT("据点与工程车快照"),GuLiStrongholds::DescribeWorld(*World))
		: FActionResult::Failure(TEXT("游戏世界未就绪"));
}

GuLiGMPanel::FActionResult GuLiGMPanel::FModel::QueryLastMove(const FString& OptionalCohortId) const
{
	const FAccessPolicy Access = GetAccessPolicy();
	if (!Access.bCanReadAuthorityDiagnostics)
	{
		return FActionResult::Failure(TEXT("gs.GM.Commander.Nav.LastMove 不可用"), Access.AuthorityUnavailableReason);
	}
	uint32 CohortFilter = 0u;
	const FString Trimmed = OptionalCohortId.TrimStartAndEnd();
	if (!Trimmed.IsEmpty() && !Private::ParsePositiveId(Trimmed, CohortFilter))
	{
		return FActionResult::Failure(TEXT("gs.GM.Commander.Nav.LastMove 被拒绝"), TEXT("CohortId 必须留空或填写正整数。"));
	}
	const UWorld* World = GetWorld();
	const UGuLiBattleAuthoritySubsystem* Authority = World ? World->GetSubsystem<UGuLiBattleAuthoritySubsystem>() : nullptr;
	FGuLiMovePlanningDebug Debug;
	if (!Authority || !Authority->TryGetLastMovePlanningDebug(Debug))
	{
		return FActionResult::Failure(TEXT("gs.GM.Commander.Nav.LastMove 不可用"), TEXT("当前没有已完成的移动规划诊断。"));
	}
	if (CohortFilter != 0u)
	{
		const FGuLiMoveCohortPlanningDebug* Cohort = Debug.Cohorts.FindByPredicate(
			[CohortFilter](const FGuLiMoveCohortPlanningDebug& Candidate)
			{
				return Candidate.CohortId.Value == CohortFilter;
			});
		if (!Cohort)
		{
			return FActionResult::Failure(
				TEXT("gs.GM.Commander.Nav.LastMove 未找到"),
				FString::Printf(TEXT("最近一次规划不包含 Cohort %u。"), CohortFilter));
		}
		return FActionResult::Success(
			TEXT("gs.GM.Commander.Nav.LastMove 成功"),
			FString::Printf(
				TEXT("command=%u batch=%u cohort=%u members=%u eligible=0x%08x accepted=0x%08x target=%s plan_ms=%.3f"),
				Debug.ClientCommandId,
				Debug.BatchOrderId,
				CohortFilter,
				Cohort->MemberCount,
				Cohort->EligibleMemberMask,
				Cohort->AcceptedMemberMask,
				*Debug.RequestedTarget.ToCompactString(),
				Debug.PlanningMilliseconds));
	}
	return FActionResult::Success(
		TEXT("gs.GM.Commander.Nav.LastMove 成功"),
		FString::Printf(
			TEXT("command=%u batch=%u target=%s theoretical=%d projected=%d legal=%d desired=%d prefix=%d->%d expansions=%d full_pool=%d frames=%d candidate_queries=%d path_queries=%d route_splits=%d conflicts=%d accepted=%d failed=%d max_radius=%.1fcm plan_ms=%.3f cohorts=%d"),
			Debug.ClientCommandId,
			Debug.BatchOrderId,
			*Debug.RequestedTarget.ToCompactString(),
			Debug.TheoreticalCandidates,
			Debug.ProjectedCandidates,
			Debug.LegalCandidates,
			Debug.DesiredLegalSlots,
			Debug.InitialProjectionLimit,
			Debug.FinalProjectionLimit,
			Debug.ProjectionExpansionCount,
			Debug.bEscalatedToFullCandidatePool ? 1 : 0,
			Debug.PlanningWorldFrames,
			Debug.CandidateProjectionQueries,
			Debug.PathQueries,
			Debug.RouteSplitCount,
			Debug.ReservationConflictCount,
			Debug.AcceptedMembers,
			Debug.FailedMembers,
			Debug.MaximumSearchRadiusCentimeters,
			Debug.PlanningMilliseconds,
			Debug.Cohorts.Num()));
}

GuLiGMPanel::FActionResult GuLiGMPanel::FModel::SetCameraDebugEnabled(const bool bEnabled) const
{
#if UE_BUILD_SHIPPING
	return FActionResult::Failure(TEXT("gs.GM.Commander.Camera.Debug 不可用"), TEXT("Shipping 构建不提供 GM 面板。"));
#else
	const FAccessPolicy Access = GetAccessPolicy();
	if (!Access.bCanToggleLocalCameraDebug)
	{
		return FActionResult::Failure(TEXT("gs.GM.Commander.Camera.Debug 不可用"), Access.AuthorityUnavailableReason);
	}
	UWorld* World = GetWorld();
	int32 Updated = 0;
	if (World)
	{
		for (TActorIterator<AGuLiCommanderCameraPawn> It(World); It; ++It)
		{
			if (It->IsLocallyControlled())
			{
				It->SetCameraDebugEnabled(bEnabled);
				++Updated;
			}
		}
	}
	if (Updated == 0)
	{
		return FActionResult::Failure(TEXT("gs.GM.Commander.Camera.Debug 未应用"), TEXT("当前 World 没有本地控制的 Commander Camera Pawn。"));
	}
	return FActionResult::Success(
		TEXT("gs.GM.Commander.Camera.Debug 已更新"),
		FString::Printf(TEXT("enabled=%d local_cameras=%d"), bEnabled ? 1 : 0, Updated),
		false,
		Updated);
#endif
}

bool GuLiGMPanel::FModel::IsCameraDebugEnabled() const
{
#if UE_BUILD_SHIPPING
	return false;
#else
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	for (TActorIterator<AGuLiCommanderCameraPawn> It(World); It; ++It)
	{
		if (It->IsLocallyControlled() && It->IsCameraDebugEnabled())
		{
			return true;
		}
	}
	return false;
#endif
}
