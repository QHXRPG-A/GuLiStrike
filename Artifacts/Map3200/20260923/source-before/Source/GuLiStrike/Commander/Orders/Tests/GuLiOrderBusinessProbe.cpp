// Explicit opt-in fixture on the authored map. It changes only its disposable test World.
#include "CoreMinimal.h"
#if !UE_BUILD_SHIPPING
#include "Commander/Orders/GuLiUnitTaskSubsystem.h"
#include "Commander/Behavior/GuLiCommanderAbilityBridge.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Gameplay/Resources/GuLiMiningVehiclePawn.h"
#include "Gameplay/Resources/GuLiResourceFactoryActor.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Gameplay/Building/GuLiConstructionVehiclePawn.h"
#include "Gameplay/Building/GuLiConstructionWorkComponent.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Gameplay/Building/GuLiBuildingCatalog.h"
#include "Gameplay/Building/GuLiBuildingProductionComponent.h"
#include "Gameplay/Building/GuLiPlacedBuilding.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "NavigationSystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

namespace GuLiOrderBusinessProbe
{
struct FRun
{
	FString Path;
	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<AGuLiBattlePlayerState> Owner;
	TWeakObjectPtr<AGuLiMiningVehiclePawn> Miner;
	TWeakObjectPtr<AGuLiConstructionVehiclePawn> Builder, Helper;
	TWeakObjectPtr<AGuLiPlacedBuilding> NearSite, FarSite, UnreachableSite, EnemySite, EqualSite;
	TWeakObjectPtr<AGuLiResourceFactoryActor> Factory;
	TArray<FGuLiSoldierId> NewSoldiers;
	FVector Origin, MoveTarget;
	int32 Stage = 0, SourceTerritory = INDEX_NONE, Destination = INDEX_NONE;
	uint32 NextCommand = 800000, SavedExecution = 0;
	uint16 Cluster = 0;
	double StartTime = FPlatformTime::Seconds(), StageTime = StartTime, NextTime = 0;
	float SavedWork = 0;
	bool Finished = false;
	TSharedRef<FJsonObject> Checks = MakeShared<FJsonObject>();
	TArray<FString> Errors;
	UGuLiUnitTaskSubsystem& Tasks() const { return *World->GetSubsystem<UGuLiUnitTaskSubsystem>(); }
	UGuLiResourceWorldSubsystem& Resources() const { return *World->GetSubsystem<UGuLiResourceWorldSubsystem>(); }
	void Check(const TCHAR* Name, bool Value)
	{ Checks->SetBoolField(Name, Value); if (!Value) Errors.Add(Name); }
	void Finish(const FString& Error = {})
	{
		if (!Error.IsEmpty()) Errors.Add(Error);
		auto Report = MakeShared<FJsonObject>(); Report->SetBoolField(TEXT("passed"), Errors.IsEmpty());
		Report->SetObjectField(TEXT("checks"), Checks); Report->SetNumberField(TEXT("stage"), Stage);
		Report->SetStringField(TEXT("fixture"), TEXT("authored map; test vehicles teleported between work boundaries; cargo/rates set only by opt-in probe; production scheduler and business components"));
		TArray<TSharedPtr<FJsonValue>> Values; for (const auto& ErrorText : Errors) Values.Add(MakeShared<FJsonValueString>(ErrorText));
		Report->SetArrayField(TEXT("errors"), Values);
		if (Miner.IsValid()) { Report->SetNumberField(TEXT("miner_state"), int32(Miner->GetTaskState())); Report->SetNumberField(TEXT("cargo"), Miner->GetCargoTotal()); }
		if (Builder.IsValid())
		{
			const auto* State = StateOf(*Builder);
			Report->SetStringField(TEXT("builder_error"), State ? State->Error : TEXT("no state"));
			Report->SetStringField(TEXT("builder_location"), Builder->GetActorLocation().ToString());
		}
		FString Json; FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
		FFileHelper::SaveStringToFile(Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		Finished = true; FPlatformMisc::RequestExit(false);
	}
	void Next(int32 NewStage, double Delay = .4)
	{ Stage = NewStage; StageTime = FPlatformTime::Seconds(); NextTime = StageTime + Delay; UE_LOG(LogTemp, Display, TEXT("OrderBusiness stage=%d"), Stage); }
	const FGuLiUnitTaskState* StateOf(APawn& Pawn) const
	{ return Tasks().FindState(FGuLiTaskUnitId::Actor(CastChecked<IGuLiEngineeringVehicle>(&Pawn)->GetStableActorId())); }
	bool Send(APawn& Pawn, EGuLiTaskDisposition Disposition, EGuLiUnitTaskKind Kind = EGuLiUnitTaskKind::Move,
		FVector Target = FVector::ZeroVector, uint32 Building = 0, uint16 MineCluster = 0, FName Territory = NAME_None)
	{
		FGuLiCommanderSelectionState Selection; Selection.SelectionRevision = 1;
		Selection.ActorIds.Add(CastChecked<IGuLiEngineeringVehicle>(&Pawn)->GetStableActorId());
		FGuLiUnitTaskCommand Command; Command.CommandId = NextCommand++; Command.SelectionRevision = 1;
		Command.Disposition = Disposition; Command.Kind = Kind; Command.Target = Target;
		Command.BuildingId = Building; Command.ClusterId = MineCluster; Command.TerritoryId = Territory;
		if (Kind == EGuLiUnitTaskKind::Special) Command.SpecialTaskId = Building ? 2 : 1;
		FString Message; int32 Accepted, Rejected;
		const bool Result = Tasks().Submit(*Owner, Selection, Command, Message, Accepted, Rejected);
		if (!Result) UE_LOG(LogTemp, Display, TEXT("OrderBusiness rejected: %s"), *Message);
		return Result;
	}
	FVector Ground(FVector Desired, APawn& Pawn) const
	{
		auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World.Get());
		FNavLocation Out;
		const auto* Data = Nav->GetNavDataForProps(Pawn.GetNavAgentPropertiesRef(), Desired);
		return Nav->ProjectPointToNavigation(Desired, Out, FVector(3000,3000,10000), Data) ? Out.Location : Desired;
	}
	void Place(ACharacter& Pawn, FVector GroundPosition)
	{ Pawn.SetActorLocation(GroundPosition + FVector(0,0,Pawn.GetCapsuleComponent()->GetScaledCapsuleHalfHeight()), false, nullptr, ETeleportType::TeleportPhysics); }
	AGuLiPlacedBuilding* Site(FVector At, EGuLiTeam Team = EGuLiTeam::Red)
	{
		const auto& Def = *UGuLiBuildingCatalog::LoadDefaultCatalog()->FindDefinition(EGuLiBuildingType::SentryTurret);
		FTransform Transform(FRotator::ZeroRotator, At + FVector(0,0,Def.CollisionExtent.Z));
		auto* Actor = World->SpawnActorDeferred<AGuLiPlacedBuilding>(AGuLiPlacedBuilding::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		Actor->InitializeFromDefinition(Def.DefinitionId, Team, FGuid::NewGuid(), INDEX_NONE, EGuLiBuildingOrigin::Manual, false);
		Actor->FinishSpawning(Transform); return Actor;
	}
	UGuLiBuildingLifecycleComponent& Life(AGuLiPlacedBuilding& SiteActor) const
	{ return *SiteActor.FindComponentByClass<UGuLiBuildingLifecycleComponent>(); }
	UGuLiConstructionWorkComponent& Work(AGuLiConstructionVehiclePawn& Pawn) const
	{ return *Pawn.FindComponentByClass<UGuLiConstructionWorkComponent>(); }
	void WarpFactoryUnloadPoint()
	{
		if (!IsValid(Miner->Factory)) return;
		const auto Points = Miner->Factory->GetUnloadPoints();
		const int32 Attempt = FMath::Max(0, Miner->UnloadPointAttempts - 1);
		Place(*Miner, Points[(Miner->GetStableActorId().Value + Attempt) % Points.Num()]);
	}
	bool Tick(float)
	{
		if (Finished) return false;
		const double Now = FPlatformTime::Seconds();
		if (Now - StartTime > 300 || (Stage && Now - StageTime > 65)) { Finish(FString::Printf(TEXT("timeout at stage %d"), Stage)); return false; }
		if (Now < NextTime) return true;
		if (!World.IsValid())
		{
			for (const auto& Context : GEngine->GetWorldContexts()) if (auto* W = Context.World(); W && W->WorldType == EWorldType::Game && W->HasBegunPlay() && W->GetNetMode() != NM_Client) World = W;
			if (!World.IsValid()) return true;
		}
		if (!Resources().IsRuntimeReady() || !World->GetSubsystem<UGuLiBattleAuthoritySubsystem>()->HasSpawnedAuthorityPopulation()) return true;
		if (Stage == 0)
		{
			for (TActorIterator<AGuLiMiningVehiclePawn> It(World.Get()); It; ++It) if (It->GetTeam() == EGuLiTeam::Red) { Miner = *It; break; }
			for (TActorIterator<AGuLiConstructionVehiclePawn> It(World.Get()); It; ++It) if (It->GetTeam() == EGuLiTeam::Red) { Builder = *It; break; }
			if (!Miner.IsValid() || !Builder.IsValid()) return true;
			Owner = World->SpawnActor<AGuLiBattlePlayerState>(); Owner->EnsureServerPlayerGuid();
			Owner->SetServerRoleAssignment(EGuLiTeam::Red, EGuLiCommanderRole::Commander, 0);
			for (TActorIterator<AActor> It(World.Get()); It; ++It) if (auto* P = It->FindComponentByClass<UGuLiBuildingProductionComponent>()) P->SetComponentTickEnabled(false);
			Send(*Builder, EGuLiTaskDisposition::Stop); Send(*Miner, EGuLiTaskDisposition::Stop);
			Origin = Ground(FVector(-175000,-110000,0), *Builder); Place(*Builder, Origin);
			Helper = Resources().SpawnConstructionVehicle(EGuLiTeam::Red, Origin+FVector(0,500,0));
			if (!Helper.IsValid()) { Finish(TEXT("helper spawn failed")); return false; }
			Send(*Helper, EGuLiTaskDisposition::Stop);
			NearSite = Site(Ground(Origin + FVector(4000,0,0), *Builder));
			EqualSite = Site(Life(*NearSite).GetGroundLocation());
			FarSite = Site(Ground(Origin + FVector(13000,0,0), *Builder));
			EnemySite = Site(Ground(Origin + FVector(0,5000,0), *Builder), EGuLiTeam::Blue);
			UnreachableSite = Site(FVector(10000000,10000000,0));
			Next(1, 3); return true;
		}
		if (Stage == 1)
		{
			FGuLiUnitTaskCommand Pick; Pick.Kind = EGuLiUnitTaskKind::Special; Pick.SpecialTaskId = 2;
			Check(TEXT("nearest_complete_ground_path_selected"), GuLiCommanderAbilities::BuildAutomatic(EGuLiCommanderBehavior::Construction, *World, StateOf(*Builder)->Context, Pick) && Pick.BuildingId == Life(*NearSite).GetState().InstanceId);
			FVector EqualPosition, NearPosition; float EqualLength, NearLength;
			Check(TEXT("equal_path_length_breaks_tie_by_instance_id"), Work(*Builder).PrepareBuilding(Life(*NearSite), NearPosition, NearLength)
				&& Work(*Builder).PrepareBuilding(Life(*EqualSite), EqualPosition, EqualLength) && FMath::IsNearlyEqual(NearLength, EqualLength)
				&& Pick.BuildingId < Life(*EqualSite).GetState().InstanceId);
			EqualSite->Destroy();
			FVector Position; float Length;
			Check(TEXT("unreachable_site_rejected"), !Work(*Builder).PrepareBuilding(Life(*UnreachableSite), Position, Length));
			Check(TEXT("enemy_site_rejected"), !Work(*Builder).PrepareBuilding(Life(*EnemySite), Position, Length));
			if (!Work(*Builder).PrepareBuilding(Life(*NearSite), Position, Length)) { Finish(TEXT("fixture site unreachable")); return false; }
			Place(*Builder, Position); Place(*Helper, Position+FVector(0,280,0));
			Send(*Builder, EGuLiTaskDisposition::Replace, EGuLiUnitTaskKind::Move, Builder->GetActorLocation());
			Send(*Helper, EGuLiTaskDisposition::Replace, EGuLiUnitTaskKind::Move, Helper->GetActorLocation()); Next(2, 2);
		}
		else if (Stage == 2)
		{
			if (Work(*Builder).GetTarget() != &Life(*NearSite) || Work(*Helper).GetTarget() != &Life(*NearSite)) return true;
			Check(TEXT("multiple_builders_share_site"), Life(*NearSite).GetState().WorkDone > 0);
			SavedWork = Life(*NearSite).GetState().WorkDone;
			Send(*Builder, EGuLiTaskDisposition::Stop); Tasks().RegisterActor(*Builder); Next(3, 1);
		}
		else if (Stage == 3)
		{
			Check(TEXT("builder_stop_and_duplicate_registration"), StateOf(*Builder)->bStopped && !Work(*Builder).GetTarget());
			Check(TEXT("other_builder_continues_and_progress_retained"), Life(*NearSite).GetState().WorkDone >= SavedWork && Work(*Helper).GetTarget());
			Send(*Helper, EGuLiTaskDisposition::Stop);
			Send(*Builder, EGuLiTaskDisposition::Replace, EGuLiUnitTaskKind::Move, Builder->GetActorLocation()); Next(4, 2);
		}
		else if (Stage == 4)
		{
			if (Work(*Builder).GetTarget() != &Life(*NearSite)) return true;
			Check(TEXT("persistent_construction_resumes_after_manual_move"), StateOf(*Builder)->Active.IsSet() && StateOf(*Builder)->Active->bAutomatic);
			Send(*Builder, EGuLiTaskDisposition::Append, EGuLiUnitTaskKind::Move, Builder->GetActorLocation());
			Check(TEXT("construction_append_keeps_current_work"), Work(*Builder).GetTarget() == &Life(*NearSite) && StateOf(*Builder)->Active->bYieldRequested);
			Life(*NearSite).AddConstructionWork(Life(*NearSite).GetDefinition().ConstructionWork);
			Next(5, 2);
		}
		else if (Stage == 5)
		{
			Check(TEXT("completed_building_yields_to_manual_queue"), StateOf(*Builder)->Queue.IsEmpty() && (!StateOf(*Builder)->Active.IsSet() || StateOf(*Builder)->Active->Command.BuildingId != Life(*NearSite).GetState().InstanceId));
			Check(TEXT("new_site_command_accepted"), Send(*Builder, EGuLiTaskDisposition::Replace, EGuLiUnitTaskKind::Special, FarSite->GetActorLocation(), Life(*FarSite).GetState().InstanceId));
			Send(*Builder, EGuLiTaskDisposition::Append, EGuLiUnitTaskKind::Move, Builder->GetActorLocation());
			FarSite->Destroy(); Next(6, 2);
		}
		else if (Stage == 6)
		{
			Check(TEXT("destroyed_target_fails_and_next_task_runs"), StateOf(*Builder)->Queue.IsEmpty() && !StateOf(*Builder)->Error.IsEmpty());
			Check(TEXT("no_target_keeps_persistent_grant"), !StateOf(*Builder)->AutomaticBehaviors.IsEmpty() && !StateOf(*Builder)->AutomaticBehaviors[0].bConsumed);
			Send(*Builder, EGuLiTaskDisposition::Stop);
			Miner->Cargo.Blue = 3; Miner->Cargo.Red = 0;
			Send(*Miner, EGuLiTaskDisposition::Stop); Tasks().RegisterActor(*Miner); Miner->ForceAutomaticControl();
			Check(TEXT("miner_stop_preserves_cargo_and_ignores_legacy_auto_restore"), Miner->GetCargoTotal() == 3 && StateOf(*Miner)->bStopped);
			Factory = Resources().FindNearestFriendlyFactory(EGuLiTeam::Red, Miner->GetActorLocation());
			if (!Factory.IsValid()) { Finish(TEXT("no fixture factory")); return false; }
			Miner->DockingSeconds = .2f;
			Send(*Miner, EGuLiTaskDisposition::Replace, EGuLiUnitTaskKind::ReturnToFactory);
			Next(7);
		}
		else if (Stage == 7)
		{
			if (Miner->GetTaskState() == EGuLiMiningTaskState::ReturningToFactory) WarpFactoryUnloadPoint();
			if (Miner->GetTaskState() != EGuLiMiningTaskState::Docking) return true;
			Check(TEXT("return_factory_unloading_started"), true);
			Send(*Miner, EGuLiTaskDisposition::Stop);
			Check(TEXT("factory_stop_is_immediate_and_retains_cargo"), StateOf(*Miner)->bStopped
				&& !StateOf(*Miner)->bCancelPending && Miner->GetTaskState() != EGuLiMiningTaskState::Docking && Miner->GetCargoTotal() == 3);
			Send(*Miner, EGuLiTaskDisposition::Replace, EGuLiUnitTaskKind::Move, Miner->GetActorLocation()+FVector(2000,0,0));
			Send(*Miner, EGuLiTaskDisposition::Stop); Next(8);
		}
		else if (Stage == 8)
		{
			if (StateOf(*Miner)->bCancelPending) return true;
			Check(TEXT("latest_stop_needs_no_factory_exit"), StateOf(*Miner)->bStopped && !StateOf(*Miner)->Active.IsSet() && Miner->GetCargoTotal() == 3);
			Send(*Miner, EGuLiTaskDisposition::Replace, EGuLiUnitTaskKind::Move, Miner->GetActorLocation()); Next(9, 2);
		}
		else if (Stage == 9)
		{
			if (!StateOf(*Miner)->Active.IsSet() || !StateOf(*Miner)->Active->bAutomatic || !Miner->TargetNodeId) return true;
			Check(TEXT("persistent_mining_resumes_after_manual_move"), true);
			Cluster = Miner->TargetClusterId;
			Send(*Miner, EGuLiTaskDisposition::Replace, EGuLiUnitTaskKind::Special, {}, 0, Cluster);
			Send(*Miner, EGuLiTaskDisposition::Append, EGuLiUnitTaskKind::Move, Miner->GetActorLocation());
			Miner->CargoCapacity = 1; Miner->MiningRatePerSecond = 20;
			Next(10);
		}
		else if (Stage == 10)
		{
			if (Miner->GetTaskState() == EGuLiMiningTaskState::MovingToCluster && Miner->TargetNodeId)
			{
				FVector Approach, Target; float Length;
				if (Miner->FindReachableMiningApproach(Miner->TargetNodeId, Approach, Length) && Resources().GetNodeMiningTarget(Miner->TargetNodeId, Target))
				{ Place(*Miner, Approach); Miner->SetActorRotation(FRotator(0,(Target-Miner->GetActorLocation()).Rotation().Yaw,0)); }
			}
			if (Miner->GetTaskState() != EGuLiMiningTaskState::ReturningToFactory) return true;
			Check(TEXT("manual_mining_collects_before_appended_move"), Miner->GetCargoTotal() == 1 && StateOf(*Miner)->Queue.Num() == 1);
			WarpFactoryUnloadPoint(); Next(11);
		}
		else if (Stage == 11)
		{
			const auto* State = StateOf(*Miner);
			if (!State->Active.IsSet() || State->Active->Command.Kind != EGuLiUnitTaskKind::Move || !State->Queue.IsEmpty()) return true;
			Check(TEXT("mining_full_round_yields_after_unload"), Miner->GetCargoTotal() == 0 && Miner->GetTaskState() != EGuLiMiningTaskState::Docking);
			Send(*Miner, EGuLiTaskDisposition::Stop);
			const auto& Map = *Resources().GetMapDefinition();
			for (int32 I = 0; I < Map.Territories.Num(); ++I) if (Resources().CanUseStrongholdTransit(I, EGuLiTeam::Red)) { SourceTerritory = I; break; }
			for (int32 I = 0; I < Map.Territories.Num(); ++I) if (Resources().GetStrongholdTopology().IsAdjacent(SourceTerritory, I)) { Destination = I; break; }
			if (SourceTerritory == INDEX_NONE || Destination == INDEX_NONE) { Finish(TEXT("no transit fixture route")); return false; }
			Resources().SetTerritoryOwner(uint8(Destination), EGuLiTeam::Red);
			Place(*Builder, Ground(Resources().GetTerritoryGroundLocation(SourceTerritory)+FVector(2000,0,0), *Builder));
			MoveTarget = Ground(Resources().GetTerritoryGroundLocation(Destination)+FVector(2500,0,0), *Builder);
			Next(12, 2);
		}
		else if (Stage == 12)
		{
			if (!Send(*Builder, EGuLiTaskDisposition::Replace, EGuLiUnitTaskKind::Transit, MoveTarget, 0, 0, Resources().GetMapDefinition()->Territories[Destination].TerritoryId)) { NextTime = Now + 1; return true; }
			Next(13);
		}
		else if (Stage == 13)
		{
			if (!Builder->FindComponentByClass<UGuLiEngineeringTravelComponent>()->IsInTransit()) return true;
			Send(*Builder, EGuLiTaskDisposition::Stop);
			Check(TEXT("transit_stop_is_deferred"), StateOf(*Builder)->bStopped && StateOf(*Builder)->bCancelPending);
			Send(*Builder, EGuLiTaskDisposition::Replace, EGuLiUnitTaskKind::Move, MoveTarget);
			Check(TEXT("replacement_during_transit_preserves_safe_exit"), !StateOf(*Builder)->bStopped && StateOf(*Builder)->bCancelPending && StateOf(*Builder)->Queue.Num() == 1);
			Next(14);
		}
		else if (Stage == 14)
		{
			if (Builder->FindComponentByClass<UGuLiEngineeringTravelComponent>()->IsInTransit() || StateOf(*Builder)->bCancelPending) return true;
			Check(TEXT("transit_lands_before_replacement_runs"), !StateOf(*Builder)->bStopped);
			Send(*Builder, EGuLiTaskDisposition::Stop);
			auto* Authority = World->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
			TArray<FVector> Positions{Origin+FVector(0,2000,0), Origin+FVector(0,2500,0)};
			Authority->SpawnSoldierBatch(EGuLiTeam::Red, 1, Positions, NewSoldiers);
			if (NewSoldiers.Num() != 2) { Finish(TEXT("new soldier fixture failed")); return false; }
			Next(15, 1);
		}
		else if (Stage == 15)
		{
			auto* A = Tasks().FindState(FGuLiTaskUnitId::Soldier(NewSoldiers[0]));
			Check(TEXT("new_soldier_gets_own_initial_grant"), A && A->Active.IsSet() && !A->AutomaticBehaviors[0].bConsumed);
			FGuLiCommanderSelectionState Selection; Selection.SelectionRevision = 1; Selection.Cohorts.AddDefaulted_GetRef().MemberIds.Add(NewSoldiers[0]);
			FGuLiUnitTaskCommand Command; Command.CommandId = NextCommand++; Command.SelectionRevision = 1; Command.Disposition = EGuLiTaskDisposition::Append; Command.Target = Origin;
			FString Message; int32 Accepted, Rejected; Tasks().Submit(*Owner, Selection, Command, Message, Accepted, Rejected);
			Check(TEXT("advance_append_does_not_consume_until_stage_boundary"), !A->AutomaticBehaviors[0].bConsumed && A->Active->bYieldRequested);
			Tasks().NotifyAdvanceStage(NewSoldiers[0]); Next(16);
		}
		else if (Stage == 16)
		{
			const auto* A = Tasks().FindState(FGuLiTaskUnitId::Soldier(NewSoldiers[0]));
			const auto* B = Tasks().FindState(FGuLiTaskUnitId::Soldier(NewSoldiers[1]));
			Check(TEXT("advance_boundary_consumes_only_handed_over_member"), A->AutomaticBehaviors[0].bConsumed && !B->AutomaticBehaviors[0].bConsumed);
			Tasks().RegisterSoldiers(EGuLiTeam::Red, NewSoldiers); Tasks().NotifyAdvanceStage(NewSoldiers[0]);
			Check(TEXT("stale_stage_callback_cannot_restore_initial_task"), A->AutomaticBehaviors[0].bConsumed);
			FGuLiCommanderSelectionState Mixed; Mixed.SelectionRevision = 1;
			Mixed.ActorIds = {Miner->GetStableActorId(), Builder->GetStableActorId()};
			Mixed.Cohorts.AddDefaulted_GetRef().MemberIds.Add(NewSoldiers[1]);
			FGuLiUnitTaskCommand Mining; Mining.CommandId = NextCommand++; Mining.SelectionRevision = 1;
			Mining.Kind = EGuLiUnitTaskKind::Special; Mining.SpecialTaskId = 1; Mining.ClusterId = Cluster;
			FString Message; int32 Accepted, Rejected;
			TSet<FGuLiTaskUnitId> AcceptedUnits;
			Tasks().Submit(*Owner, Mixed, Mining, Message, Accepted, Rejected, &AcceptedUnits);
			Check(TEXT("mixed_selection_accepts_only_capable_unit"), Accepted == 1 && Rejected == 2 && StateOf(*Builder)->bStopped && !B->AutomaticBehaviors[0].bConsumed
				&& AcceptedUnits.Num() == 1 && AcceptedUnits.Contains(FGuLiTaskUnitId::Actor(Miner->GetStableActorId())) && !StateOf(*Builder)->Error.IsEmpty());
			Send(*Miner, EGuLiTaskDisposition::Stop);
			World->GetSubsystem<UGuLiBattleAuthoritySubsystem>()->ApplyDamage(NewSoldiers[0], 1000000);
			Helper->Destroy(); Next(17);
		}
		else if (Stage == 17)
		{
			Check(TEXT("dead_unit_state_removed"), !Tasks().HasState(FGuLiTaskUnitId::Soldier(NewSoldiers[0])));
			FarSite = Site(Ground(Builder->GetActorLocation()+FVector(4000,0,0), *Builder));
			Next(18, 2);
		}
		else if (Stage == 18)
		{
			if (!Send(*Builder, EGuLiTaskDisposition::Replace, EGuLiUnitTaskKind::Special, FarSite->GetActorLocation(), Life(*FarSite).GetState().InstanceId)) return true;
			Next(19);
		}
		else if (Stage == 19)
		{
			if (Work(*Builder).GetTarget() != &Life(*FarSite)) return true;
			FarSite->SetBuildingTeamAuthority(EGuLiTeam::Blue); Next(20);
		}
		else if (Stage == 20)
		{
			Check(TEXT("active_site_ownership_change_invalidates_work"), !Work(*Builder).GetTarget() && !StateOf(*Builder)->Error.IsEmpty());
			Miner->Cargo.Blue = 2;
			Send(*Miner, EGuLiTaskDisposition::Replace, EGuLiUnitTaskKind::ReturnToFactory); Next(21);
		}
		else if (Stage == 21)
		{
			if (Miner->GetTaskState() == EGuLiMiningTaskState::ReturningToFactory) WarpFactoryUnloadPoint();
			if (Miner->GetTaskState() != EGuLiMiningTaskState::Docking) return true;
			Factory = Miner->Factory;
			Factory->SetBuildingTeamAuthority(EGuLiTeam::Blue); Next(22);
		}
		else if (Stage == 22)
		{
			if (Miner->GetTaskState() == EGuLiMiningTaskState::Docking) return true;
			Check(TEXT("factory_ownership_loss_preserves_cargo_for_rerouting"), Miner->GetCargoTotal() == 2);
			Factory->SetBuildingTeamAuthority(EGuLiTeam::Red);
			Finish(); return false;
		}
		return true;
	}
};
void Start(const TArray<FString>& Args, UWorld*)
{
	if (Args.Num() != 1) return;
	auto Run = MakeShared<FRun>(); Run->Path = FPaths::ConvertRelativePathToFull(Args[0]);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Run](float Delta){ return Run->Tick(Delta); }));
}
FAutoConsoleCommandWithWorldAndArgs Probe(TEXT("gs.Commander.OrderBusinessProbe"), TEXT("Opt-in authored-map task fixture: report.json"), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Start));
}
#endif
