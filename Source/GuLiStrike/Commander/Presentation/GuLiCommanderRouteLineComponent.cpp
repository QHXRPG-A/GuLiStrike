#include "Commander/Presentation/GuLiCommanderRouteLineComponent.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
#include "DynamicMeshBuilder.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "LocalVertexFactory.h"
#include "Materials/Material.h"
#include "Materials/MaterialRenderProxy.h"
#include "MaterialShared.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveUniformShaderParametersBuilder.h"
#include "StaticMeshResources.h"
#include "SceneManagement.h"
#include "HAL/IConsoleManager.h"
#include "GuLiStrike.h"
#include "Misc/ScopeLock.h"

namespace
{
TAutoConsoleVariable<int32> CVarRouteLinesPerFrame(TEXT("guli.Commander.RouteLinesPerFrame"),128,TEXT("Maximum selected route additions per render frame; visible anchors refresh every frame."));
TAutoConsoleVariable<float> CVarRouteLineBudgetMs(TEXT("guli.Commander.RouteLineBudgetMs"),.5f,TEXT("CPU route addition budget in milliseconds; visible anchors and invalidations update every frame."));
TAutoConsoleVariable<int32> CVarRouteLineDiagnostics(TEXT("guli.Commander.RouteLineDiagnostics"),0,TEXT("Log route backlog and changed chunk counts once per second."));
struct FLineChunkUpdate { int32 Index=0; TArray<FVector3f> Positions; TArray<GuLiMoveLatency::FContext> Traces; };
}

