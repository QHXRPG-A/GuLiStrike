#include "Gameplay/CombatEffects/GuLiImpactBatchPresentation.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectDefinition.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "Gameplay/Vfx/GuLiClientPresentationPolicy.h"
#include "Commander/Presentation/GuLiCommanderLODSubsystem.h"
#include "Commander/Presentation/GuLiCommanderOverviewSubsystem.h"
#include "Gameplay/Performance/GuLiPerformanceSubsystem.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraDataChannel.h"
#include "NiagaraDataChannelAsset.h"
#include "NiagaraDataChannelAccessContext.h"
#include "NiagaraDataChannelAccessor.h"
#include "NiagaraDataChannelFunctionLibrary.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

static TAutoConsoleVariable<int32> GImpactBatchMode(TEXT("gs.Impacts.BatchMode"),2,
	TEXT("0: immediate individual; 1: next-frame individual; 2: previous-frame NDC batch, with next-frame individual fallback."));

UWorld* UGuLiImpactBatchPresentation::GetWorld() const { return GetOuter() ? GetOuter()->GetWorld() : nullptr; }

void UGuLiImpactBatchPresentation::Prepare(const UGuLiCombatEffectCatalog* Catalog)
{
	if (bPrepared || !Catalog || !GetWorld() || GetWorld()->GetNetMode()==NM_DedicatedServer) return;
	bPrepared=true; Channel=Catalog->ImpactChannel.LoadSynchronous();
	// Resolve all tiers outside the timed publication path. The World registry holds strong references.
	if (auto* Registry=GetWorld()->GetSubsystem<UGuLiVfxRegistrySubsystem>())
		for (int32 I=0;I<3;++I)
		{
			Registry->LoadNiagaraForLOD(Catalog->MachineGunImpact.VfxId,EGuLiCommanderLODLevel(I));
			Registry->LoadNiagaraForLOD(Catalog->MachineGunImpact.VfxId,EGuLiCommanderLODLevel(I),true);
		}
}

bool UGuLiImpactBatchPresentation::GetEnvelope(UNiagaraSystem* System, const FGuLiImpactEvent& Event, FBox& Bounds)
{
	if (!System) return false;
	const auto& Params=System->GetExposedParameters();
	const FNiagaraVariable Var(FNiagaraTypeDefinition::GetVec3Def(),TEXT("User.GuLiPreSpawnBoundsExtent"));
	if (Params.IndexOf(Var)==INDEX_NONE) return false;
	const FVector Extent(Params.GetParameterValue<FVector3f>(Var));
	if (Extent.ContainsNaN() || Extent.GetMin()<=0) return false;
	Bounds=FBox(-Extent,Extent).TransformBy(FTransform(Event.Rotation,Event.Position,Event.Scale));
	return Bounds.IsValid!=0;
}

