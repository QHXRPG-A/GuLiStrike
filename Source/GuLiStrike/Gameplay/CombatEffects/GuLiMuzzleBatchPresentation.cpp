#include "Gameplay/CombatEffects/GuLiMuzzleBatchPresentation.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectDefinition.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "Gameplay/Data/Generated/GuLiVfxIds.h"
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
#if WITH_EDITOR
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#endif

static TAutoConsoleVariable<int32> GMuzzleBatchMode(TEXT("gs.Muzzles.BatchMode"),2,
	TEXT("0: immediate individual; 1: F+1 individual; 2: F+1 NDC batch, with same-tier F+1 fallback."));

UWorld* UGuLiMuzzleBatchPresentation::GetWorld() const { return GetOuter() ? GetOuter()->GetWorld() : nullptr; }

void UGuLiMuzzleBatchPresentation::Prepare(const UGuLiCombatEffectCatalog* Catalog)
{
	if (bPrepared || !Catalog || !GetWorld() || GetWorld()->GetNetMode()==NM_DedicatedServer) return;
	bPrepared=true; Channel=Catalog->MuzzleChannel.LoadSynchronous();
	if (auto* Registry=GetWorld()->GetSubsystem<UGuLiVfxRegistrySubsystem>())
		for (int32 I=0;I<3;++I)
		{
			auto* Single=Registry->LoadNiagaraForLOD(GuLiVfxIds::GroundMachineGunMuzzle,EGuLiCommanderLODLevel(I));
			if (I==0) FullSystem=Single;
			Registry->LoadNiagaraForLOD(GuLiVfxIds::GroundMachineGunMuzzle,EGuLiCommanderLODLevel(I),true);
		}
}

bool UGuLiMuzzleBatchPresentation::Enqueue(const FGuLiCombatShotCue& Cue,int32 ReviewMode)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiMuzzles_Accept);
	FGuLiPerformanceScope Timing(GetWorld(),TEXT("Muzzle.AcceptMs"));
	if (!GetWorld() || !Cue.bMechanicalShot || !Cue.ShotId.IsValid()) return false;
	const FGuLiMuzzleIdentity Identity{Cue.ShotId,Cue.MatchEpoch};
	if (Seen.Contains(Identity)) { ++Duplicates; return false; }
	const double Deadline=GetWorld()->GetTimeSeconds()+4;
	Seen.Add(Identity,Deadline); SeenOrder.Emplace(Identity,Deadline);
	const auto Handle=Pool.Acquire(); Slots.SetNum(Pool.Capacity());
	auto& Slot=Slots[Handle.Index]; Slot={}; Slot.Handle=Handle; Slot.Cue=Cue;
	Slot.AcceptedFrame=GFrameCounter;
	Slot.ReviewMode=GetWorld()->IsPlayInEditor() ? ReviewMode : INDEX_NONE;
	++Accepted; return true;
}

bool UGuLiMuzzleBatchPresentation::Admit(FGuLiMuzzleSlot& Slot)
{
	auto* Registry=GetWorld()->GetSubsystem<UGuLiVfxRegistrySubsystem>(); if (!Registry) return false;
	FGuLiCommanderLODDecision Decision; Decision.bVisible=true; Decision.TargetLevel=EGuLiCommanderLODLevel::Full;
	if (Slot.bKnownBounds)
		if (auto* LOD=GetWorld()->GetSubsystem<UGuLiCommanderLODSubsystem>())
		{ FGuLiCommanderLODQuery Query; Query.Bounds=Slot.Bounds; Decision=LOD->EvaluateWorldEffectBounds(Query,20000); }
	if (!Decision.bVisible) { ++Culled; return false; }
	if (!GuLiClientPresentation::ThreeTierEffectsEnabled()) Decision.TargetLevel=EGuLiCommanderLODLevel::Full;
	Slot.SingleSystem=Registry->LoadNiagaraForLOD(GuLiVfxIds::GroundMachineGunMuzzle,Decision.TargetLevel,false,false);
	if (!Slot.SingleSystem) return false;
	Slot.Mode=FMath::Clamp(Slot.ReviewMode!=INDEX_NONE ? Slot.ReviewMode : GMuzzleBatchMode.GetValueOnGameThread(),0,2);
	Slot.BirthPose=Slot.Pose; Slot.bAdmitted=true;
	if (Slot.Mode==2)
	{
		Slot.BatchSystem=Registry->LoadNiagaraForLOD(GuLiVfxIds::GroundMachineGunMuzzle,Decision.TargetLevel,true,false);
		const bool bCertified=Channel && Channel->Get() && Slot.bKnownBounds && Slot.BatchSystem
			&& UGuLiVfxRegistrySubsystem::NiagaraFloat(Slot.BatchSystem,TEXT("User.GuLiMuzzleInputVersion"),0)==1
			&& UGuLiVfxRegistrySubsystem::NiagaraFloat(Slot.BatchSystem,TEXT("User.GuLiMuzzleMaximumLifetime"),0)>=1;
		if (!bCertified) { Slot.BatchSystem=nullptr; Slot.Mode=1; ++Fallbacks; }
	}
	return true;
}