class FGuLiRouteLineSceneProxy final : public FPrimitiveSceneProxy
{
	struct FChunkResource
	{
		FStaticMeshVertexBuffers Vertices;
		FDynamicMeshIndexBuffer32 Indices;
		FLocalVertexFactory Factory;
		TArray<GuLiMoveLatency::FContext> Traces;
		explicit FChunkResource(ERHIFeatureLevel::Type Level) : Factory(Level,"GuLiRouteLines") {}
		~FChunkResource()
		{
			Factory.ReleaseResource(); Indices.ReleaseResource();
			Vertices.PositionVertexBuffer.ReleaseResource(); Vertices.StaticMeshVertexBuffer.ReleaseResource(); Vertices.ColorVertexBuffer.ReleaseResource();
		}
	};
	TMap<int32,TUniquePtr<FChunkResource>> Resources;
	const FMaterialRenderProxy* Material;
	FMaterialRelevance MaterialRelevance;
	mutable TSet<uint64> SubmittedBatches;
	mutable FCriticalSection TraceMutex;
public:
	explicit FGuLiRouteLineSceneProxy(const UGuLiCommanderRouteLineComponent* Component)
		: FPrimitiveSceneProxy(Component)
	{
		const UMaterialInterface* Source=Component->ResolvedOverlayMaterial.Get();
		Material=Source->GetRenderProxy(); MaterialRelevance=Source->GetRelevance_Concurrent(GetScene().GetShaderPlatform());
		TArray<FLineChunkUpdate> Initial;
		for (int32 I=0; I<Component->Chunks.Num(); ++I)
		{
			auto& U=Initial.AddDefaulted_GetRef(); U.Index=I;
			for (const auto& Line : Component->Chunks[I].Lines) if (Line.bVisible)
			{ U.Positions.Add(FVector3f(Component->GetComponentTransform().InverseTransformPosition(Line.Start))); U.Positions.Add(FVector3f(Component->GetComponentTransform().InverseTransformPosition(Line.End))); if (Line.Trace.Batch) U.Traces.Add(Line.Trace); }
		}
		ENQUEUE_RENDER_COMMAND(GuLiInitRouteLines)([this,Initial=MoveTemp(Initial)](FRHICommandListImmediate& RHICmdList) mutable { Update(RHICmdList,MoveTemp(Initial)); });
	}
	void Update(FRHICommandListBase& RHICmdList,TArray<FLineChunkUpdate>&& Updates)
	{
		check(IsInRenderingThread());
		for (auto& U : Updates)
		{
			Resources.Remove(U.Index);
			if (U.Positions.IsEmpty()) continue;
			check(U.Positions.Num()<=256 && U.Positions.Num()%2==0);
			auto R=MakeUnique<FChunkResource>(GetScene().GetFeatureLevel());
			R->Traces=MoveTemp(U.Traces);
			TArray<FDynamicMeshVertex> V; V.Reserve(U.Positions.Num());
			for (int32 I=0; I<U.Positions.Num(); ++I)
			{
				FDynamicMeshVertex Vertex; Vertex.Position=U.Positions[I]; Vertex.Color=FColor::Green;
				Vertex.SetTangents(FVector3f(1,0,0),FVector3f(0,1,0),FVector3f(0,0,1)); Vertex.TextureCoordinate[0]=FVector2f::ZeroVector;
				V.Add(Vertex); R->Indices.Indices.Add(I);
			}
			R->Vertices.InitFromDynamicVertex(RHICmdList,&R->Factory,V);
			R->Indices.InitResource(RHICmdList); // InitFromDynamicVertex initializes vertex buffers and factory on this command list.
			Resources.Add(U.Index,MoveTemp(R));
		}
	}
	virtual SIZE_T GetTypeHash() const override { static size_t Type; return reinterpret_cast<SIZE_T>(&Type); }
	virtual uint32 GetMemoryFootprint() const override { return sizeof(*this)+GetAllocatedSize(); }
	virtual bool CanBeOccluded() const override { return false; }
	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		FPrimitiveViewRelevance R; R.bDrawRelevance=IsShown(View); R.bDynamicRelevance=true; R.bRenderInMainPass=ShouldRenderInMainPass();
		MaterialRelevance.SetPrimitiveViewRelevance(R); return R;
	}
	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views,const FSceneViewFamily& Family,uint32 VisibilityMap,FMeshElementCollector& Collector) const override
	{
		auto* Green=new FColoredMaterialRenderProxy(Material,FLinearColor(.08f,.94f,.20f)); Collector.RegisterOneFrameMaterialProxy(Green);
		for (int32 V=0; V<Views.Num(); ++V) if (VisibilityMap&(1u<<V))
		{
			auto& Uniform=Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
			FPrimitiveUniformShaderParametersBuilder Builder; BuildUniformShaderParameters(Builder); Uniform.Set(Collector.GetRHICommandList(),Builder);
			for (const auto& Pair : Resources)
			{
				const auto& R=*Pair.Value; auto& Mesh=Collector.AllocateMesh(); auto& E=Mesh.Elements[0];
				Mesh.VertexFactory=&R.Factory; Mesh.MaterialRenderProxy=Green; Mesh.Type=PT_LineList;
				Mesh.DepthPriorityGroup=SDPG_Foreground; Mesh.bCanApplyViewModeOverrides=false; Mesh.bDisableBackfaceCulling=true;
				E.IndexBuffer=&R.Indices; E.FirstIndex=0; E.NumPrimitives=R.Indices.Indices.Num()/2;
				E.MinVertexIndex=0; E.MaxVertexIndex=R.Indices.Indices.Num()-1; E.PrimitiveUniformBufferResource=&Uniform.UniformBuffer;
				Collector.AddMesh(V,Mesh);
				if (GuLiMoveLatency::IsEnabled()) for (const auto& Trace : R.Traces)
				{
					FScopeLock Lock(&TraceMutex);
					const uint64 Key=(uint64(Trace.Epoch)<<32)|Trace.Batch;
					if (!SubmittedBatches.Contains(Key))
					{ SubmittedBatches.Add(Key); GuLiMoveLatency::Record(TEXT("render-submit"),Trace,1); }
				}
			}
		}
	}
};

