#include "Gameplay/CombatEffects/GuLiWingmanProjectilePresentation.h"
#include "Gameplay/CombatEffects/GuLiProjectileFlightPresentationProfile.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "Gameplay/Vfx/GuLiClientPresentationPolicy.h"
#include "Gameplay/Performance/GuLiPerformanceSubsystem.h"
#include "Commander/Presentation/GuLiCommanderLODSubsystem.h"
#include "Commander/Presentation/GuLiCommanderOverviewSubsystem.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

static TAutoConsoleVariable<int32> GWingmanPresentation(TEXT("gs.WingmanFlight.Presentation"),1,
	TEXT("0 legacy flight; 1 optional opaque pulsing core and bounded mesh trail."));

UWorld* UGuLiWingmanProjectilePresentation::GetWorld() const { return GetOuter() ? GetOuter()->GetWorld() : nullptr; }
int32 UGuLiWingmanProjectilePresentation::FindGroup(UGuLiProjectileFlightPresentationProfile* Profile) const
{
	for (int32 I=0;I<Groups.Num();++I) if (Groups[I].Profile==Profile) return I; return INDEX_NONE;
}
void UGuLiWingmanProjectilePresentation::Prepare(UGuLiProjectileFlightPresentationProfile* Profile)
{
	if (!GetWorld() || GetWorld()->GetNetMode()==NM_DedicatedServer || !Profile || !Profile->IsValidProfile() || FindGroup(Profile)!=INDEX_NONE) return;
	auto* System=Profile->BatchSystem.LoadSynchronous();
	if (!System || UGuLiVfxRegistrySubsystem::NiagaraFloat(System,TEXT("User.GuLiWingmanInputVersion"),0)!=1) return;
	auto& Group=Groups.AddDefaulted_GetRef(); Group.Profile=Profile; Group.System=System;
}

void UGuLiWingmanProjectilePresentation::BeginFrame(double Now,double ServerNow,bool bEnabled)
{
	LocalTime=Now; ServerTime=ServerNow; bFrameEnabled=bEnabled && GWingmanPresentation.GetValueOnGameThread()!=0;
	if (!bFrameEnabled) { Reset(); return; }
	for (auto& Group:Groups)
	{
		for (const int32 I:Group.Pool.ActiveIndices()) Group.Slots[I].bSubmitted=false;
		for (auto& Block:Group.Blocks)
		{
			Block.bDirty=false; Block.Bounds=FBox(ForceInit); Block.UsedRows.Reset();
			for (const int32 I:Block.PreviousRows)
			{
				Block.bDirty|=!Block.Scales[I].IsZero();
				Block.Scales[I]=FVector::ZeroVector; Block.Colors[I]=FLinearColor::Transparent;
			}
		}
	}
}

void UGuLiWingmanProjectilePresentation::AppendTrail(FGuLiWingmanFlightSlot& Slot,const FVector& Position,float Limit)
{
	if (Slot.Trail.IsEmpty()) { Slot.Trail.Add(Position); Slot.ArcLength=0; return; }
	const float Distance=float(FVector::Distance(Slot.Trail.Last(),Position));
	if (Distance<.1f) return;
	Slot.Trail.Add(Position); Slot.ArcLength+=Distance;
	while (Slot.Trail.Num()>1 && Slot.ArcLength>Limit)
	{
		const float Segment=float(FVector::Distance(Slot.Trail[0],Slot.Trail[1]));
		const float Excess=Slot.ArcLength-Limit;
		if (Segment<=Excess+.001f)
		{ Slot.ArcLength-=Segment; Slot.Trail.RemoveAt(0,1,EAllowShrinking::No); }
		else
		{ Slot.Trail[0]=FMath::Lerp(Slot.Trail[0],Slot.Trail[1],Excess/Segment); Slot.ArcLength=Limit; }
	}
	// Bound retained samples while preserving the head and old cut endpoint.
	if (Slot.Trail.Num()>128)
	{
		const float Removed=float(FVector::Distance(Slot.Trail[0],Slot.Trail[1])+FVector::Distance(Slot.Trail[1],Slot.Trail[2])-FVector::Distance(Slot.Trail[0],Slot.Trail[2]));
		Slot.Trail.RemoveAt(1,1,EAllowShrinking::No); Slot.ArcLength=FMath::Max(0.f,Slot.ArcLength-Removed);
	}
}