bool UGuLiMuzzleBatchPresentation::SpawnSingle(FGuLiMuzzleSlot& Slot)
{
	Slot.SingleComponent=UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(),Slot.SingleSystem,
		Slot.Pose.GetLocation(),Slot.Pose.Rotator(),Slot.Scale,false,false,ENCPoolMethod::ManualRelease,false);
	if (!Slot.SingleComponent) return false;
	Slot.SingleComponent->SetCastShadow(false);
	if (auto* Overview=GetWorld()->GetSubsystem<UGuLiCommanderOverviewSubsystem>()) Overview->RegisterVisual(Slot.SingleComponent);
	Slot.SingleComponent->Activate(true); return true;
}

void UGuLiMuzzleBatchPresentation::Release(UNiagaraComponent* C)
{
	if (!IsValid(C)) return;
	C->DeactivateImmediate(); UGuLiCommanderOverviewSubsystem::ForgetVisual(C); C->ReleaseToPool();
}

void UGuLiMuzzleBatchPresentation::Retire(int32 I)
{
	auto& Slot=Slots[I]; Release(Slot.SingleComponent); Pool.Release(Slot.Handle); Slot={}; ++PoseRevision;
}

void UGuLiMuzzleBatchPresentation::BeginFrame(uint64 Frame,double LocalSeconds,float ServerSeconds,bool bRender,FPoseResolver Resolve)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiMuzzles_Lifecycle);
	FGuLiPerformanceScope Timing(GetWorld(),TEXT("Muzzle.LifecycleMs"));
	WorldNow=LocalSeconds; bEnabled=bRender;
	const int32 Mode=FMath::Clamp(GMuzzleBatchMode.GetValueOnGameThread(),0,2);
	if (!bEnabled) { Reset(); LastMode=Mode; return; }
	// Mode changes discard old live visuals and pending rows, preserving duplicate history.
	// Never requeue already played feedback in the new mode.
	if (LastMode!=INDEX_NONE && LastMode!=Mode)
	{
		TArray<int32> Active; Active.Append(Pool.ActiveIndices()); for (const int32 I:Active) Retire(I);
		for (auto& Group:Groups) { Release(Group.Component); Group.Component=nullptr; Group.LifeStamp.Reset(); Group.PoseStamp.Reset(); }
		Pending.Reset();
	}
	LastMode=Mode;
	while (SeenOrder.IsValidIndex(SeenHead) && SeenOrder[SeenHead].Value<=WorldNow) Seen.Remove(SeenOrder[SeenHead++].Key);
	if (SeenHead>=1024) { SeenOrder.RemoveAt(0,SeenHead,EAllowShrinking::No); SeenHead=0; }
	Retired.Reset();
	for (const int32 I:Pool.ActiveIndices())
	{
		auto& Slot=Slots[I]; FTransform Pose; float RenderTime=ServerSeconds; ++PoseQueries;
		const bool bPoseUnresolved = !Resolve(Slot.Cue,Pose,RenderTime) || Pose.ContainsNaN();
		if (bPoseUnresolved || ServerSeconds-Slot.Cue.ServerTime>2)
		{
			++Expired;
			if (bPoseUnresolved) ++PoseUnresolved; else ++TooOld;
			Retired.Add(I); continue;
		}
		const float AuthAge=RenderTime-Slot.Cue.MechanicalPoseTimeSeconds;
		if (!Slot.bAdmitted && AuthAge>0.20f) { ++Expired; ++AdmissionLate; Retired.Add(I); continue; }
		if (!Pose.Equals(Slot.Pose)) { Slot.Pose=Pose; ++PoseRevision; }
		Slot.Scale=GuLiVfx::Scale(this,GuLiVfxIds::GroundMachineGunMuzzle,
			FVector(Slot.Cue.Source.Kind==EGuLiTargetKind::CommanderSoldier && Slot.Cue.UnitTypeId==2 ? 2.f : 1.f));
		if (FullSystem)
		{
			const FNiagaraVariable Var(FNiagaraTypeDefinition::GetVec3Def(),TEXT("User.GuLiPreSpawnBoundsExtent"));
			const auto& Params=FullSystem->GetExposedParameters();
			if (Params.IndexOf(Var)!=INDEX_NONE)
			{
				const FVector Extent(Params.GetParameterValue<FVector3f>(Var));
				if (!Extent.ContainsNaN() && Extent.GetMin()>0)
				{ FTransform Transform=Pose; Transform.SetScale3D(Slot.Scale); Slot.Bounds+=FBox(-Extent,Extent).TransformBy(Transform); Slot.bKnownBounds=true; }
			}
		}
		if (IsValid(Slot.SingleComponent) && Slot.SingleComponent->Bounds.GetBox().IsValid) Slot.Bounds+=Slot.SingleComponent->Bounds.GetBox();
		bool bVisible=true;
		if (Slot.bKnownBounds)
			if (auto* LOD=GetWorld()->GetSubsystem<UGuLiCommanderLODSubsystem>())
			{ FGuLiCommanderLODQuery Query; Query.Bounds=Slot.Bounds; bVisible=LOD->EvaluateWorldEffectBounds(Query,20000).bVisible; }
		if (bVisible) Slot.OffscreenSince=-1;
		else if (Slot.OffscreenSince<0) Slot.OffscreenSince=WorldNow;
		if (!bVisible && (!Slot.bAdmitted || !GuLiClientPresentation::OffscreenLifecycleEnabled()
			|| WorldNow-Slot.OffscreenSince>=GuLiClientPresentation::OffscreenGraceSeconds))
		{ ++OffscreenRecycled; Retired.Add(I); continue; }
		if (!Slot.bAdmitted)
		{
			if (AuthAge<0) continue;
			if (!Slot.bKnownBounds) ++UnknownBounds;
			if (!Admit(Slot)) { Retired.Add(I); continue; }
			if (Slot.Mode==0)
			{
				Slot.BornAt=WorldNow; Slot.PublishedFrame=Frame; Slot.BornFrame=Frame; Pool.MarkPayloadChanged();
				if (!SpawnSingle(Slot)) { Retired.Add(I); continue; } ++Born;
			}
			else Pending.Add(I);
		}
		if (Slot.PublishedFrame!=MAX_uint64 && Slot.BornAt<0 && Frame>Slot.PublishedFrame)
		{
			Slot.BornAt=WorldNow; Slot.BornFrame=Frame; Pool.MarkPayloadChanged();
			if (Slot.Mode!=2 && !SpawnSingle(Slot)) { Retired.Add(I); continue; } ++Born;
		}
		if (Slot.BornAt<0) continue;
		const double Age=WorldNow-Slot.BornAt;
		if (Age>1 || (IsValid(Slot.SingleComponent) && Slot.SingleComponent->IsComplete()))
		{ Retired.Add(I); continue; }
		if (IsValid(Slot.SingleComponent))
		{ Slot.SingleComponent->SetWorldLocationAndRotation(Pose.GetLocation(),Pose.GetRotation()); if (Age>.24) Slot.SingleComponent->Deactivate(); }
	}
	for (const int32 I:Retired) Retire(I);
}

