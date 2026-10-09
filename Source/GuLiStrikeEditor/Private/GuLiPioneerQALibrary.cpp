#include "GuLiPioneerQALibrary.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
#include "EngineUtils.h"
#include "Gameplay/CommanderSkills/GuLiCommanderSkillComponent.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectRuntimeSubsystem.h"
#include "Gameplay/CombatEffects/GuLiProjectilePoolSubsystem.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Gameplay/Skills/GuLiArmySkillSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/PlayerInput.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	struct FObservation
	{
		FDelegateHandle ShotsHandle, ProjectileHandle;
		TArray<TSharedPtr<FJsonValue>> Shots, Projectiles;
	};
	TMap<TWeakObjectPtr<UWorld>, FObservation> Observations;
	AGuLiCommanderPlayerController* Allowed(APlayerController* Controller)
	{
		auto* PC = Cast<AGuLiCommanderPlayerController>(Controller);
		return PC && PC->IsLocalController() && PC->HasAuthority() && PC->GetWorld()->IsPlayInEditor()
			&& PC->GetWorld()->GetMapName().Contains(TEXT("LVL_CommanderMassPrototype")) ? PC : nullptr;
	}
	void Vector(TSharedRef<FJsonObject> Object, const TCHAR* Name, const FVector& V)
	{
		Object->SetArrayField(Name, { MakeShared<FJsonValueNumber>(V.X), MakeShared<FJsonValueNumber>(V.Y), MakeShared<FJsonValueNumber>(V.Z) });
	}
}

bool UGuLiPioneerQALibrary::Observe(APlayerController* Controller, bool bEnabled)
{
	auto* PC = Allowed(Controller); if (!PC) return false;
	UWorld* World = PC->GetWorld();
	auto* Runtime = World->GetSubsystem<UGuLiCombatEffectRuntimeSubsystem>();
	auto* Pool = World->GetSubsystem<UGuLiProjectilePoolSubsystem>();
	if (!Runtime || !Pool) return false;
	if (auto* Old = Observations.Find(World))
	{ Runtime->OnShots.Remove(Old->ShotsHandle); Pool->OnState.Remove(Old->ProjectileHandle); Observations.Remove(World); }
	for (auto It = Observations.CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent();
	if (!bEnabled) return true;
	auto& Observation = Observations.Add(World);
	const TWeakObjectPtr<UWorld> WeakWorld(World);
	Observation.ShotsHandle = Runtime->OnShots.AddLambda([WeakWorld](const TArray<FGuLiCombatShotCue>& Cues)
	{
		auto* O = Observations.Find(WeakWorld); if (!O) return;
		for (const auto& Cue : Cues)
		{
			if (O->Shots.Num() >= 4096 || Cue.Source.Kind != EGuLiTargetKind::CommanderSoldier) continue;
			auto Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("shot_id"), Cue.ShotId.ToString());
			Row->SetNumberField(TEXT("source"), Cue.Source.LocalId); Row->SetNumberField(TEXT("target"), Cue.Target.LocalId);
			Row->SetNumberField(TEXT("type"), Cue.UnitTypeId); Row->SetNumberField(TEXT("muzzle"), Cue.MuzzleIndex);
			Row->SetNumberField(TEXT("time"), Cue.ServerTime); Row->SetNumberField(TEXT("pose_time"), Cue.MechanicalPoseTimeSeconds);
			Vector(Row, TEXT("start"), Cue.Start); Vector(Row, TEXT("end"), Cue.End); Vector(Row, TEXT("direction"), Cue.MuzzleDirection);
			O->Shots.Add(MakeShared<FJsonValueObject>(Row));
		}
	});
	Observation.ProjectileHandle = Pool->OnState.AddLambda([WeakWorld](const FGuLiCombatEffectState& State, bool)
	{
		auto* O = Observations.Find(WeakWorld);
		if (!O || O->Projectiles.Num() >= 8192 || State.Source.Kind != EGuLiTargetKind::CommanderSoldier) return;
		auto Row = MakeShared<FJsonObject>(); Row->SetStringField(TEXT("effect_id"), State.EffectId.ToString());
		Row->SetNumberField(TEXT("source"), State.Source.LocalId); Row->SetNumberField(TEXT("target"), State.Target.LocalId);
		Row->SetNumberField(TEXT("phase"), int32(State.Phase)); Row->SetNumberField(TEXT("reason"), int32(State.EndReason));
		Vector(Row, TEXT("launch"), State.LaunchLocation); Vector(Row, TEXT("direction"), State.LaunchDirection);
		Vector(Row, TEXT("velocity"), State.Velocity); Vector(Row, TEXT("aim_point"), State.LastTargetLocation);
		O->Projectiles.Add(MakeShared<FJsonValueObject>(Row));
	});
	return true;
}

