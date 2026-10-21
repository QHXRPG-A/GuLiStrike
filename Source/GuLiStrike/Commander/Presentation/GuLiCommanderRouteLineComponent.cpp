#include "Commander/Presentation/GuLiCommanderRouteLineComponent.h"
#include "Commander/UI/GuLiSceneUISourceRegistry.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "PrimitiveSceneProxy.h"
#include "HAL/IConsoleManager.h"
#include "GuLiStrike.h"

namespace
{
TAutoConsoleVariable<int32> CVarRouteLinesPerFrame(TEXT("guli.Commander.RouteLinesPerFrame"),128,TEXT("Maximum selected route additions per render frame; visible anchors refresh every frame."));
TAutoConsoleVariable<float> CVarRouteLineBudgetMs(TEXT("guli.Commander.RouteLineBudgetMs"),.5f,TEXT("CPU route addition budget in milliseconds; visible anchors and invalidations update every frame."));
TAutoConsoleVariable<int32> CVarRouteLineDiagnostics(TEXT("guli.Commander.RouteLineDiagnostics"),0,TEXT("Log route backlog and changed chunk counts once per second."));
TAutoConsoleVariable<int32> CVarImmediateMoveFeedback(TEXT("gs.Commander.ImmediateMoveFeedback"),1,TEXT("Draw local move intent before planning/network completion; never predicts unit movement."));
}
bool UGuLiCommanderRouteLineComponent::IsImmediateFeedbackEnabled()
{ return CVarImmediateMoveFeedback.GetValueOnGameThread()!=0; }