int32 UGuLiMuzzleBatchPresentation::FindOrAddGroup(UNiagaraSystem* System)
{
	for (int32 I=0;I<Groups.Num();++I) if (Groups[I].System==System) return I;
	auto& Group=Groups.AddDefaulted_GetRef(); Group.System=System; return Groups.Num()-1;
}

void UGuLiMuzzleBatchPresentation::PublishFrame(uint64 Frame)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiMuzzles_Publish);
	FGuLiPerformanceScope Timing(GetWorld(),TEXT("Muzzle.PublishMs"));
	if (!bEnabled || !GetWorld()) return;
	for (auto& Group:Groups) { Group.bNeeded=false; Group.Bounds=FBox(ForceInit); }
	TArray<int32,TInlineAllocator<128>> Rows;
	for (const int32 I:Pending)
	{
		if (!Slots.IsValidIndex(I) || !Pool.IsLive(Slots[I].Handle)) continue;
		auto& Slot=Slots[I]; Slot.PublishedFrame=Frame;
		if (Slot.Mode==2) { Slot.Group=FindOrAddGroup(Slot.BatchSystem); Rows.Add(I); }
	}
	Pending.Reset();
	for (const int32 I:Pool.ActiveIndices())
	{
		const auto& Slot=Slots[I]; if (Slot.Mode!=2 || Slot.Group==INDEX_NONE) continue;
		auto& Group=Groups[Slot.Group]; Group.bNeeded=true; Group.Bounds+=Slot.Bounds;
	}
	bool bLifeBuilt=false,bPoseBuilt=false;
	for (int32 I=0;I<Groups.Num();++I)
	{
		auto& Group=Groups[I];
		if (!Group.bNeeded) { Release(Group.Component); Group.Component=nullptr; Group.LifeStamp.Reset(); Group.PoseStamp.Reset(); continue; }
		bool bStart=false;
		if (!IsValid(Group.Component))
		{
			Group.Component=UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(),Group.System,FVector::ZeroVector,
				FRotator::ZeroRotator,FVector::OneVector,false,false,ENCPoolMethod::ManualRelease,false);
			if (!Group.Component) continue;
			Group.Component->SetVariableInt(TEXT("User.MuzzleGroup"),I); Group.Component->SetCastShadow(false);
			if (auto* Overview=GetWorld()->GetSubsystem<UGuLiCommanderOverviewSubsystem>()) Overview->RegisterVisual(Group.Component);
			bStart=true;
		}
		if (Group.LifeStamp.NeedsUpload(Frame,Pool.GetRevision(),bStart))
		{
			if (!bLifeBuilt)
			{
				Generations=Pool.ActiveGenerations(); BirthTimes.Init(float(WorldNow),Pool.Capacity());
				for (const int32 A:Pool.ActiveIndices()) BirthTimes[A]=float(Slots[A].BornAt>=0 ? Slots[A].BornAt : WorldNow);
				bLifeBuilt=true;
			}
			UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayInt32(Group.Component,TEXT("User.MuzzleGenerations"),Generations);
			UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayFloat(Group.Component,TEXT("User.MuzzleBirthTimes"),BirthTimes);
			Group.LifeStamp.Commit(Frame,Pool.GetRevision()); ++LifeUploads;
		}
		if (Group.PoseStamp.NeedsUpload(Frame,PoseRevision,bStart))
		{
			if (!bPoseBuilt)
			{
				Positions.Init(FVector::ZeroVector,Pool.Capacity()); Rotations.Init(FQuat::Identity,Pool.Capacity());
				for (const int32 A:Pool.ActiveIndices()) { Positions[A]=Slots[A].Pose.GetLocation(); Rotations[A]=Slots[A].Pose.GetRotation(); }
				bPoseBuilt=true;
			}
			UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayPosition(Group.Component,TEXT("User.MuzzlePositions"),Positions);
			UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayQuat(Group.Component,TEXT("User.MuzzleRotations"),Rotations);
			Group.PoseStamp.Commit(Frame,PoseRevision); ++PoseUploads;
		}
		Group.Component->SetSystemFixedBounds(Group.Bounds); Group.Component->SetVariableFloat(TEXT("User.MuzzleWorldTime"),float(WorldNow));
		if (bStart) Group.Component->Activate(true);
	}
	if (!Rows.IsEmpty())
	{
		for (int32 R=Rows.Num()-1;R>=0;--R)
			if (!Channel || !Channel->Get() || !IsValid(Groups[Slots[Rows[R]].Group].Component))
			{ Slots[Rows[R]].Mode=1; Slots[Rows[R]].Group=INDEX_NONE; Rows.RemoveAtSwap(R); ++Fallbacks; }
		if (!Rows.IsEmpty())
		{
			FNDCAccessContextInst Context(Channel->Get()->GetAccessContextType());
			auto* Writer=UNiagaraDataChannelLibrary::WriteToNiagaraDataChannel_WithContext(GetWorld(),Channel,Context,Rows.Num(),false,true,false,TEXT("GuLiMuzzleBatch"));
			if (Writer) for (int32 R=0;R<Rows.Num();++R)
			{
				const auto& Slot=Slots[Rows[R]];
				Writer->WritePosition(TEXT("Position"),R,Slot.BirthPose.GetLocation()); Writer->WriteQuat(TEXT("Rotation"),R,Slot.BirthPose.GetRotation());
				Writer->WriteVector(TEXT("Scale"),R,Slot.Scale); Writer->WriteLinearColor(TEXT("Tint"),R,FLinearColor::White);
				Writer->WriteInt(TEXT("Seed"),R,int32(GetTypeHash(Slot.Cue.ShotId))); Writer->WriteFloat(TEXT("Lifetime"),R,1.f);
				Writer->WriteInt(TEXT("Slot"),R,Slot.Handle.Index); Writer->WriteInt(TEXT("Generation"),R,int32(Slot.Handle.Generation));
				Writer->WriteInt(TEXT("Group"),R,Slot.Group); ++Published;
			}
			else for (const int32 R:Rows) { Slots[R].Mode=1; Slots[R].Group=INDEX_NONE; ++Fallbacks; }
		}
	}
	if (auto* Capture=GetWorld()->GetSubsystem<UGuLiPerformanceSubsystem>())
	{
		Capture->Record(TEXT("Muzzle.Active"),GetActiveCount()); Capture->Record(TEXT("Muzzle.Components"),GetComponentCount());
		Capture->Record(TEXT("Muzzle.AcceptedTotal"),double(Accepted)); Capture->Record(TEXT("Muzzle.BornTotal"),double(Born));
		Capture->Record(TEXT("Muzzle.PublishedTotal"),double(Published)); Capture->Record(TEXT("Muzzle.FallbacksTotal"),double(Fallbacks));
		Capture->Record(TEXT("Muzzle.PoseQueriesTotal"),double(PoseQueries)); Capture->Record(TEXT("Muzzle.LifeUploadsTotal"),double(LifeUploads));
		Capture->Record(TEXT("Muzzle.PoseUploadsTotal"),double(PoseUploads)); Capture->Record(TEXT("Muzzle.OffscreenTotal"),double(OffscreenRecycled));
	}
}