bool UGuLiImpactBatchPresentation::Enqueue(const FGuLiImpactEvent& Event)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiImpacts_Accept);
	FGuLiPerformanceScope Timing(GetWorld(),TEXT("Impact.AcceptMs"));
	if (!bEnabled || !GetWorld() || !Event.Identity.Effect.IsValid() || Event.Position.ContainsNaN() || !UGuLiVfxRegistrySubsystem::IsValidScale(Event.Scale)) return false;
	WorldNow=GetWorld()->GetTimeSeconds();
	if (Seen.Contains(Event.Identity)) { ++Duplicates; return false; }
	Seen.Add(Event.Identity,WorldNow+4); SeenOrder.Emplace(Event.Identity,WorldNow+4);
	auto* Registry=GetWorld()->GetSubsystem<UGuLiVfxRegistrySubsystem>(); if (!Registry) return false;
	UNiagaraSystem* Full=Registry->LoadNiagaraForLOD(Event.VfxId,EGuLiCommanderLODLevel::Full,false,false);
	FBox Bounds;
	const bool bKnown=GetEnvelope(Full,Event,Bounds);
	FGuLiCommanderLODDecision Decision; Decision.bVisible=true; Decision.TargetLevel=EGuLiCommanderLODLevel::Full;
	if (bKnown)
	{
		FGuLiCommanderLODQuery Query; Query.Bounds=Bounds;
		if (auto* LOD=GetWorld()->GetSubsystem<UGuLiCommanderLODSubsystem>()) Decision=LOD->EvaluateWorldEffectBounds(Query,20000);
	}
	else ++UnknownBounds;
	if (!Decision.bVisible) { ++Culled; return false; }
	UNiagaraSystem* Single=Registry->LoadNiagaraForLOD(Event.VfxId,Decision.TargetLevel,false,false);
	if (!Single) return false;
	const auto Handle=Pool.Acquire(); Slots.SetNum(Pool.Capacity());
	auto& Slot=Slots[Handle.Index]; Slot={}; Slot.Handle=Handle; Slot.Event=Event;
	Slot.Bounds=Bounds; Slot.bKnownBounds=bKnown; Slot.SingleSystem=Single; Slot.AcceptedFrame=GFrameCounter;
	Slot.Mode=FMath::Clamp(GImpactBatchMode.GetValueOnGameThread(),0,2);
	if (Slot.Mode==2)
	{
		Slot.BatchSystem=Registry->LoadNiagaraForLOD(Event.VfxId,Decision.TargetLevel,true,false);
		const float BatchMaximumLifetime=UGuLiVfxRegistrySubsystem::NiagaraFloat(
			Slot.BatchSystem,TEXT("User.GuLiImpactMaximumLifetime"),0);
		const bool bCertified=Channel && Channel->Get() && Slot.BatchSystem && bKnown
			&& UGuLiVfxRegistrySubsystem::NiagaraFloat(Slot.BatchSystem,TEXT("User.GuLiImpactInputVersion"),0)==1
			&& FMath::IsFinite(BatchMaximumLifetime) && BatchMaximumLifetime>=1.25f;
		if (!bCertified) { Slot.BatchSystem=nullptr; Slot.Mode=1; ++Fallbacks; }
		else Slot.Event.Lifetime=FMath::Min(Slot.Event.Lifetime,BatchMaximumLifetime);
	}
	++Accepted;
	if (Slot.Mode==0)
	{
		Slot.PublishedFrame=GFrameCounter; Slot.BornAt=WorldNow; Slot.ExpiresAt=WorldNow+Event.Lifetime;
		if (!SpawnSingle(Slot)) { RetireSlot(Handle.Index); return false; }
	}
	else Pending.Add(Handle.Index);
	return true;
}

bool UGuLiImpactBatchPresentation::SpawnSingle(FGuLiImpactSlot& Slot)
{
	Slot.SingleComponent=UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(),Slot.SingleSystem,Slot.Event.Position,
		Slot.Event.Rotation.Rotator(),Slot.Event.Scale,false,false,ENCPoolMethod::ManualRelease,false);
	if (!Slot.SingleComponent) return false;
	Slot.SingleComponent->SetCastShadow(false);
	Slot.SingleComponent->SetVariableLinearColor(TEXT("User.Tint"),Slot.Event.Tint);
	Slot.SingleComponent->SetVariableFloat(TEXT("User.Radius"),0);
	if (auto* Overview=GetWorld()->GetSubsystem<UGuLiCommanderOverviewSubsystem>()) Overview->RegisterVisual(Slot.SingleComponent);
	Slot.SingleComponent->Activate(true); return true;
}

void UGuLiImpactBatchPresentation::Release(UNiagaraComponent* C)
{
	if (!IsValid(C)) return;
	C->DeactivateImmediate(); UGuLiCommanderOverviewSubsystem::ForgetVisual(C); C->ReleaseToPool();
}

void UGuLiImpactBatchPresentation::RetireSlot(int32 Index)
{
	auto& Slot=Slots[Index]; Release(Slot.SingleComponent); Pool.Release(Slot.Handle); Slot={};
}