FVector UGuLiWingmanProjectilePresentation::SampleTrail(const FGuLiWingmanFlightSlot& Slot,float Distance)
{
	if (Slot.Trail.IsEmpty()) return Slot.Head;
	for (int32 I=1;I<Slot.Trail.Num();++I)
	{
		const float Span=float(FVector::Distance(Slot.Trail[I-1],Slot.Trail[I]));
		if (Distance<=Span) return FMath::Lerp(Slot.Trail[I-1],Slot.Trail[I],Span>UE_SMALL_NUMBER ? Distance/Span : 0.f);
		Distance-=Span;
	}
	return Slot.Trail.Last();
}

FBox UGuLiWingmanProjectilePresentation::BoundsFor(const FGuLiWingmanFlightSlot& Slot,float W0)
{
	FBox Bounds(ForceInit); Bounds+=Slot.Head; for (const auto& P:Slot.Trail) Bounds+=P;
	return Bounds.ExpandBy(W0*1.25f);
}

bool UGuLiWingmanProjectilePresentation::Submit(const FGuid& Id,const FVector& Position,double LaunchServerTime,UGuLiProjectileFlightPresentationProfile* Profile)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiWingmanFlight_Submit);
	FGuLiPerformanceScope Timing(GetWorld(),TEXT("WingmanFlight.SubmitMs"));
	if (!bFrameEnabled || Position.ContainsNaN()) return false;
	const int32 GroupIndex=FindGroup(Profile); if (GroupIndex==INDEX_NONE) return false;
	auto& Group=Groups[GroupIndex];
	FLocation* Location=Locations.Find(Id);
	if (!Location)
	{
		const auto Handle=Group.Pool.Acquire(); Group.Slots.SetNum(Group.Pool.Capacity());
		auto& Slot=Group.Slots[Handle.Index]; Slot={}; Slot.Id=Id; Slot.Handle=Handle; Slot.StartServerTime=LaunchServerTime;
		Location=&Locations.Add(Id,{GroupIndex,Handle});
		Group.Blocks.SetNum(Group.Pool.Capacity()/SlotsPerBlock);
	}
	if (Location->Group!=GroupIndex || !Group.Pool.IsLive(Location->Handle)) return false;
	auto& Slot=Group.Slots[Location->Handle.Index]; Slot.Head=Position; Slot.bSubmitted=true;
	FGuLiCommanderLODQuery Query; Query.Bounds=BoundsFor(Slot,Profile->LegacyCoreDiameter);
	if (Slot.LODChangedAt>=0) Query.CurrentLevel=Slot.Level;
	Query.LastChangeWorldSeconds=Slot.LODChangedAt;
	FGuLiCommanderLODDecision Decision; Decision.bVisible=true; Decision.TargetLevel=EGuLiCommanderLODLevel::Full;
	if (auto* LOD=GetWorld()->GetSubsystem<UGuLiCommanderLODSubsystem>()) Decision=LOD->EvaluateWorldEffectBounds(Query,20000);
	if (!Decision.bVisible)
	{
		if (Slot.bVisible) ++OffscreenDrops;
		Slot.bVisible=false; Slot.Trail.Reset(); Slot.ArcLength=0; return true;
	}
	if (!Slot.bVisible) { ++Reentries; Slot.Trail.Reset(); Slot.ArcLength=0; }
	auto& Block=Group.Blocks[Location->Handle.Index/SlotsPerBlock];
	if (!IsValid(Block.Component))
	{
		Block.Component=UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(),Group.System,FVector::ZeroVector,
			FRotator::ZeroRotator,FVector::OneVector,false,false,ENCPoolMethod::ManualRelease,false);
		if (!Block.Component) { Slot.bOwnsPresentation=false; Slot.Trail.Reset(); Slot.ArcLength=0; return false; }
		Block.Component->SetVariableInt(TEXT("User.WingmanSlotCount"),SlotsPerBlock); Block.Component->SetCastShadow(false);
		if (auto* Overview=GetWorld()->GetSubsystem<UGuLiCommanderOverviewSubsystem>()) Overview->RegisterVisual(Block.Component);
	}
	Slot.bOwnsPresentation=true;
	if (Slot.LODChangedAt<0 || (Decision.bCanTransition && Slot.Level!=Decision.TargetLevel))
	{ Slot.Level=Decision.TargetLevel; Slot.LODChangedAt=LocalTime; }
	Slot.bVisible=true;
	AppendTrail(Slot,Position,Profile->MaximumTrailLength); return true;
}