UGuLiCommanderRouteLineComponent::UGuLiCommanderRouteLineComponent()
{
	PrimaryComponentTick.bCanEverTick=true; PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
	SetCollisionEnabled(ECollisionEnabled::NoCollision); SetCanEverAffectNavigation(false); SetCastShadow(false); SetIsReplicatedByDefault(false);
	SetVisibleInRayTracing(false); bUseAsOccluder=false;
}
void UGuLiCommanderRouteLineComponent::OnRegister()
{
	Super::OnRegister(); // Material and geometry submission belong to the final Slate layer.
}
void UGuLiCommanderRouteLineComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!LocalController.IsValid()) { SetComponentTickEnabled(false); return; }
	if (Presentation.IsValid()) AddTickPrerequisiteActor(Presentation.Get());
}
void UGuLiCommanderRouteLineComponent::InitializeForController(AGuLiCommanderPlayerController* Viewer, AGuLiCommanderPresentationActor* Source)
{ LocalController=Viewer; Presentation=Source; }
FPrimitiveSceneProxy* UGuLiCommanderRouteLineComponent::CreateSceneProxy()
{ return nullptr; } // Final Slate layer consumes the same bounded, authoritative endpoint cache.
void UGuLiCommanderRouteLineComponent::GetUsedMaterials(TArray<UMaterialInterface*>& Out,bool bGetDebugMaterials) const
{ if (ResolvedOverlayMaterial) Out.Add(ResolvedOverlayMaterial.Get()); }
FBoxSphereBounds UGuLiCommanderRouteLineComponent::CalcBounds(const FTransform& Transform) const
{ return FBoxSphereBounds(Transform.GetLocation(),FVector::ZeroVector,0); }
void UGuLiCommanderRouteLineComponent::SendRenderDynamicData_Concurrent()
{
	Super::SendRenderDynamicData_Concurrent();
}
void UGuLiCommanderRouteLineComponent::Unbind()
{
	if (NetSync.IsValid())
	{
		NetSync->OnSelectionChanged.RemoveAll(this); NetSync->OnMoveEndpointIdsChanged.RemoveAll(this);
		NetSync->OnLocalTaskSubmitted.RemoveAll(this); NetSync->OnCommandAckChanged.RemoveAll(this);
	}
	if (Roster.IsValid()) Roster->OnRosterDelta.RemoveAll(this);
#if !UE_BUILD_SHIPPING
	if (ResponseAuthority.IsValid()) ResponseAuthority->OnPositionApplied.Remove(ResponsePositionHandle);
	ResponseAuthority.Reset();ResponsePositionHandle.Reset();ResponseSamples.Reset();
#endif
	NetSync.Reset(); Roster.Reset();
}
void UGuLiCommanderRouteLineComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Presentation.IsValid()) RemoveTickPrerequisiteActor(Presentation.Get());
	Presentation.Reset(); Unbind(); ClearLines(); Super::EndPlay(Reason);
}
void UGuLiCommanderRouteLineComponent::ClearLines()
{
	UGuLiSceneUISourceRegistry::Notify(this);
	for (int32 I=0; I<Chunks.Num(); ++I) DirtyChunks.Add(I);
	Chunks.Reset(); Slots.Reset(); FreeSlots.Reset(); Queue.Reset(); Queued.Reset(); AwaitingPresentation.Reset(); QueueCursor=0;
	SubmittedBatches.Reset();
	LocalPreviews.Reset(); SubmittedPreviewCommands.Reset();
#if !UE_BUILD_SHIPPING
	ResponseSamples.Reset();
#endif
	MarkRenderDynamicDataDirty();
}
void UGuLiCommanderRouteLineComponent::Hide(FGuLiSoldierId Id)
{
	if (const int32* Found=Slots.Find(Id))
	{
		const int32 Slot=*Found; Slots.Remove(Id); ReleaseSlot(Slot);
	}
}
void UGuLiCommanderRouteLineComponent::ReleaseSlot(int32 Slot)
{
	FreeSlots.Add(Slot);
	Chunks[Slot/LinesPerChunk].Lines[Slot%LinesPerChunk].bVisible=false;
	DirtyChunks.Add(Slot/LinesPerChunk); MarkRenderDynamicDataDirty();
}
bool UGuLiCommanderRouteLineComponent::CanShow(FGuLiSoldierId Id) const
{
	const auto* E=NetSync.IsValid() ? NetSync->FindMoveEndpoint(Id) : nullptr;
	const auto* S=Roster.IsValid() ? Roster->FindSoldierState(Id) : nullptr;
	return Selected.Contains(Id) && E && S && S->IsAlive() && !S->bPhased && !S->bExternalActionsLocked
		&& E->ActiveOrderId && E->ActiveOrderId==S->ActiveOrderId;
}
void UGuLiCommanderRouteLineComponent::Enqueue(FGuLiSoldierId Id)
{
	if (!CanShow(Id)) { Hide(Id); Queued.Remove(Id); AwaitingPresentation.Remove(Id); return; }
	if (Slots.Contains(Id)) return;
	AwaitingPresentation.Remove(Id);
	if (!Queued.Contains(Id)) { Queued.Add(Id); Queue.Add(Id); }
}
void UGuLiCommanderRouteLineComponent::OnSelection(const FGuLiCommanderSelectionState& Selection)
{
	UGuLiSceneUISourceRegistry::Notify(this);
	TSet<FGuLiSoldierId> NewSelection;
	for (const auto& C : Selection.Cohorts) for (auto Id : C.MemberIds) NewSelection.Add(Id);
	for (auto Id : Selected) if (!NewSelection.Contains(Id)) { Hide(Id); Queued.Remove(Id); AwaitingPresentation.Remove(Id); }
	Selected=MoveTemp(NewSelection); for (auto Id : Selected) Enqueue(Id);
	for (auto It=LocalPreviews.CreateIterator(); It; ++It) if (!Selected.Contains(It.Key())) It.RemoveCurrent();
}
void UGuLiCommanderRouteLineComponent::OnLocalTask(const FGuLiUnitTaskCommand& Command,const FGuLiCommanderSelectionState& Selection)
{
	if (!IsImmediateFeedbackEnabled() && !GuLiMoveLatency::IsEnabled()) return;
	for (const auto& Cohort:Selection.Cohorts) for (int32 I=0;I<Cohort.MemberIds.Num();++I)
	{
		const auto Id=Cohort.MemberIds[I];
		if (Command.Kind!=EGuLiUnitTaskKind::Move || Command.Disposition==EGuLiTaskDisposition::Stop)
		{
			LocalPreviews.Remove(Id);
#if !UE_BUILD_SHIPPING
			ResponseSamples.Remove(Id);
#endif
			continue;
		}
		const auto* State=Roster.IsValid()?Roster->FindSoldierState(Id):nullptr;
		if (!Selected.Contains(Id) || !State || !State->IsAlive() || State->bPhased || State->bExternalActionsLocked) continue;
		const auto* Endpoint=NetSync.IsValid()?NetSync->FindMoveEndpoint(Id):nullptr;
		if (IsImmediateFeedbackEnabled())
		{
			auto& Preview=LocalPreviews.FindOrAdd(Id);
			Preview={Command.CommandId,Cohort.CohortId,Endpoint?Endpoint->ActiveOrderId:0u,I,
				FVector(Command.Target),GetWorld()->GetTimeSeconds()+2.5,false};
		}
#if !UE_BUILD_SHIPPING
		if (GuLiMoveLatency::IsEnabled() && ResponseSamples.Num()<2048)
		{
			FTransform Submitted;FGuLiSoldierNavigationDebug Movement;bool bLocalPose=false;
			if (Presentation.IsValid() && Presentation->TryGetSubmittedSoldierTransform(Id,Submitted,&bLocalPose)
				&& ResponseAuthority.IsValid() && ResponseAuthority->TryGetSoldierNavigationDebug(Id,Movement))
			{
				const auto Trace=GuLiMoveLatency::Context(NetSync.Get(),Command.CommandId);
				// This probe measures starting from rest. Continuing an earlier
				// displayed movement, or passive crowd pressure, is not a response.
				// Network poses use the protocol's XY grid; the local source uses
				// exact committed coordinates. Validate against the source actually submitted.
				FGuLiQuantizedSoldierPose InitialPose;
				const bool HasPose=bLocalPose || InitialPose.SetWorldLocationCentimeters(Movement.Location);
				const double PoseError=HasPose
					? FVector::Dist2D(Submitted.GetLocation(),bLocalPose ? Movement.Location
						: InitialPose.GetWorldLocationCentimeters()) : -1.;
				const bool Idle=Movement.ActiveOrderId==0 && Movement.Velocity.IsNearlyZero(1.f)
					&& HasPose && PoseError<=2.;
				GuLiMoveLatency::Record(Idle?TEXT("local-probe-ready"):TEXT("local-probe-ineligible"),Trace,1,
					*FString::Printf(TEXT("soldier=%u localSimulation=%d initialOrder=%u speed=%.3f poseErrorCm=%.3f localPose=%d"),
						Id.Value,Movement.bLocalSimulation?1:0,Movement.ActiveOrderId,Movement.Velocity.Size2D(),PoseError,bLocalPose?1:0));
				if (!Idle) {ResponseSamples.Remove(Id);continue;}
				ResponseSamples.Add(Id,{GuLiMoveLatency::Context(NetSync.Get(),Command.CommandId),Cohort.CohortId,I,
					Movement.Location,Submitted.GetLocation(),FPlatformTime::Seconds()+5.,false});
			}
		}
#endif
	}
	UGuLiSceneUISourceRegistry::Notify(this);
}
void UGuLiCommanderRouteLineComponent::OnCommandAck(const FGuLiCommandAck& Ack)
{
	if (Ack.CommandKind!=EGuLiCommandKind::Move) return;
	auto IsAccepted=[&](FGuLiControlCohortId Cohort,int32 MemberIndex)
	{
		if (!Ack.IsAccepted()) return false;
		if (Ack.CohortResults.IsEmpty()) return true;
		const auto* Result=Ack.CohortResults.FindByPredicate([&](const auto& R){return R.CohortId==Cohort;});
		return Result && (Result->MemberCount?Result->IsMemberAccepted(uint8(MemberIndex)):Result->Result==EGuLiCommandAckResult::Accepted);
	};
	for (auto It=LocalPreviews.CreateIterator();It;++It)
	{
		auto& Preview=It.Value();if (Preview.Command!=Ack.ClientCommandId) continue;
		const bool Accepted=IsAccepted(Preview.Cohort,Preview.MemberIndex);
		if (!Accepted) It.RemoveCurrent();else Preview.bAccepted=true;
	}
#if !UE_BUILD_SHIPPING
	for (auto It=ResponseSamples.CreateIterator();It;++It)
		if (It.Value().Trace.Command==Ack.ClientCommandId && !IsAccepted(It.Value().Cohort,It.Value().MemberIndex)) It.RemoveCurrent();
#endif
	UGuLiSceneUISourceRegistry::Notify(this);
}
void UGuLiCommanderRouteLineComponent::OnEndpoints(TConstArrayView<FGuLiSoldierId> Ids,bool bReset)
{
	UGuLiSceneUISourceRegistry::Notify(this);
	if (bReset) { ClearLines(); for (auto Id : Selected) Enqueue(Id); }
	else for (auto Id : Ids)
	{
		// A new order invalidates the old route immediately, even if the new draw is queued.
		const auto* Slot=Slots.Find(Id); const auto* Endpoint=NetSync.IsValid() ? NetSync->FindMoveEndpoint(Id) : nullptr;
		if (Slot && (!Endpoint || Chunks[*Slot/LinesPerChunk].Lines[*Slot%LinesPerChunk].Order!=Endpoint->ActiveOrderId
			|| Chunks[*Slot/LinesPerChunk].Lines[*Slot%LinesPerChunk].Revision!=Endpoint->Revision)) Hide(Id);
		Enqueue(Id);
	}
}
void UGuLiCommanderRouteLineComponent::OnRoster(const FGuLiSoldierRosterDelta& Delta)
{
	UGuLiSceneUISourceRegistry::Notify(this);
	if (Delta.bReset) ClearLines();
	for (auto Id : Delta.Removed) { Hide(Id); Queued.Remove(Id); AwaitingPresentation.Remove(Id); LocalPreviews.Remove(Id); }
	for (auto Id : Delta.Added) Enqueue(Id);
	for (const auto& P : Delta.Changed) if (EnumHasAnyFlags(P.Value,EGuLiSoldierStateChange::Order|EGuLiSoldierStateChange::Life|EGuLiSoldierStateChange::Phase|EGuLiSoldierStateChange::Team)) Enqueue(P.Key);
}
void UGuLiCommanderRouteLineComponent::TickComponent(float Dt,ELevelTick Tick,FActorComponentTickFunction* Function)
{
	Super::TickComponent(Dt,Tick,Function);
	auto* PC=LocalController.Get();
	auto* Sync=PC && PC->IsLocalController() ? PC->GetCommanderNetSyncComponent() : nullptr;
	const bool Active=Sync && PC->IsCommanderViewActive() && Sync->IsSoldierStreamReady();
	if (!Active) { if (bWasActive) { ClearLines(); Selected.Reset(); } bWasActive=false; return; }
	if (NetSync.Get()!=Sync)
	{
		Unbind(); ClearLines(); NetSync=Sync;
		Sync->OnSelectionChanged.AddUObject(this,&UGuLiCommanderRouteLineComponent::OnSelection);
		Sync->OnMoveEndpointIdsChanged.AddUObject(this,&UGuLiCommanderRouteLineComponent::OnEndpoints);
		Sync->OnLocalTaskSubmitted.AddUObject(this,&UGuLiCommanderRouteLineComponent::OnLocalTask);
		Sync->OnCommandAckChanged.AddUObject(this,&UGuLiCommanderRouteLineComponent::OnCommandAck);
		for (TActorIterator<AGuLiSoldierStateReplicator> It(GetWorld()); It; ++It) { Roster=*It; break; }
		if (Roster.IsValid()) Roster->OnRosterDelta.AddUObject(this,&UGuLiCommanderRouteLineComponent::OnRoster);
#if !UE_BUILD_SHIPPING
		ResponseAuthority=GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
		if (ResponseAuthority.IsValid()) ResponsePositionHandle=ResponseAuthority->OnPositionApplied.AddUObject(this,&UGuLiCommanderRouteLineComponent::OnPositionApplied);
#endif
		bWasActive=false;
	}
	if (!bWasActive) OnSelection(Sync->GetSelectionState());
	bWasActive=true;
#if !UE_BUILD_SHIPPING
	RecordSubmittedMovement();
#endif
	for (auto It=LocalPreviews.CreateIterator();It;++It)
	{
		const auto* State=Roster.IsValid()?Roster->FindSoldierState(It.Key()):nullptr;
		const auto* Endpoint=Sync->FindMoveEndpoint(It.Key());const auto& Preview=It.Value();
		if (!IsImmediateFeedbackEnabled() || !Selected.Contains(It.Key()) || !State || !State->IsAlive()
			|| State->bPhased || State->bExternalActionsLocked || GetWorld()->GetTimeSeconds()>=Preview.Expires
			|| (Preview.bAccepted && CanShow(It.Key()) && Endpoint && Endpoint->ActiveOrderId!=Preview.PreviousOrder))
			It.RemoveCurrent();
	}
	for (auto It=SubmittedPreviewCommands.CreateIterator();It;++It)
	{
		const uint32 Command=*It;
		bool ActivePreview=false;
		for (const auto& Pair:LocalPreviews) if (Pair.Value.Command==Command) {ActivePreview=true;break;}
		if (!ActivePreview) It.RemoveCurrent();
	}
	const double Start=FPlatformTime::Seconds(); int32 Work=0, Refreshed=0;
	for (auto It=AwaitingPresentation.CreateIterator(); It; ++It)
	{
		const auto Id=*It;
		if (!CanShow(Id)) { It.RemoveCurrent(); continue; }
		FVector Center;
		if (Presentation.IsValid() && Presentation->TryGetPresentedSoldierModelCenter(Id,Center))
		{ It.RemoveCurrent(); Enqueue(Id); }
	}
	// Existing lines follow the same-frame visual pose, independently of the addition queue budget.
	for (auto It=Slots.CreateIterator(); It; ++It)
	{
		const auto Id=It.Key(); const int32 Slot=It.Value();
		if (!CanShow(Id))
		{ It.RemoveCurrent(); ReleaseSlot(Slot); Queued.Remove(Id); continue; }
		const auto& E=*Sync->FindMoveEndpoint(Id);
		auto& L=Chunks[Slot/LinesPerChunk].Lines[Slot%LinesPerChunk];
		if (L.Order!=E.ActiveOrderId || L.Revision!=E.Revision)
		{ It.RemoveCurrent(); ReleaseSlot(Slot); Enqueue(Id); continue; }
		FVector Center;
		if (!Presentation.IsValid() || !Presentation->TryGetPresentedSoldierModelCenter(Id,Center))
		{ It.RemoveCurrent(); ReleaseSlot(Slot); AwaitingPresentation.Add(Id); continue; }
		if (L.Start!=Center || L.End!=E.FinalDestination)
		{ L.Start=Center; L.End=E.FinalDestination; DirtyChunks.Add(Slot/LinesPerChunk); ++Refreshed; }
	}
	const double AdditionStart=FPlatformTime::Seconds();
	while (QueueCursor<Queue.Num() && Work<FMath::Max(1,CVarRouteLinesPerFrame.GetValueOnGameThread())
		&& FPlatformTime::Seconds()-AdditionStart<FMath::Max(.01f,CVarRouteLineBudgetMs.GetValueOnGameThread())*.001)
	{
		const auto Id=Queue[QueueCursor++]; if (!Queued.Remove(Id)) continue;
		++Work; if (!CanShow(Id)) { Hide(Id); continue; }
		const auto& E=*Sync->FindMoveEndpoint(Id);
		FVector Center;
		if (!Presentation.IsValid() || !Presentation->TryGetPresentedSoldierModelCenter(Id,Center))
		{ Hide(Id); AwaitingPresentation.Add(Id); continue; }
		int32 Slot;
		if (const auto* Previous=Slots.Find(Id)) Slot=*Previous;
		else
		{
			if (FreeSlots.IsEmpty()) { const int32 Base=Chunks.AddDefaulted()*LinesPerChunk; for (int32 I=LinesPerChunk-1; I>=0; --I) FreeSlots.Add(Base+I); }
			Slot=FreeSlots.Pop(EAllowShrinking::No); Slots.Add(Id,Slot);
		}
		auto& L=Chunks[Slot/LinesPerChunk].Lines[Slot%LinesPerChunk];
		L.Start=Center; L.End=E.FinalDestination; L.Order=E.ActiveOrderId; L.Revision=E.Revision;
		L.bVisible=true; L.Trace=GuLiMoveLatency::IsEnabled() ? GuLiMoveLatency::Context(Sync,0,E.ActiveOrderId) : GuLiMoveLatency::FContext{};
		DirtyChunks.Add(Slot/LinesPerChunk);
	}
	if (QueueCursor==Queue.Num()) { Queue.Reset(); QueueCursor=0; }
	// Slate reads the cache on this frame; no render-thread payload is retained.
	DirtyChunks.Reset();
	if (CVarRouteLineDiagnostics.GetValueOnGameThread() && Start>=NextDiagnostic)
	{
		NextDiagnostic=Start+1;
		UE_LOG(LogGuLiStrike,Display,TEXT("MassRouteLines visible=%d queued=%d awaitingPose=%d work=%d refreshed=%d dirtyChunks=%d ms=%.3f"),Slots.Num(),Queued.Num(),AwaitingPresentation.Num(),Work,Refreshed,DirtyChunks.Num(),(FPlatformTime::Seconds()-Start)*1000);
	}
}