FString UGuLiPioneerQALibrary::Snapshot(APlayerController* Controller)
{
	auto* PC = Cast<AGuLiCommanderPlayerController>(Controller);
	if (!PC || !PC->IsLocalController() || !PC->GetWorld()->IsPlayInEditor()
		|| !PC->GetWorld()->GetMapName().Contains(TEXT("LVL_CommanderMassPrototype")))
		return TEXT("{\"success\":false,\"error\":\"Local commander PIE required\"}");
	auto* PS = PC->GetPlayerState<AGuLiBattlePlayerState>();
	if (!PC->HasAuthority())
	{
		auto* Net = PC->GetCommanderNetSyncComponent();
		if (!PS || !Net) return TEXT("{\"success\":false}");
		auto Client = MakeShared<FJsonObject>(); Client->SetBoolField(TEXT("success"), true);
		Client->SetBoolField(TEXT("client_selection_readback"), true);
		Client->SetBoolField(TEXT("orders_ready"), PC->CanIssueCommanderOrders());
		Client->SetStringField(TEXT("world"), PC->GetWorld()->GetPathName());
		Client->SetNumberField(TEXT("selection_revision"), Net->GetSelectionState().SelectionRevision);
		TArray<TSharedPtr<FJsonValue>> Members;
		for (const auto& Cohort : Net->GetSelectionState().Cohorts)
			for (const auto Id : Cohort.MemberIds) Members.Add(MakeShared<FJsonValueNumber>(Id.Value));
		Client->SetArrayField(TEXT("selected"), Members);
		FString Json; FJsonSerializer::Serialize(Client, TJsonWriterFactory<>::Create(&Json)); return Json;
	}
	auto* Authority = PC->GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
	const auto* Data = PC->GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>();
	const auto* Army = PC->GetWorld()->GetSubsystem<UGuLiArmySkillSubsystem>();
	if (!PS || !Authority || !Data || !Army) return TEXT("{\"success\":false}");
	auto Root = MakeShared<FJsonObject>(); Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("world"), PC->GetWorld()->GetPathName()); Root->SetNumberField(TEXT("world_time"), PC->GetWorld()->GetTimeSeconds());
	Root->SetNumberField(TEXT("sim_tick"), Authority->GetServerSimTick()); Root->SetBoolField(TEXT("orders_ready"), PC->CanIssueCommanderOrders());
	const auto Population = Authority->GetTeamPopulation(PS->GetTeam());
	Root->SetNumberField(TEXT("population"), Population.X); Root->SetNumberField(TEXT("reserved"), Population.Y);
	Root->SetNumberField(TEXT("population_cap"), Authority->GetTeamUnitCap());
	Root->SetNumberField(TEXT("member_count"), Authority->GetAuthoritativeMemberCount());
	const auto& Selection = PC->GetCommanderNetSyncComponent()->GetSelectionState();
	Root->SetNumberField(TEXT("selection_revision"), Selection.SelectionRevision);
	TArray<TSharedPtr<FJsonValue>> Selected;
	for (const auto& Cohort : Selection.Cohorts) for (const auto Id : Cohort.MemberIds) Selected.Add(MakeShared<FJsonValueNumber>(Id.Value));
	Root->SetArrayField(TEXT("selected"), Selected);
	TArray<FGuLiSoldierStateItem> States; Authority->BuildSoldierStateSnapshot(States);
	TArray<TSharedPtr<FJsonValue>> Units;
	for (const auto& S : States)
	{
		auto Row = MakeShared<FJsonObject>(); Row->SetNumberField(TEXT("id"), S.SoldierId.Value);
		Row->SetNumberField(TEXT("type"), S.UnitTypeId); Row->SetNumberField(TEXT("team"), int32(S.Team));
		Row->SetBoolField(TEXT("alive"), S.IsAlive()); Row->SetNumberField(TEXT("health"), S.Health); Row->SetNumberField(TEXT("max_health"), S.MaxHealth);
		FTransform Transform;
		if (Authority->TryGetSoldierTransform(S.SoldierId, Transform))
		{ Vector(Row, TEXT("position"), Transform.GetLocation()); Row->SetNumberField(TEXT("yaw"), Transform.Rotator().Yaw); }
		FGuLiSoldierNavigationDebug Nav;
		if (Authority->TryGetSoldierNavigationDebug(S.SoldierId, Nav))
		{ Vector(Row, TEXT("velocity"), Nav.Velocity); Row->SetBoolField(TEXT("moving"), Nav.bMoving); Row->SetNumberField(TEXT("nav_state"), int32(Nav.State)); }
		FGuLiSoldierCombatDebug Combat;
		if (Authority->TryGetSoldierCombatDebug(S.SoldierId, Combat))
		{
			Row->SetNumberField(TEXT("rounds"), double(Combat.ShotsFired)); Row->SetNumberField(TEXT("target"), Combat.TargetId.Value);
			Row->SetStringField(TEXT("combat_state"), GuLiSoldierCombat::LexToString(Combat.StopReason));
			Row->SetNumberField(TEXT("range"), Combat.RangeCentimeters); Row->SetNumberField(TEXT("damage"), Combat.Damage);
			Row->SetNumberField(TEXT("rate"), Combat.AttackRate);
		}
		if (const auto* Definition = Data->FindSoldierDefinition(S.UnitTypeId))
		{
			Row->SetStringField(TEXT("name"), Definition->DisplayName.ToString()); Row->SetBoolField(TEXT("summon_only"), Definition->bSummonOnly);
			Row->SetNumberField(TEXT("move_speed"), Definition->MovementSpeedCmPerSecond);
			Row->SetNumberField(TEXT("effective_move_speed"), Authority->GetUnitMovementSpeed(S.Team, S.UnitTypeId));
			Row->SetNumberField(TEXT("radius"), Definition->GetMassAvoidanceRadius(150.f));
			const FBox Bounds = Definition->GetModelBoundsCentimeters(); Vector(Row, TEXT("bounds_min"), Bounds.Min); Vector(Row, TEXT("bounds_max"), Bounds.Max);
		}
		if (const auto* Profile = Army->FindResolvedSkill(S.Team, S.UnitTypeId, TEXT("BasicAttack")))
		{ Row->SetNumberField(TEXT("projectile_count"), Profile->ProjectileCount); Row->SetNumberField(TEXT("spread"), Profile->ProjectileSpreadAngleDegrees); }
		FGuLiActiveSkillRuntime Skill;
		if (Authority->QueryUnitSkillRuntime(S.SoldierId, *PS->GetCommanderSkills()->GetSkillCatalog(), Skill))
		{ Row->SetStringField(TEXT("skill"), Skill.SkillId.ToString()); Row->SetNumberField(TEXT("ready_at"), Skill.ReadyAt); }
		Units.Add(MakeShared<FJsonValueObject>(Row));
	}
	Root->SetArrayField(TEXT("units"), Units);
	if (const auto* O = Observations.Find(PC->GetWorld()))
	{ Root->SetArrayField(TEXT("shots"), O->Shots); Root->SetArrayField(TEXT("projectiles"), O->Projectiles); }
	FString Json; FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Json)); return Json;
}