int32 UGuLiMuzzleBatchPresentation::GetComponentCount() const
{
	int32 Count=0; for (const auto& G:Groups) Count+=IsValid(G.Component);
	for (const int32 I:Pool.ActiveIndices()) Count+=IsValid(Slots[I].SingleComponent); return Count;
}

void UGuLiMuzzleBatchPresentation::Reset()
{
	for (const int32 I:Pool.ActiveIndices()) Release(Slots[I].SingleComponent);
	for (const auto& G:Groups) Release(G.Component);
	Pool.Reset(); Slots.Reset(); Groups.Reset(); Pending.Reset(); Retired.Reset(); Seen.Reset(); SeenOrder.Reset(); SeenHead=0; ++PoseRevision;
}

#if WITH_EDITOR
FString UGuLiMuzzleBatchPresentation::GetProtocolSnapshot() const
{
	auto Result=MakeShared<FJsonObject>(); Result->SetNumberField(TEXT("frame"),double(GFrameCounter));
	Result->SetNumberField(TEXT("world_time"),GetWorld() ? GetWorld()->GetTimeSeconds() : 0);
	Result->SetNumberField(TEXT("capacity"),Pool.Capacity()); Result->SetNumberField(TEXT("components"),GetComponentCount());
	TArray<TSharedPtr<FJsonValue>> Entries;
	for (const int32 I:Pool.ActiveIndices())
	{
		const auto& S=Slots[I]; auto E=MakeShared<FJsonObject>();
		E->SetStringField(TEXT("shot"),S.Cue.ShotId.ToString()); E->SetNumberField(TEXT("slot"),I); E->SetNumberField(TEXT("generation"),S.Handle.Generation);
		E->SetNumberField(TEXT("epoch"),S.Handle.Epoch); E->SetNumberField(TEXT("mode"),S.Mode); E->SetNumberField(TEXT("group"),S.Group);
		E->SetNumberField(TEXT("accepted_frame"),double(S.AcceptedFrame)); E->SetNumberField(TEXT("published_frame"),S.PublishedFrame==MAX_uint64 ? -1 : double(S.PublishedFrame));
		E->SetNumberField(TEXT("born_frame"),S.BornFrame==MAX_uint64 ? -1 : double(S.BornFrame)); E->SetNumberField(TEXT("born_at"),S.BornAt);
		const FVector P=S.Pose.GetLocation(); E->SetArrayField(TEXT("position"),{MakeShared<FJsonValueNumber>(P.X),MakeShared<FJsonValueNumber>(P.Y),MakeShared<FJsonValueNumber>(P.Z)});
		E->SetNumberField(TEXT("scale"),S.Scale.X); Entries.Add(MakeShared<FJsonValueObject>(E));
	}
	Result->SetArrayField(TEXT("slots"),Entries); FString Json; FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Json)); return Json;
}
#endif