int32 UGuLiCommanderRouteLineComponent::CountInvalidDisplayedLines() const
{
	int32 Count=0;
	for (const auto& Pair : Slots)
	{
		const auto& Line=Chunks[Pair.Value/LinesPerChunk].Lines[Pair.Value%LinesPerChunk];
		const auto* E=NetSync.IsValid() ? NetSync->FindMoveEndpoint(Pair.Key) : nullptr;
		FVector Center;
		Count+=!Line.bVisible || !CanShow(Pair.Key) || !E || E->ActiveOrderId!=Line.Order
			|| E->Revision!=Line.Revision || !E->FinalDestination.Equals(Line.End,.01)
			|| !Presentation.IsValid() || !Presentation->TryGetPresentedSoldierModelCenter(Pair.Key,Center)
			|| !Center.Equals(Line.Start,.01);
	}
	return Count;
}

void UGuLiCommanderRouteLineComponent::GatherSceneUILines(TArray<FGuLiSceneUILine>& Out) const
{
	Out.Reset(Slots.Num()+LocalPreviews.Num());
	for (const auto& Pair : Slots)
	{
		const auto& Line = Chunks[Pair.Value / LinesPerChunk].Lines[Pair.Value % LinesPerChunk];
		if (Line.bVisible && CanShow(Pair.Key) && !LocalPreviews.Contains(Pair.Key)) Out.Add({Line.Start, Line.End, Line.Order});
	}
	for (const auto& Pair:LocalPreviews)
	{
		FVector Center;
		if (Selected.Contains(Pair.Key) && Presentation.IsValid()
			&& Presentation->TryGetPresentedSoldierModelCenter(Pair.Key,Center))
			Out.Add({Center,Pair.Value.End,0,Pair.Value.Command});
	}
}
#if !UE_BUILD_SHIPPING
void UGuLiCommanderRouteLineComponent::OnPositionApplied(FGuLiSoldierId Soldier,uint32 Command,const FVector& Position,double At)
{
	auto* Sample=ResponseSamples.Find(Soldier);
	if (!Sample || Sample->bApplied || Sample->Trace.Command!=Command) return;
	const double DistanceSquared=FVector::DistSquared2D(Position,Sample->AuthorityStart);
	if (!Sample->bFirstApplied && DistanceSquared>1.)
	{
		Sample->bFirstApplied=true;
		GuLiMoveLatency::Record(TEXT("local-first-mass-write"),Sample->Trace,1,
			*FString::Printf(TEXT("soldier=%u displacementCm=%.3f thresholdCm=1"),Soldier.Value,FMath::Sqrt(DistanceSquared)),At);
	}
	if (DistanceSquared<=4.) return;
	Sample->bApplied=true;
	GuLiMoveLatency::Record(TEXT("local-mass-write"),Sample->Trace,1,*FString::Printf(TEXT("soldier=%u"),Soldier.Value),At);
}
void UGuLiCommanderRouteLineComponent::RecordSubmittedMovement()
{
	if (!GuLiMoveLatency::IsEnabled()) {ResponseSamples.Reset();return;}
	for (auto It=ResponseSamples.CreateIterator();It;++It)
	{
		auto& Sample=It.Value();FTransform Submitted;
		const bool HasTransform=Presentation.IsValid() && Presentation->TryGetSubmittedSoldierTransform(It.Key(),Submitted);
		const double DistanceSquared=HasTransform ? FVector::DistSquared2D(Submitted.GetLocation(),Sample.ModelStart) : 0.;
		if (Sample.bFirstApplied && !Sample.bFirstModelSubmitted && DistanceSquared>1.)
		{
			Sample.bFirstModelSubmitted=true;
			GuLiMoveLatency::Record(TEXT("local-first-model-submit"),Sample.Trace,1,
				*FString::Printf(TEXT("soldier=%u displacementCm=%.3f thresholdCm=1 ISM slot; not display scanout"),It.Key().Value,FMath::Sqrt(DistanceSquared)));
		}
		if (Sample.bApplied && DistanceSquared>4.)
		{
			GuLiMoveLatency::Record(TEXT("local-model-submit"),Sample.Trace,1,
				*FString::Printf(TEXT("soldier=%u ISM slot after presentation tick; not display scanout"),It.Key().Value));
			It.RemoveCurrent();
		}
		else if (FPlatformTime::Seconds()>=Sample.Deadline)
		{
			GuLiMoveLatency::Record(TEXT("local-model-unobserved"),Sample.Trace,1,*FString::Printf(TEXT("soldier=%u"),It.Key().Value));
			It.RemoveCurrent();
		}
	}
}
#endif