void UGuLiWingmanProjectilePresentation::Finish(const FGuid& Id,const FVector& Position,bool bImmediate)
{
	const auto* Location=Locations.Find(Id); if (!Location || !Groups.IsValidIndex(Location->Group)) return;
	auto& Group=Groups[Location->Group]; if (!Group.Pool.IsLive(Location->Handle)) return;
	auto& Slot=Group.Slots[Location->Handle.Index];
	if (Slot.FinishedAt>=0) return;
	if (bImmediate || !Slot.bVisible || Slot.Trail.Num()<2) { ReleaseSlot(Location->Group,Location->Handle.Index); return; }
	Slot.Head=Position; AppendTrail(Slot,Position,Group.Profile->MaximumTrailLength);
	Slot.FinishedAt=GetWorld()->GetTimeSeconds(); Slot.bSubmitted=true;
}

void UGuLiWingmanProjectilePresentation::Release(UNiagaraComponent* C)
{
	if (IsValid(C)) { C->DeactivateImmediate(); UGuLiCommanderOverviewSubsystem::ForgetVisual(C); C->ReleaseToPool(); }
}
void UGuLiWingmanProjectilePresentation::ReleaseSlot(int32 GroupIndex,int32 SlotIndex)
{
	auto& Group=Groups[GroupIndex]; auto& Slot=Group.Slots[SlotIndex];
	Locations.Remove(Slot.Id); Group.Pool.Release(Slot.Handle); Slot={};
}