UGuLiCommanderRouteLineComponent::UGuLiCommanderRouteLineComponent()
{
	PrimaryComponentTick.bCanEverTick=true; PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
	SetCollisionEnabled(ECollisionEnabled::NoCollision); SetCanEverAffectNavigation(false); SetCastShadow(false); SetIsReplicatedByDefault(false);
	SetVisibleInRayTracing(false); bUseAsOccluder=false;
}
void UGuLiCommanderRouteLineComponent::OnRegister()
{
	ResolvedOverlayMaterial=nullptr;
	if (GetNetMode()!=NM_DedicatedServer)
	{
		ResolvedOverlayMaterial=OverlayMaterial.LoadSynchronous();
		const UMaterial* Base=ResolvedOverlayMaterial ? ResolvedOverlayMaterial->GetMaterial() : nullptr;
		if (!Base || !IsTranslucentBlendMode(Base->BlendMode) || !Base->bDisableDepthTest)
		{
			UE_LOG(LogGuLiStrike,Error,TEXT("Commander route overlay requires a translucent material with depth testing disabled: %s"),*OverlayMaterial.ToSoftObjectPath().ToString());
			ResolvedOverlayMaterial=nullptr;
		}
	}
	Super::OnRegister();
}
void UGuLiCommanderRouteLineComponent::BeginPlay()
{
	Super::BeginPlay();
	Presentation=Cast<AGuLiCommanderPresentationActor>(GetOwner());
	if (Presentation.IsValid()) AddTickPrerequisiteActor(Presentation.Get());
}
FPrimitiveSceneProxy* UGuLiCommanderRouteLineComponent::CreateSceneProxy()
{ return GetNetMode()==NM_DedicatedServer || !ResolvedOverlayMaterial ? nullptr : new FGuLiRouteLineSceneProxy(this); }
void UGuLiCommanderRouteLineComponent::GetUsedMaterials(TArray<UMaterialInterface*>& Out,bool bGetDebugMaterials) const
{ if (ResolvedOverlayMaterial) Out.Add(ResolvedOverlayMaterial.Get()); }
FBoxSphereBounds UGuLiCommanderRouteLineComponent::CalcBounds(const FTransform& Transform) const
{ return FBoxSphereBounds(FVector::ZeroVector,FVector(HALF_WORLD_MAX),HALF_WORLD_MAX); }
void UGuLiCommanderRouteLineComponent::SendRenderDynamicData_Concurrent()
{
	Super::SendRenderDynamicData_Concurrent();
	if (!SceneProxy || DirtyChunks.IsEmpty()) return;
	TArray<FLineChunkUpdate> Updates;
	for (int32 I : DirtyChunks)
	{
		auto& U=Updates.AddDefaulted_GetRef(); U.Index=I;
		if (Chunks.IsValidIndex(I)) for (const auto& L : Chunks[I].Lines) if (L.bVisible)
		{ U.Positions.Add(FVector3f(GetComponentTransform().InverseTransformPosition(L.Start))); U.Positions.Add(FVector3f(GetComponentTransform().InverseTransformPosition(L.End))); if (L.Trace.Batch) U.Traces.Add(L.Trace); }
	}
	DirtyChunks.Reset(); auto* Proxy=static_cast<FGuLiRouteLineSceneProxy*>(SceneProxy);
	// Immutable payload; the render queue orders updates before proxy destruction. No UObject is captured.
	ENQUEUE_RENDER_COMMAND(GuLiUpdateRouteLines)([Proxy,Updates=MoveTemp(Updates)](FRHICommandListImmediate& RHICmdList) mutable { Proxy->Update(RHICmdList,MoveTemp(Updates)); });
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
	auto* PC=Cast<AGuLiCommanderPlayerController>(GetWorld()->GetFirstPlayerController());
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
	if (!DirtyChunks.IsEmpty()) MarkRenderDynamicDataDirty();
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
