// Opt-in integration fixture. Runs only in a disposable authored-map game World.
#include "CoreMinimal.h"
#if !UE_BUILD_SHIPPING
#include "Commander/Orders/GuLiUnitTaskSubsystem.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Resources/GuLiMiningVehiclePawn.h"
#include "Gameplay/Building/GuLiConstructionVehiclePawn.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "NavigationSystem.h"
#include "NavModifierComponent.h"
#include "NavAreas/NavArea_Null.h"
#include "NavMesh/RecastNavMesh.h"
#include "Navigation/PathFollowingComponent.h"
#include "Camera/CameraActor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

namespace GuLiMoveContinuityProbe
{
constexpr auto Revision = TEXT("blocked-goal-v2");
struct FUnit
{
	FGuLiTaskUnitId Id;
	TWeakObjectPtr<APawn> Pawn;
	FVector Target;
	uint32 Execution = 0;
};
struct FRun
{
	FString Path;
	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<AGuLiBattlePlayerState> Owner;
	TWeakObjectPtr<ACameraActor> Camera;
	FVector BlockedTarget = FVector::ZeroVector;
	TArray<FUnit> Units;
	TArray<FString> Errors;
	TArray<TSharedPtr<FJsonValue>> Samples;
	TSharedRef<FJsonObject> Checks = MakeShared<FJsonObject>();
	int32 Stage = 0, PendingSamples = 0;
	uint32 NextCommand = 910000;
	uint32 RetainedRoute = 0;
	double Started = FPlatformTime::Seconds(), StageStarted = Started, SampleAt = 0;
	int32 CaptureIndex = 0;
	double CaptureAt = 0;
	UGuLiUnitTaskSubsystem& Tasks() const { return *World->GetSubsystem<UGuLiUnitTaskSubsystem>(); }
	UGuLiBattleAuthoritySubsystem& Authority() const { return *World->GetSubsystem<UGuLiBattleAuthoritySubsystem>(); }
	const FGuLiUnitTaskState* State(const FUnit& U) const { return Tasks().FindState(U.Id); }
	bool Check(const FString& Label, bool Passed)
	{
		Checks->SetBoolField(Label, Passed);
		if (!Passed) { Errors.Add(Label); UE_LOG(LogTemp, Error, TEXT("MoveContinuity: %s"), *Label); }
		return Passed;
	}
	FVector Position(const FUnit& U) const
	{
		if (U.Pawn.IsValid()) return U.Pawn->GetActorLocation();
		FGuLiSoldierNavigationDebug Nav; Authority().TryGetSoldierNavigationDebug(FGuLiSoldierId(U.Id.Id), Nav); return Nav.Location;
	}
	FVector Velocity(const FUnit& U) const
	{
		if (U.Pawn.IsValid()) return U.Pawn->GetVelocity();
		FGuLiSoldierNavigationDebug Nav; Authority().TryGetSoldierNavigationDebug(FGuLiSoldierId(U.Id.Id), Nav); return Nav.Velocity;
	}
	uint32 Route(const FUnit& U) const
	{
		if (U.Pawn.IsValid())
			return uint32(CastChecked<AAIController>(U.Pawn->GetController())->GetPathFollowingComponent()->GetCurrentRequestId());
		FGuLiSoldierNavigationDebug Nav; Authority().TryGetSoldierNavigationDebug(FGuLiSoldierId(U.Id.Id), Nav); return Nav.ActiveOrderId;
	}
	bool Send(TConstArrayView<FUnit> Selected, FVector Target, EGuLiTaskDisposition How = EGuLiTaskDisposition::Replace)
	{
		TArray<FGuLiSoldierId> Soldiers; TArray<FGuLiControllableActorId> Actors;
		for (const auto& U : Selected) if (U.Id.bActor) Actors.Add(FGuLiControllableActorId(U.Id.Id)); else Soldiers.Add(FGuLiSoldierId(U.Id.Id));
		FGuLiCommanderSelectionState Selection;
		if (!Authority().SetExplicitSelection(EGuLiTeam::Red, Soldiers, Actors, Selection)) return false;
		FGuLiUnitTaskCommand C; C.CommandId = NextCommand++; C.SelectionRevision = Selection.SelectionRevision;
		C.Disposition = How; C.Target = Target; C.bGroundMoveOnly = true;
		FString Message; int32 Accepted = 0, Rejected = 0;
		return Tasks().Submit(*Owner, Selection, C, Message, Accepted, Rejected) && Accepted == Selected.Num() && Rejected == 0;
	}
	bool Send(const FUnit& U, FVector Target, EGuLiTaskDisposition How = EGuLiTaskDisposition::Replace)
	{ return Send(MakeArrayView(&U, 1), Target, How); }
	FVector Ground(FVector Point, const APawn* Pawn = nullptr) const
	{
		auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World.Get());
		FNavLocation Out;
		const auto* Data = Pawn ? Nav->GetNavDataForProps(Pawn->GetNavAgentPropertiesRef(), Point) : Nav->GetDefaultNavDataInstance();
		return Nav->ProjectPointToNavigation(Point, Out, FVector(3000,3000,10000), Data) ? Out.Location : Point;
	}
	void Next(int32 Value)
	{ Stage = Value; StageStarted = FPlatformTime::Seconds(); CaptureIndex = 0; CaptureAt = 0; UE_LOG(LogTemp, Display, TEXT("MoveContinuity stage=%d"), Stage); }
	bool Finish(const FString& Error = {})
	{
		if (!Error.IsEmpty()) Errors.Add(Error);
		auto Report = MakeShared<FJsonObject>(); Report->SetBoolField(TEXT("passed"), Errors.IsEmpty());
		Report->SetStringField(TEXT("probe_revision"), Revision);
		Report->SetNumberField(TEXT("stage"), Stage); Report->SetNumberField(TEXT("pending_samples"), PendingSamples);
		Report->SetObjectField(TEXT("checks"), Checks); Report->SetArrayField(TEXT("samples"), Samples);
		TArray<TSharedPtr<FJsonValue>> Values; for (const auto& E : Errors) Values.Add(MakeShared<FJsonValueString>(E));
		Report->SetArrayField(TEXT("errors"), Values);
		FString Json; FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
		FFileHelper::SaveStringToFile(Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		FPlatformMisc::RequestExit(false); return false;
	}
	bool Moving(const FUnit& U) const
	{
		const auto* S = State(U);
		return S && S->Active.IsSet() && !S->PendingMove.IsSet() && S->Active->bStarted && !S->Active->bPlanning
			&& S->Active->Command.Kind == EGuLiUnitTaskKind::Move && Velocity(U).Size2D() > 50;
	}
	void Observe(double Now)
	{
		if (Now < SampleAt || Units.IsEmpty()) return;
		SampleAt = Now + .05;
		for (int32 I = 0; I < Units.Num(); ++I)
		{
			const auto& U = Units[I]; const auto* S = State(U); if (!S) continue;
			auto Sample = MakeShared<FJsonObject>(); Sample->SetNumberField(TEXT("time"), Now-Started);
			Sample->SetNumberField(TEXT("stage"), Stage); Sample->SetNumberField(TEXT("unit"), I);
			Sample->SetNumberField(TEXT("speed"), Velocity(U).Size2D()); Sample->SetNumberField(TEXT("route"), Route(U));
			Sample->SetStringField(TEXT("position"), Position(U).ToString());
			Sample->SetBoolField(TEXT("pending"), S->PendingMove.IsSet());
			Samples.Add(MakeShared<FJsonValueObject>(Sample));
			if (Stage == 3)
			{
				if (S->PendingMove.IsSet()) ++PendingSamples;
				if (Velocity(U).Size2D() <= 1) Check(FString::Printf(TEXT("unit_%d_keeps_moving_through_replacement"), I), false);
			}
		}
		if (World->GetNetMode() != NM_DedicatedServer)
			if (auto* PC = World->GetFirstPlayerController())
			{
				if (!Camera.IsValid()) Camera = World->SpawnActor<ACameraActor>();
				const FVector Focus = Position(Units[Stage >= 6 ? 3 : Stage >= 4 ? 2 : 0]);
				const FVector At = Focus + FVector(-1600,-1200,2200);
				Camera->SetActorLocationAndRotation(At, (Focus-At).Rotation()); PC->SetViewTarget(Camera.Get());
				const int32 CaptureLimit = Stage == 2 || Stage == 3 ? 5 : 1;
				if (CaptureIndex < CaptureLimit && Stage >= 2 && Now-StageStarted > .25 && Now >= CaptureAt)
				{
					const FString Name = CaptureIndex ? FString::Printf(TEXT("stage_%d_frame_%d.png"),Stage,CaptureIndex) : FString::Printf(TEXT("stage_%d.png"),Stage);
					FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::GetPath(Path),Name), false, false);
					++CaptureIndex; CaptureAt = Now + .15;
				}
			}
	}
	bool Tick(float)
	{
		const double Now = FPlatformTime::Seconds();
		if (!Errors.IsEmpty()) return Finish();
		if (Now-Started > 180) return Finish(TEXT("Timeout"));
		if (!World.IsValid())
			for (const auto& Context : GEngine->GetWorldContexts())
				if (auto* W = Context.World(); W && W->WorldType == EWorldType::Game && W->HasBegunPlay() && W->GetNetMode() != NM_Client) World = W;
		if (!World.IsValid() || !Authority().HasSpawnedAuthorityPopulation()
			|| !World->GetSubsystem<UGuLiResourceWorldSubsystem>()->IsRuntimeReady()) return true;
		Observe(Now);
		if (Stage == 0)
		{
			AGuLiMiningVehiclePawn* Miner = nullptr; AGuLiConstructionVehiclePawn* Builder = nullptr;
			for (TActorIterator<AGuLiMiningVehiclePawn> It(World.Get()); It; ++It) if (It->GetTeam() == EGuLiTeam::Red) { Miner = *It; break; }
			for (TActorIterator<AGuLiConstructionVehiclePawn> It(World.Get()); It; ++It) if (It->GetTeam() == EGuLiTeam::Red) { Builder = *It; break; }
			if (!Miner || !Builder) return true;
			Owner = World->SpawnActor<AGuLiBattlePlayerState>(); Owner->EnsureServerPlayerGuid();
			Owner->SetServerRoleAssignment(EGuLiTeam::Red, EGuLiCommanderRole::Commander, 0);
			const FVector Origin(-175000,-110000,0);
			// Remove all navigation around a goal away from the old routes. Mass goals
			// are grounded in XY, so a high Z alone does not make them unreachable.
			BlockedTarget = Ground(Origin+FVector(45000,60000,0));
			auto* Blocker = World->SpawnActor<AActor>();
			auto* Root = NewObject<USceneComponent>(Blocker);
			Blocker->SetRootComponent(Root); Root->RegisterComponent();
			Blocker->SetActorLocation(BlockedTarget);
			auto* Modifier = NewObject<UNavModifierComponent>(Blocker);
			Modifier->FailsafeExtent = FVector(12000,12000,50000); // Exceeds the 9000 cm slot search radius.
			Modifier->SetAreaClass(UNavArea_Null::StaticClass()); Modifier->RegisterComponent();
			TArray<FVector> Starts{Ground(Origin+FVector(0,6000,0)), Ground(Origin+FVector(0,8000,0))};
			TArray<FGuLiSoldierId> Ids; Authority().SpawnSoldierBatch(EGuLiTeam::Red,1,Starts,Ids);
			if (Ids.Num() != 2) return Finish(TEXT("Soldier fixture spawn failed"));
			for (auto Id : Ids) { FUnit U; U.Id = FGuLiTaskUnitId::Soldier(Id); Units.Add(U); }
			for (ACharacter* Pawn : {static_cast<ACharacter*>(Miner), static_cast<ACharacter*>(Builder)})
			{
				FUnit U; U.Pawn = Pawn; U.Id = FGuLiTaskUnitId::Actor(CastChecked<IGuLiEngineeringVehicle>(Pawn)->GetStableActorId());
				Send(U, {}, EGuLiTaskDisposition::Stop);
				const FVector At = Ground(Origin+FVector(0, Pawn == Miner ? 0 : -6000,0), Pawn);
				Pawn->SetActorLocation(At+FVector(0,0,Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),false,nullptr,ETeleportType::TeleportPhysics);
				Units.Add(U);
			}
			for (auto& U : Units) { Send(U, {}, EGuLiTaskDisposition::Stop); U.Target = Ground(Position(U)+FVector(70000,0,0), U.Pawn.Get()); }
			Next(1);
		}
		else if (Stage == 1 && Now-StageStarted > 1 && !UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World.Get()))
		{
			auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World.Get());
			bool bBlocked = true; int32 CheckedMeshes = 0;
			for (TActorIterator<ARecastNavMesh> It(World.Get()); It; ++It)
			{
				FNavLocation Projected; ++CheckedMeshes;
				bBlocked &= !Nav->ProjectPointToNavigation(BlockedTarget, Projected, FVector(150,150,50000), *It);
			}
			if (!Check(TEXT("unreachable_fixture_has_no_navigation"), bBlocked && CheckedMeshes > 0)) return Finish();
			for (int32 I=0; I<Units.Num(); ++I)
			{
				auto& U = Units[I];
				if (!Check(FString::Printf(TEXT("initial_move_%d"),I), Send(U,U.Target) && State(U))) return Finish();
				const uint64 Version = State(U)->Version;
				Send(U,U.Target+FVector(2499,0,0));
				Check(FString::Printf(TEXT("waiting_reuse_%d"),I), State(U)->Version == Version && State(U)->Queue.Num() == 1
					&& FVector(State(U)->Queue[0].Target).Equals(U.Target,1));
			}
			Next(2);
		}
		else if (Stage == 2 && Now-StageStarted > 1 && !Units.ContainsByPredicate([&](const FUnit& U){return !Moving(U);}))
		{
			for (int32 I=0; I<Units.Num(); ++I)
			{
				auto& U = Units[I]; U.Execution = State(U)->Active->ExecutionId; const uint32 OldRoute = Route(U);
				Send(U,U.Target+FVector(0,2000,0),EGuLiTaskDisposition::Append);
				Check(FString::Printf(TEXT("shift_keeps_near_waypoint_%d"),I), State(U)->Queue.Num()==1);
				const auto Queries = Authority().GetNavigationStats().MovePlanningPathQueries;
				for (int32 Click=0; Click<12; ++Click) Send(U,U.Target+FVector(0,Click%2 ? 2500 : 0,0));
				Check(FString::Printf(TEXT("running_reuse_%d"),I), State(U)->Active->ExecutionId == U.Execution && Route(U)==OldRoute
					&& State(U)->Queue.IsEmpty() && !State(U)->PendingMove.IsSet());
				Check(FString::Printf(TEXT("no_new_queries_%d"),I), Authority().GetNavigationStats().MovePlanningPathQueries==Queries);
				const FVector Before = Velocity(U);
				U.Target = Ground(U.Target+FVector(0,15000,0),U.Pawn.Get()); Send(U,U.Target);
				Check(FString::Printf(TEXT("replacement_preserves_velocity_%d"),I), Velocity(U).Equals(Before,.01) && Route(U)==OldRoute);
				U.Target = Ground(U.Target+FVector(6000,0,0),U.Pawn.Get()); Send(U,U.Target);
				if (!Check(FString::Printf(TEXT("latest_replacement_pending_%d"),I), State(U)->PendingMove.IsSet())) return Finish();
				const auto Version = State(U)->PendingMove->Version;
				Send(U,U.Target+FVector(0,2499,0));
				Check(FString::Printf(TEXT("latest_pending_reused_%d"),I), State(U)->PendingMove->Version==Version && State(U)->ManualTaskCount()==1);
			}
			Next(3);
		}
		else if (Stage == 3 && Now-StageStarted > 1 && !Units.ContainsByPredicate([&](const FUnit& U){return !Moving(U);}))
		{
			for (int32 I=0; I<Units.Num(); ++I)
			{
				auto& U = Units[I];
				Check(FString::Printf(TEXT("latest_target_committed_%d"),I), State(U)->Active->ExecutionId!=U.Execution
					&& FVector(State(U)->Active->Command.Target).Equals(U.Target,1));
				U.Execution=State(U)->Active->ExecutionId;
				Send(U,BlockedTarget);
			}
			Next(4);
		}
		else if (Stage == 4 && Now-StageStarted > 1 && !Units.ContainsByPredicate([&](const FUnit& U){return State(U)->PendingMove.IsSet();}))
		{
			for (int32 I=0; I<Units.Num(); ++I)
			{
				const auto& U=Units[I]; const auto* S=State(U);
				Check(FString::Printf(TEXT("failed_target_keeps_old_task_%d"),I), S->Active.IsSet() && S->Active->ExecutionId==U.Execution && S->Error.IsEmpty());
			}
			Check(TEXT("observed_moving_during_planning"),PendingSamples>0);
			const FVector Target=Ground(Units[0].Target+FVector(0,-25000,0)); Units[0].Target=Units[1].Target=Target;
			Send(MakeArrayView(Units.GetData(),2),Target);
			Next(5);
		}
		else if (Stage == 5 && State(Units[0])->PendingMove.IsSet() && State(Units[0])->PendingMove->bPlanning)
		{
			Send(Units[0],{},EGuLiTaskDisposition::Stop);
			Check(TEXT("stop_clears_pending"),State(Units[0])->bStopped && !State(Units[0])->PendingMove.IsSet() && Velocity(Units[0]).IsNearlyZero());
			Next(6);
		}
		else if (Stage == 6 && Now-StageStarted > 1 && !State(Units[1])->PendingMove.IsSet())
		{
			Check(TEXT("partial_cancel_keeps_other_member"),State(Units[1])->Active.IsSet()
				&& FVector(State(Units[1])->Active->Command.Target).Equals(Units[1].Target,1));
			Check(TEXT("stale_result_cannot_restart_stopped_member"),State(Units[0])->bStopped && Route(Units[0])==0);
			Send(Units[2],Units[2].Target+FVector(0,15000,0));
			auto* Control=Units[2].Pawn->FindComponentByClass<UGuLiExternalUnitControlComponent>();
			const FGuid Token=FGuid::NewGuid();
			Control->ApplyServerState(Token,false,true,Units[2].Pawn->GetActorTransform(),false,nullptr);
			Check(TEXT("external_control_clears_vehicle_replacement"),!State(Units[2])->PendingMove.IsSet());
			Control->ApplyServerState(Token,false,false,Units[2].Pawn->GetActorTransform(),false,nullptr);
			const FVector Target=Ground(Units[1].Target+FVector(0,25000,0)); Units[0].Target=Units[1].Target=Target;
			Send(MakeArrayView(Units.GetData(),2),Target);
			Next(7);
		}
		else if (Stage == 7 && State(Units[1])->PendingMove.IsSet() && State(Units[1])->PendingMove->bPlanning)
		{
			Authority().ApplyDamage(FGuLiSoldierId(Units[0].Id.Id),1000000);
			Next(8);
		}
		else if (Stage == 8 && Now-StageStarted > 1 && !State(Units[1])->PendingMove.IsSet())
		{
			Check(TEXT("death_does_not_cancel_surviving_member"),State(Units[1])->Active.IsSet()
				&& FVector(State(Units[1])->Active->Command.Target).Equals(Units[1].Target,1));
			Check(TEXT("dead_member_task_cleaned_up"),State(Units[0])==nullptr);
			if (!Errors.IsEmpty()) return Finish();
			auto& U = Units[1]; U.Execution = State(U)->Active->ExecutionId; RetainedRoute = Route(U);
			Send(U,BlockedTarget); Next(9);
		}
		else if (Stage == 9 && State(Units[1])->PendingMove.IsSet() && State(Units[1])->PendingMove->bPlanning)
		{
			// A deliberate hitch in this disposable process exercises the real 5 s
			// planning deadline. Check route preservation after simulation resumes.
			FPlatformProcess::Sleep(5.1f); Next(10);
		}
		else if (Stage == 10 && Now-StageStarted > 1 && !State(Units[1])->PendingMove.IsSet())
		{
			auto& U = Units[1]; const auto* S = State(U);
			Check(TEXT("timeout_keeps_old_task_and_route"),S->Active.IsSet() && S->Active->ExecutionId==U.Execution
				&& Route(U)==RetainedRoute && S->Error.IsEmpty());
			// The blocked destination requires multiple planning frames, ensuring
			// the next command supersedes an unfinished job rather than a committed path.
			Send(U,BlockedTarget); Next(11);
		}
		else if (Stage == 11 && State(Units[1])->PendingMove.IsSet() && State(Units[1])->PendingMove->bPlanning)
		{
			auto& U = Units[1]; const FVector Before = Velocity(U); U.Target += FVector(32000,0,0);
			Send(U,U.Target);
			Check(TEXT("superseding_inflight_plan_preserves_motion"),Velocity(U).Equals(Before,.01) && Route(U)==RetainedRoute);
			Next(12);
		}
		else if (Stage == 12 && Now-StageStarted > 1 && !State(Units[1])->PendingMove.IsSet())
		{
			auto& U = Units[1]; const auto* S = State(U);
			Check(TEXT("only_latest_inflight_target_commits"),S->Active.IsSet() && S->Active->ExecutionId!=U.Execution
				&& FVector(S->Active->Command.Target).Equals(U.Target,1));
			U.Target = Ground(Position(U)+FVector(2000,0,0)); Send(U,U.Target); Next(13);
		}
		else if (Stage == 13 && Now-StageStarted > 1 && !State(Units[1])->PendingMove.IsSet()
			&& !State(Units[1])->Active.IsSet() && State(Units[1])->Queue.IsEmpty())
		{
			auto& U = Units[1]; const uint64 Version = State(U)->Version;
			Check(TEXT("short_route_arrived"),FVector::Dist2D(Position(U),U.Target)<500);
			U.Target += FVector(1000,0,0);
			Check(TEXT("nearby_move_after_arrival_accepted"),Send(U,U.Target) && State(U)->Version>Version
				&& State(U)->Queue.Num()==1 && FVector(State(U)->Queue[0].Target).Equals(U.Target,1));
			return Finish();
		}
		return true;
	}
};
void Start(const TArray<FString>& Args, UWorld*)
{
	if (Args.Num()!=1) return;
	UE_LOG(LogTemp, Display, TEXT("MoveContinuity revision=%s"), Revision);
	auto Run=MakeShared<FRun>(); Run->Path=FPaths::ConvertRelativePathToFull(Args[0]);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Run](float Delta){return Run->Tick(Delta);}));
}
FAutoConsoleCommandWithWorldAndArgs Probe(TEXT("gs.Commander.MoveContinuityProbe"),
	TEXT("Authored-map movement reuse/replacement regression: report.json"),FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Start));
}
#endif