void UGuLiCommanderRouteLineComponent::RecordSceneUISubmission(TConstArrayView<FGuLiSceneUILine> VisibleLines) const
{
	if (!GuLiMoveLatency::IsEnabled()) return;
	TSet<uint64> ActiveBatches;
	for (const auto& Line:VisibleLines)
	{
		if (Line.PreviewCommand && !SubmittedPreviewCommands.Contains(Line.PreviewCommand))
		{
			SubmittedPreviewCommands.Add(Line.PreviewCommand);
			GuLiMoveLatency::Record(TEXT("local-green-submit"),GuLiMoveLatency::Context(NetSync.Get(),Line.PreviewCommand),1,TEXT("clipped Slate geometry; not display scanout"));
		}
		if (!Line.Order) continue;
		const auto Trace=GuLiMoveLatency::Context(NetSync.Get(),0,Line.Order);
		const uint64 Key=(uint64(Trace.Epoch)<<32)|Trace.Batch;ActiveBatches.Add(Key);
		if (!SubmittedBatches.Contains(Key))
		{ SubmittedBatches.Add(Key); GuLiMoveLatency::Record(TEXT("render-submit"),Trace,1); }
	}
	for (auto It=SubmittedBatches.CreateIterator(); It; ++It) if (!ActiveBatches.Contains(*It)) It.RemoveCurrent();
}