void UGuLiImpactBatchPresentation::BeginFrame(uint64 FrameNumber, double LocalWorldSeconds, bool bRenderEnabled)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiImpacts_Lifecycle);
	FGuLiPerformanceScope Timing(GetWorld(),TEXT("Impact.LifecycleMs"));
	WorldNow=LocalWorldSeconds; bEnabled=bRenderEnabled;
	if (!bEnabled) { Reset(); return; }
	while (SeenOrder.IsValidIndex(SeenHead) && SeenOrder[SeenHead].Value<=WorldNow) Seen.Remove(SeenOrder[SeenHead++].Key);
	if (SeenHead>=1024) { SeenOrder.RemoveAt(0,SeenHead,EAllowShrinking::No); SeenHead=0; }
	Expired.Reset();
	for (const int32 I:Pool.ActiveIndices())
	{
		auto& Slot=Slots[I];
		if (Slot.PublishedFrame!=MAX_uint64 && Slot.BornAt<0 && FrameNumber>Slot.PublishedFrame)
		{
			Slot.BornAt=WorldNow; Slot.ExpiresAt=WorldNow+Slot.Event.Lifetime;
			Pool.MarkPayloadChanged();
			if (Slot.Mode!=2 && !SpawnSingle(Slot)) { Expired.Add(I); continue; }
		}
		bool bOffscreenExpired=false;
		if (GuLiClientPresentation::OffscreenLifecycleEnabled() && Slot.bKnownBounds)
		{
			FGuLiCommanderLODQuery Query; Query.Bounds=Slot.Bounds;
			if (IsValid(Slot.SingleComponent) && Slot.SingleComponent->Bounds.GetBox().IsValid) Query.Bounds+=Slot.SingleComponent->Bounds.GetBox();
			auto* LOD=GetWorld()->GetSubsystem<UGuLiCommanderLODSubsystem>();
			if (!LOD || LOD->EvaluateWorldEffectBounds(Query,20000).bVisible) Slot.OffscreenSince=-1;
			else if (Slot.OffscreenSince<0) Slot.OffscreenSince=WorldNow;
			else bOffscreenExpired=WorldNow-Slot.OffscreenSince>=GuLiClientPresentation::OffscreenGraceSeconds;
		}
		if (bOffscreenExpired || (Slot.ExpiresAt>=0 && WorldNow>=Slot.ExpiresAt)
			|| (IsValid(Slot.SingleComponent) && Slot.SingleComponent->IsComplete())) Expired.Add(I);
	}
	for (const int32 I:Expired) RetireSlot(I);
}

int32 UGuLiImpactBatchPresentation::FindOrAddGroup(UNiagaraSystem* System)
{
	for (int32 I=0;I<Groups.Num();++I) if (Groups[I].System==System) return I;
	auto& Group=Groups.AddDefaulted_GetRef(); Group.System=System; return Groups.Num()-1;
}