void UGuLiWingmanProjectilePresentation::EndFrame()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiWingmanFlight_Publish);
	FGuLiPerformanceScope Timing(GetWorld(),TEXT("WingmanFlight.PublishMs"));
	ActiveCount=0; if (!bFrameEnabled) return;
	for (int32 G=0;G<Groups.Num();++G)
	{
		auto& Group=Groups[G]; const auto* Profile=Group.Profile.Get(); TArray<int32,TInlineAllocator<32>> Remove;
		for (const int32 I:Group.Pool.ActiveIndices())
		{
			auto& Slot=Group.Slots[I];
			const float Fade=Slot.FinishedAt<0 ? 1.f : FMath::Clamp(1.f-float((LocalTime-Slot.FinishedAt)/Profile->TrailFadeSeconds),0.f,1.f);
			if (Fade<=0 || (Slot.FinishedAt<0 && !Slot.bSubmitted)) { Remove.Add(I); continue; }
			++ActiveCount;
			if (!Slot.bVisible || !Slot.bOwnsPresentation) continue;
			if (Slot.FinishedAt>=0)
			{
				FGuLiCommanderLODQuery Query; Query.Bounds=BoundsFor(Slot,Profile->LegacyCoreDiameter);
				if (auto* LOD=GetWorld()->GetSubsystem<UGuLiCommanderLODSubsystem>(); LOD && !LOD->EvaluateWorldEffectBounds(Query,20000).bVisible)
				{ Remove.Add(I); continue; }
			}
			auto& Block=Group.Blocks[I/SlotsPerBlock]; const int32 First=(I%SlotsPerBlock)*RowsPerSlot;
			if (Block.Positions.IsEmpty())
			{
				Block.Positions.Init(FVector::ZeroVector,SlotsPerBlock*RowsPerSlot); Block.Scales.Init(FVector::ZeroVector,SlotsPerBlock*RowsPerSlot);
				Block.Orientations.Init(FQuat::Identity,SlotsPerBlock*RowsPerSlot); Block.Colors.Init(FLinearColor::Transparent,SlotsPerBlock*RowsPerSlot);
			}
			auto WriteRow=[&](int32 Row,const FVector& Position,const FVector& Scale,const FQuat& Rotation,const FLinearColor& Color)
			{
				Block.bDirty|=Block.Positions[Row]!=Position || Block.Scales[Row]!=Scale || Block.Orientations[Row]!=Rotation || Block.Colors[Row]!=Color;
				Block.Positions[Row]=Position; Block.Scales[Row]=Scale; Block.Orientations[Row]=Rotation; Block.Colors[Row]=Color; Block.UsedRows.Add(Row);
			};
			if (Slot.FinishedAt<0)
			{
				const double Phase=FMath::Max(0.0,ServerTime-Slot.StartServerTime)/Profile->PulsePeriod;
				const float U=float((1-FMath::Cos(2*UE_PI*Phase))*.5);
				const float Diameter=Profile->LegacyCoreDiameter*(1.5f+U);
				FLinearColor Color=Profile->CoreColor*(1+3*U); Color.A=1;
				WriteRow(First,Slot.Head,FVector(Diameter/100.f),FQuat::Identity,Color);
			}
			const int32 Segments=Slot.Level==EGuLiCommanderLODLevel::Full ? 8 : Slot.Level==EGuLiCommanderLODLevel::Reduced ? 4 : 2;
			for (int32 Segment=0;Segment<Segments && Slot.ArcLength>UE_SMALL_NUMBER;++Segment)
			{
				const float A=float(Segment)/Segments, B=float(Segment+1)/Segments;
				const FVector From=SampleTrail(Slot,Slot.ArcLength*A),To=SampleTrail(Slot,Slot.ArcLength*B);
				const FVector Delta=To-From; const float Length=float(Delta.Size()); if (Length<=UE_SMALL_NUMBER) continue;
				const float StartWidth=Profile->LegacyCoreDiameter*.35f*FMath::Max(A,.001f)*Fade;
				const float EndWidth=Profile->LegacyCoreDiameter*.35f*B*Fade;
				FLinearColor Color=Profile->TrailColor; Color.A=StartWidth/EndWidth;
				// Unit cylinder is 100cm tall along +Z. Material tapers local XY towards its old endpoint.
				WriteRow(First+1+Segment,(From+To)*.5,FVector(EndWidth/100.f,EndWidth/100.f,Length/100.f),
					FQuat::FindBetweenNormals(FVector::UpVector,Delta/Length),Color);
			}
			Block.Bounds+=BoundsFor(Slot,Profile->LegacyCoreDiameter);
		}
		for (const int32 I:Remove) ReleaseSlot(G,I);
		for (auto& Block:Group.Blocks)
		{
			Swap(Block.PreviousRows,Block.UsedRows);
			if (!Block.Bounds.IsValid) { Release(Block.Component); Block.Component=nullptr; Block.UploadStamp.Reset(); continue; }
			bool bStart=IsValid(Block.Component) && !Block.Component->IsActive();
			if (!IsValid(Block.Component))
			{
				Block.Component=UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(),Group.System,FVector::ZeroVector,
					FRotator::ZeroRotator,FVector::OneVector,false,false,ENCPoolMethod::ManualRelease,false);
				if (!Block.Component) continue; bStart=true;
				Block.Component->SetVariableInt(TEXT("User.WingmanSlotCount"),SlotsPerBlock); Block.Component->SetCastShadow(false);
				if (auto* Overview=GetWorld()->GetSubsystem<UGuLiCommanderOverviewSubsystem>()) Overview->RegisterVisual(Block.Component);
			}
			if (Block.bDirty) ++Block.PayloadRevision;
			if (Block.UploadStamp.NeedsUpload(GFrameCounter,Block.PayloadRevision,bStart))
			{
				using Arrays=UNiagaraDataInterfaceArrayFunctionLibrary;
				Arrays::SetNiagaraArrayPosition(Block.Component,TEXT("User.WingmanPositions"),Block.Positions);
				Arrays::SetNiagaraArrayVector(Block.Component,TEXT("User.WingmanScales"),Block.Scales);
				Arrays::SetNiagaraArrayQuat(Block.Component,TEXT("User.WingmanOrientations"),Block.Orientations);
				Arrays::SetNiagaraArrayColor(Block.Component,TEXT("User.WingmanColors"),Block.Colors);
				Block.UploadStamp.Commit(GFrameCounter,Block.PayloadRevision); Uploads+=4;
			}
			Block.Component->SetSystemFixedBounds(Block.Bounds); if (bStart) Block.Component->Activate(true);
		}
	}
	if (auto* Capture=GetWorld()->GetSubsystem<UGuLiPerformanceSubsystem>())
	{
		Capture->Record(TEXT("WingmanFlight.Active"),ActiveCount); Capture->Record(TEXT("WingmanFlight.Components"),GetComponentCount());
		Capture->Record(TEXT("WingmanFlight.UploadsTotal"),double(Uploads)); Capture->Record(TEXT("WingmanFlight.OffscreenDropsTotal"),double(OffscreenDrops));
	}
}

int32 UGuLiWingmanProjectilePresentation::GetComponentCount() const
{ int32 Count=0; for (const auto& G:Groups) for (const auto& B:G.Blocks) Count+=IsValid(B.Component); return Count; }
void UGuLiWingmanProjectilePresentation::Reset()
{
	for (auto& Group:Groups)
	{
		for (auto& Block:Group.Blocks) Release(Block.Component);
		Group.Blocks.Reset(); Group.Slots.Reset(); Group.Pool.Reset();
	}
	Locations.Reset(); ActiveCount=0;
}