bool UGuLiPioneerQALibrary::Select(APlayerController* Controller, int64 SoldierId, bool bAdd)
{
	// Selection must exercise the real client RPC as well as authority PIE.
	auto* PC = Cast<AGuLiCommanderPlayerController>(Controller);
	if (!PC || !PC->IsLocalController() || !PC->GetWorld()->IsPlayInEditor()
		|| !PC->GetWorld()->GetMapName().Contains(TEXT("LVL_CommanderMassPrototype"))
		|| !PC->CanIssueCommanderOrders() || SoldierId < 0 || SoldierId > MAX_uint32) return false;
	auto* A = PC->GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
	FGuLiSoldierId Id{uint32(SoldierId)}; FTransform Transform;
	if (SoldierId != 0 && (!A || !A->TryGetSoldierTransform(Id, Transform)))
	{
		bool bFound = false;
		for (TActorIterator<AGuLiCommanderPresentationActor> It(PC->GetWorld()); It; ++It)
			if (!It->IsEditorOnly() && It->TryGetPresentedSoldierTransform(Id, Transform)) { bFound = true; break; }
		if (!bFound) return false;
	}
	auto* Net = PC->GetCommanderNetSyncComponent(); FGuLiSelectionRequest Request;
	Request.Kind = EGuLiSelectionKind::Point; Request.SeedSoldierId = Id;
	Request.Center = Transform.GetLocation(); Request.RayOrigin = Transform.GetLocation() + FVector(0,0,10000);
	Request.RayDirection = FVector(0,0,-1); Request.PickHalfAngleRadians = .004f;
	Request.Modifier = SoldierId == 0 ? EGuLiSelectionModifier::Clear : (bAdd ? EGuLiSelectionModifier::Add : EGuLiSelectionModifier::Replace);
	Request.ClientRequestId = PC->AllocateEditorQASelectionRequestId(); Request.KnownSelectionRevision = Net->GetSelectionState().SelectionRevision;
	TGuardValue<bool> Guard(GAllowActorScriptExecutionInEditor, false); Net->SubmitOrderedSelection(Request); return true;
}

