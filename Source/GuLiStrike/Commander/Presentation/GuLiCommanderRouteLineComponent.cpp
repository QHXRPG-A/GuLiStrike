#include "Commander/Presentation/GuLiCommanderRouteLineComponent.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
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
}

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
	if (NetSync.IsValid()) { NetSync->OnSelectionChanged.RemoveAll(this); NetSync->OnMoveEndpointIdsChanged.RemoveAll(this); }
	if (Roster.IsValid()) Roster->OnRosterDelta.RemoveAll(this);
	NetSync.Reset(); Roster.Reset();
}
void UGuLiCommanderRouteLineComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Presentation.IsValid()) RemoveTickPrerequisiteActor(Presentation.Get());
	Presentation.Reset(); Unbind(); ClearLines(); Super::EndPlay(Reason);
}
void UGuLiCommanderRouteLineComponent::ClearLines()
{
	for (int32 I=0; I<Chunks.Num(); ++I) DirtyChunks.Add(I);
	Chunks.Reset(); Slots.Reset(); FreeSlots.Reset(); Queue.Reset(); Queued.Reset(); AwaitingPresentation.Reset(); QueueCursor=0;
	SubmittedBatches.Reset();
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
	TSet<FGuLiSoldierId> NewSelection;
	for (const auto& C : Selection.Cohorts) for (auto Id : C.MemberIds) NewSelection.Add(Id);
	for (auto Id : Selected) if (!NewSelection.Contains(Id)) { Hide(Id); Queued.Remove(Id); AwaitingPresentation.Remove(Id); }
	Selected=MoveTemp(NewSelection); for (auto Id : Selected) Enqueue(Id);
}
void UGuLiCommanderRouteLineComponent::OnEndpoints(TConstArrayView<FGuLiSoldierId> Ids,bool bReset)
{
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
	if (Delta.bReset) ClearLines();
	for (auto Id : Delta.Removed) { Hide(Id); Queued.Remove(Id); AwaitingPresentation.Remove(Id); }
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
		for (TActorIterator<AGuLiSoldierStateReplicator> It(GetWorld()); It; ++It) { Roster=*It; break; }
		if (Roster.IsValid()) Roster->OnRosterDelta.AddUObject(this,&UGuLiCommanderRouteLineComponent::OnRoster);
		bWasActive=false;
	}
	if (!bWasActive) OnSelection(Sync->GetSelectionState());
	bWasActive=true;
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
	Out.Reset(Slots.Num());
	for (const auto& Pair : Slots)
	{
		const auto& Line = Chunks[Pair.Value / LinesPerChunk].Lines[Pair.Value % LinesPerChunk];
		if (Line.bVisible && CanShow(Pair.Key)) Out.Add({Line.Start, Line.End});
	}
}

void UGuLiCommanderRouteLineComponent::RecordSceneUISubmission() const
{
	if (!GuLiMoveLatency::IsEnabled()) return;
	TSet<uint64> ActiveBatches;
	for (const auto& Pair : Slots)
	{
		const auto& Line=Chunks[Pair.Value/LinesPerChunk].Lines[Pair.Value%LinesPerChunk];
		const uint64 Key=(uint64(Line.Trace.Epoch)<<32)|Line.Trace.Batch;
		if (Line.bVisible && Line.Trace.Batch) ActiveBatches.Add(Key);
		if (Line.bVisible && Line.Trace.Batch && !SubmittedBatches.Contains(Key))
		{ SubmittedBatches.Add(Key); GuLiMoveLatency::Record(TEXT("render-submit"),Line.Trace,1); }
	}
	for (auto It=SubmittedBatches.CreateIterator(); It; ++It) if (!ActiveBatches.Contains(*It)) It.RemoveCurrent();
}