void UGuLiImpactBatchPresentation::PublishFrame(uint64 FrameNumber)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiImpacts_Publish);
	FGuLiPerformanceScope Timing(GetWorld(),TEXT("Impact.PublishMs"));
	if (!bEnabled || !GetWorld()) return;
	for (auto& Group:Groups) { Group.bNeeded=false; Group.Bounds=FBox(ForceInit); }
	TArray<int32,TInlineAllocator<128>> Rows;
	for (const int32 I:Pending)
	{
		if (!Slots.IsValidIndex(I) || !Pool.IsLive(Slots[I].Handle)) continue;
		auto& Slot=Slots[I];
		Slot.PublishedFrame=FrameNumber;
		if (Slot.Mode==2) { Slot.Group=FindOrAddGroup(Slot.BatchSystem); Rows.Add(I); }
	}
	Pending.Reset();
	for (const int32 I:Pool.ActiveIndices())
	{
		const auto& Slot=Slots[I]; if (Slot.Mode!=2 || Slot.Group==INDEX_NONE) continue;
		auto& Group=Groups[Slot.Group]; Group.bNeeded=true; Group.Bounds+=Slot.Bounds;
	}
	const uint64 Revision=Pool.GetRevision();
	TArray<int32> Generations;
	TArray<float> BirthTimes;
	for (int32 I=0;I<Groups.Num();++I)
	{
		auto& Group=Groups[I];
		if (!Group.bNeeded) { Release(Group.Component); Group.Component=nullptr; Group.UploadStamp.Reset(); continue; }
		bool bStart=false;
		if (!IsValid(Group.Component))
		{
			Group.Component=UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(),Group.System,FVector::ZeroVector,
				FRotator::ZeroRotator,FVector::OneVector,false,false,ENCPoolMethod::ManualRelease,false);
			if (!Group.Component) continue;
			Group.Component->SetVariableInt(TEXT("User.ImpactGroup"),I);
			Group.Component->SetCastShadow(false);
			if (auto* Overview=GetWorld()->GetSubsystem<UGuLiCommanderOverviewSubsystem>()) Overview->RegisterVisual(Group.Component);
			bStart=true;
		}
		if (Group.UploadStamp.NeedsUpload(FrameNumber,Revision,bStart))
		{
			if (Generations.IsEmpty())
			{
				Generations=Pool.ActiveGenerations(); BirthTimes.Init(float(WorldNow),Pool.Capacity());
				for (const int32 Active:Pool.ActiveIndices()) BirthTimes[Active]=float(Slots[Active].BornAt>=0 ? Slots[Active].BornAt : WorldNow);
			}
			UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayInt32(Group.Component,TEXT("User.ImpactGenerations"),Generations);
			UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayFloat(Group.Component,TEXT("User.ImpactBirthTimes"),BirthTimes);
			Group.UploadStamp.Commit(FrameNumber,Revision); ++GenerationUploads;
		}
		Group.Component->SetSystemFixedBounds(Group.Bounds);
		Group.Component->SetVariableFloat(TEXT("User.ImpactWorldTime"),float(WorldNow));
		if (bStart) Group.Component->Activate(true);
	}
	if (!Rows.IsEmpty() && Channel && Channel->Get())
	{
		for (int32 I=Rows.Num()-1;I>=0;--I)
			if (!IsValid(Groups[Slots[Rows[I]].Group].Component))
			{ Slots[Rows[I]].Mode=1; Slots[Rows[I]].Group=INDEX_NONE; Rows.RemoveAtSwap(I); ++Fallbacks; }
		if (Rows.IsEmpty()) return;
		FNDCAccessContextInst Context(Channel->Get()->GetAccessContextType());
		auto* Writer=UNiagaraDataChannelLibrary::WriteToNiagaraDataChannel_WithContext(GetWorld(),Channel,Context,Rows.Num(),false,true,false,TEXT("GuLiImpactBatch"));
		if (Writer)
		{
			for (int32 Row=0;Row<Rows.Num();++Row)
			{
				const auto& Slot=Slots[Rows[Row]]; const auto& E=Slot.Event;
				Writer->WritePosition(TEXT("Position"),Row,E.Position); Writer->WriteQuat(TEXT("Rotation"),Row,E.Rotation);
				Writer->WriteVector(TEXT("Scale"),Row,E.Scale); Writer->WriteLinearColor(TEXT("Tint"),Row,E.Tint);
				Writer->WriteInt(TEXT("Seed"),Row,E.Seed); Writer->WriteFloat(TEXT("Lifetime"),Row,E.Lifetime);
				Writer->WriteInt(TEXT("Slot"),Row,Slot.Handle.Index); Writer->WriteInt(TEXT("Generation"),Row,int32(Slot.Handle.Generation));
				Writer->WriteInt(TEXT("Group"),Row,Slot.Group); ++Published;
			}
		}
		else for (const int32 I:Rows) { Slots[I].Mode=1; Slots[I].Group=INDEX_NONE; ++Fallbacks; }
	}
	if (auto* Capture=GetWorld()->GetSubsystem<UGuLiPerformanceSubsystem>())
	{
		Capture->Record(TEXT("Impact.Active"),GetActiveCount()); Capture->Record(TEXT("Impact.Components"),GetComponentCount());
		Capture->Record(TEXT("Impact.AcceptedTotal"),double(Accepted)); Capture->Record(TEXT("Impact.PublishedTotal"),double(Published));
		Capture->Record(TEXT("Impact.FallbacksTotal"),double(Fallbacks)); Capture->Record(TEXT("Impact.UnknownBoundsTotal"),double(UnknownBounds));
	}
}

int32 UGuLiImpactBatchPresentation::GetComponentCount() const
{
	int32 Count=0; for (const auto& Group:Groups) Count+=IsValid(Group.Component);
	for (const int32 I:Pool.ActiveIndices()) Count+=IsValid(Slots[I].SingleComponent);
	return Count;
}

void UGuLiImpactBatchPresentation::Reset()
{
	for (const int32 I:Pool.ActiveIndices()) Release(Slots[I].SingleComponent);
	for (const auto& Group:Groups) Release(Group.Component);
	Pool.Reset(); Slots.Reset(); Groups.Reset(); Pending.Reset(); Expired.Reset(); Seen.Reset(); SeenOrder.Reset(); SeenHead=0;
}