bool UGuLiPioneerQALibrary::Action(APlayerController* Controller, FName Command, FVector Point)
{
	auto* PC = Allowed(Controller); if (!PC || !PC->CanIssueCommanderOrders() || Point.ContainsNaN()) return false;
	TGuardValue<bool> Guard(GAllowActorScriptExecutionInEditor, false);
	if (Command == TEXT("QDown") || Command == TEXT("QUp"))
	{
		PC->InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), EKeys::Q,
			Command == TEXT("QDown") ? IE_Pressed : IE_Released, FPlatformTime::Cycles64())); return true;
	}
	if (Command == TEXT("Move")) return PC->IssueMapMove(Point, false);
	if (Command == TEXT("Stop")) { PC->StopSelectedUnits(); return true; }
	if (Command == TEXT("Skill"))
	{ PC->InvokeHUDCommand(Command); return true; }
	return false;
}

bool UGuLiPioneerQALibrary::ReplayLastQ(APlayerController* Controller)
{
	auto* PC = Allowed(Controller); if (!PC || !PC->CanIssueCommanderOrders()) return false;
	const auto* O = Observations.Find(PC->GetWorld()); if (!O) return false;
	auto* Skills = PC->GetPlayerState<AGuLiBattlePlayerState>()->GetCommanderSkills();
	const auto Reply = Skills->GetLastReply(); if (!Reply.RequestId.IsValid() || Reply.Units.IsEmpty()) return false;
	FGuLiActiveSkillRequest Request;
	if (!Skills->CopyServerRequestReceipt(Reply.RequestId, Request) || !Request.GlobalSkillId.IsNone()) return false;
	TGuardValue<bool> Guard(GAllowActorScriptExecutionInEditor, false); Skills->ServerRequestSkill(Request); return true;
}
