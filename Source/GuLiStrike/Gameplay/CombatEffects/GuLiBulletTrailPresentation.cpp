#include "Gameplay/CombatEffects/GuLiBulletTrailPresentation.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectDefinition.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "Commander/Presentation/GuLiCommanderOverviewSubsystem.h"
#include "Network/GuLiNetworkSettings.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "Engine/World.h"

UWorld* UGuLiBulletTrailPresentation::GetWorld() const { return GetOuter() ? GetOuter()->GetWorld() : nullptr; }

void UGuLiBulletTrailPresentation::Prepare(UGuLiCombatEffectCatalog* Catalog)
{
	Reset(); MassSystem=nullptr; LightSystem=nullptr;
	if (!Catalog || !UGuLiNetworkSettings::CanPresent(GetWorld())) return;
	auto* Mass=GuLiVfx::Load<UNiagaraSystem>(this,Catalog->GroundMachineGunVfxId);
	if (Mass && Mass->GetExposedParameters().IndexOf(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.TrailNow")))!=INDEX_NONE)
		MassSystem=Mass;
	LightSystem=GuLiVfx::Load<UNiagaraSystem>(this,Catalog->BulletLightVfxId);
	Style=FVector4(Catalog->MassTrailWidth,12,.06,.18); MaximumLength=1200;
	FGuLiStrikeVfxEffectsRow Row;
	if (UGuLiVfxRegistrySubsystem::Query(this,Catalog->GroundMachineGunVfxId,Row) && Row.TrailLengthCm>0)
	{ MaximumLength=Row.TrailLengthCm; Style.Y=Row.TrailBendCm; Style.Z=Row.TrailHoldSeconds; Style.W=Row.TrailFadeSeconds; }
	Emission=FMath::Max(0.f,Catalog->LaserIntensity);
	Radius=FMath::Max(0.f,Catalog->TracerLightRadius); Brightness=FMath::Max(0.f,Catalog->TracerLightBrightness);
	LightBudget=FMath::Clamp(Catalog->MaximumTracerLightsPerFrame,0,MaximumLights);
}

void UGuLiBulletTrailPresentation::EnsureBlock(int32 Index)
{
	while (Blocks.Num()<=Index)
	{
		auto& Block=Blocks.AddDefaulted_GetRef();
		Block.Origins.Init(FVector::ZeroVector,BlockSize); Block.Directions.Init(FVector::ForwardVector,BlockSize);
		Block.Facings.Init(FVector::UpVector,BlockSize); Block.Speeds.Init(1.f,BlockSize);
		Block.Times.Init(FVector4(0,0,-1.e30,0),BlockSize); Block.Colors.Init(FLinearColor::Transparent,BlockSize);
		Block.Generations.Init(0,BlockSize);
	}
}

void UGuLiBulletTrailPresentation::ReleaseComponent(TObjectPtr<UNiagaraComponent>& Component)
{
	if (IsValid(Component))
	{ Component->DeactivateImmediate(); UGuLiCommanderOverviewSubsystem::ForgetVisual(Component); Component->ReleaseToPool(); }
	Component=nullptr;
}

UNiagaraComponent* UGuLiBulletTrailPresentation::AcquireComponent(UNiagaraSystem* System, int32 Capacity)
{
	if (!System || !UGuLiNetworkSettings::CanPresent(GetWorld())) return nullptr;
	auto* Component=UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(),System,FVector::ZeroVector,
		FRotator::ZeroRotator,FVector::OneVector,false,false,ENCPoolMethod::ManualRelease,false);
	if (Component)
	{
		Component->SetVariableInt(TEXT("User.TrailSlotCount"),Capacity); Component->SetCastShadow(false);
		if (auto* Overview=GetWorld()->GetSubsystem<UGuLiCommanderOverviewSubsystem>()) Overview->RegisterVisual(Component);
	}
	return Component;
}

void UGuLiBulletTrailPresentation::BeginFrame(float ServerNow, bool Enabled)
{
	Now=ServerNow; bEnabled=Enabled;
	if (!bClockInitialized) { ClockOrigin=Now; bClockInitialized=true; }
	LightCandidates.Reset(); SeenLights.Reset(); LightCount=0;
	for (auto& Block:Blocks) Block.Bounds=FBox(ForceInit);
}

bool UGuLiBulletTrailPresentation::SubmitMass(const FGuLiCombatEffectState& State, FVector Origin, FVector Facing,
	FLinearColor Tint, bool Visible)
{
	if (!MassSystem || !State.EffectId.IsValid() || Origin.ContainsNaN()) return true;
	const bool Terminal=State.Phase==EGuLiCombatEffectPhase::Finished;
	const float End=Terminal ? FMath::Clamp(State.SampleTime,State.StartTime,State.EndTime) : State.EndTime;
	if (Now>=End+Style.Z+Style.W) return true;
	auto* Entry=Entries.Find(State.EffectId);
	if (!Entry)
	{
		FEntry New; New.Handle=Slots.Acquire(); New.Origin=Origin;
		New.Direction=FVector(State.LaunchDirection).GetSafeNormal();
		New.Speed=FMath::Max(1.f,float(FVector(State.Velocity).Size())); New.Start=State.StartTime; New.End=State.EndTime;
		EnsureBlock(New.Handle.Index/BlockSize);
		auto& Block=Blocks[New.Handle.Index/BlockSize]; const int32 I=New.Handle.Index%BlockSize;
		Block.Origins[I]=Origin; Block.Directions[I]=New.Direction; Block.Facings[I]=Facing.GetSafeNormal(UE_SMALL_NUMBER,FVector::UpVector);
		Block.Speeds[I]=New.Speed; Block.bLaunchDirty=true;
		Entry=&Entries.Add(State.EffectId,New);
	}
	if (!Slots.IsLive(Entry->Handle)) return true;
	const float TerminalDistance=Terminal ? FMath::Max(0.f,float(FVector::DotProduct(FVector(State.Location)-Entry->Origin,Entry->Direction))) : 0;
	Entry->TerminalTime=Terminal ? End : -1; Entry->TerminalDistance=TerminalDistance;
	auto& Block=Blocks[Entry->Handle.Index/BlockSize]; const int32 I=Entry->Handle.Index%BlockSize;
	// A terminal can predate this client's clock origin, so -1 is not a safe sentinel.
	const FVector4 Times(Entry->Start-ClockOrigin,Entry->End-ClockOrigin,Terminal ? End-ClockOrigin : -1.e30,TerminalDistance);
	if (Block.Times[I]!=Times) { Block.Times[I]=Times; Block.bLifeDirty=true; }
	const int32 Generation=bEnabled && Visible ? int32(Entry->Handle.Generation) : 0;
	Tint.A=Generation ? 1.f : 0.f;
	if (Block.Colors[I]!=Tint || Block.Generations[I]!=Generation)
	{ Block.Colors[I]=Tint; Block.Generations[I]=Generation; Block.bColorDirty=true; }
	if (Generation)
	{
		const float Distance=Terminal ? TerminalDistance : Entry->Speed*FMath::Clamp(Now-Entry->Start,0.f,Entry->End-Entry->Start);
		const float EndAge=FMath::Max(0.f,Now-End);
		const float Start=FMath::Max(0.f,Distance-FMath::Min(MaximumLength,Entry->Speed*FMath::Max(0.f,float(Style.Z+Style.W)-EndAge)));
		FBox Bounds(ForceInit); Bounds+=Entry->Origin+Entry->Direction*Start; Bounds+=Entry->Origin+Entry->Direction*Distance;
		Block.Bounds+=Bounds.ExpandBy(2.f*float(Style.X+Style.Y));
	}
	return false;
}

void UGuLiBulletTrailPresentation::SubmitLight(FGuid Id,FVector Head,FLinearColor Tint,double DistanceSquared,
	bool Visible,float NearDistance,float FarDistance)
{
	if (!bEnabled || !LightSystem || LightBudget==0 || !Visible || !FMath::IsFinite(DistanceSquared) || Head.ContainsNaN()) return;
	const float Distance=FMath::Sqrt(DistanceSquared);
	if (Distance>FarDistance || (!NearLights.Contains(Id) && Distance>NearDistance)) return;
	NearLights.Add(Id); SeenLights.Add(Id);
	Tint.A=Brightness*FMath::Clamp((FarDistance-Distance)/FMath::Max(1.f,FarDistance-NearDistance),0.f,1.f);
	if (Tint.A<=0 || Radius<=0) return;
	const FLight Candidate{Id,Head,Tint,DistanceSquared};
	int32 Insert=0;
	while (Insert<LightCandidates.Num() && (LightCandidates[Insert].DistanceSquared<DistanceSquared
		|| (LightCandidates[Insert].DistanceSquared==DistanceSquared && LightCandidates[Insert].Id<Id))) ++Insert;
	if (Insert<LightBudget)
	{
		if (LightCandidates.Num()==LightBudget) LightCandidates.Pop(EAllowShrinking::No);
		LightCandidates.Insert(Candidate,Insert);
	}
}

void UGuLiBulletTrailPresentation::EndFrame()
{
	using Arrays=UNiagaraDataInterfaceArrayFunctionLibrary;
	for (auto& Block:Blocks)
	{
		if (!bEnabled || !Block.Bounds.IsValid) { ReleaseComponent(Block.Component); continue; }
		const bool New=!IsValid(Block.Component);
		if (New) Block.Component=AcquireComponent(MassSystem,BlockSize);
		if (!Block.Component) continue;
		if (New || Block.bLaunchDirty)
		{
			Arrays::SetNiagaraArrayPosition(Block.Component,TEXT("User.TrailOrigins"),Block.Origins);
			Arrays::SetNiagaraArrayVector(Block.Component,TEXT("User.TrailDirections"),Block.Directions);
			Arrays::SetNiagaraArrayVector(Block.Component,TEXT("User.TrailFacings"),Block.Facings);
			Arrays::SetNiagaraArrayFloat(Block.Component,TEXT("User.TrailSpeeds"),Block.Speeds);
			ArrayUploads+=4; Block.bLaunchDirty=false;
		}
		if (New || Block.bLifeDirty)
		{ Arrays::SetNiagaraArrayVector4(Block.Component,TEXT("User.TrailTimes"),Block.Times); ++ArrayUploads; Block.bLifeDirty=false; }
		if (New || Block.bColorDirty)
		{
			Arrays::SetNiagaraArrayColor(Block.Component,TEXT("User.TrailColors"),Block.Colors);
			Arrays::SetNiagaraArrayInt32(Block.Component,TEXT("User.TrailGenerations"),Block.Generations);
			ArrayUploads+=2; Block.bColorDirty=false;
		}
		Block.Component->SetVariableFloat(TEXT("User.TrailNow"),Now-ClockOrigin);
		if (New)
		{
			Block.Component->SetVariableVec4(TEXT("User.TrailStyle"),Style);
			Block.Component->SetVariableFloat(TEXT("User.TrailMaximumLength"),MaximumLength);
			Block.Component->SetVariableFloat(TEXT("User.TrailEmission"),Emission);
		}
		if (New || Block.UploadedBounds!=Block.Bounds)
		{ Block.Component->SetSystemFixedBounds(Block.Bounds); Block.UploadedBounds=Block.Bounds; }
		if (New) Block.Component->Activate(true);
	}
	for (auto It=NearLights.CreateIterator();It;++It) if (!SeenLights.Contains(*It)) It.RemoveCurrent();
	if (LightCandidates.IsEmpty()) { ReleaseComponent(Lights); return; }
	const bool New=!IsValid(Lights); if (New) Lights=AcquireComponent(LightSystem,MaximumLights);
	if (!Lights) return;
	TArray<FVector> Positions; Positions.Init(FVector::ZeroVector,MaximumLights);
	TArray<FLinearColor> Colors; Colors.Init(FLinearColor::Transparent,MaximumLights);
	TArray<float> Radii; Radii.Init(0.f,MaximumLights); TArray<bool> Enabled; Enabled.Init(false,MaximumLights);
	FBox Bounds(ForceInit);
	for (int32 I=0;I<LightCandidates.Num();++I)
	{
		Positions[I]=LightCandidates[I].Position; Colors[I]=LightCandidates[I].Color; Radii[I]=Radius; Enabled[I]=true;
		Bounds+=FBox(Positions[I]-FVector(Radius),Positions[I]+FVector(Radius));
	}
	if (New || Positions!=LightPositions || Colors!=LightColors || Radii!=LightRadii || Enabled!=LightEnabled)
	{
		Arrays::SetNiagaraArrayPosition(Lights,TEXT("User.BulletLightPositions"),Positions);
		Arrays::SetNiagaraArrayColor(Lights,TEXT("User.BulletLightColors"),Colors);
		Arrays::SetNiagaraArrayFloat(Lights,TEXT("User.BulletLightRadii"),Radii);
		Arrays::SetNiagaraArrayBool(Lights,TEXT("User.BulletLightEnabled"),Enabled); ArrayUploads+=4;
		LightPositions=MoveTemp(Positions); LightColors=MoveTemp(Colors); LightRadii=MoveTemp(Radii); LightEnabled=MoveTemp(Enabled);
		Lights->SetSystemFixedBounds(Bounds);
	}
	LightCount=LightCandidates.Num(); if (New) Lights->Activate(true);
}

void UGuLiBulletTrailPresentation::Remove(FGuid Id)
{
	NearLights.Remove(Id);
	if (const auto* Entry=Entries.Find(Id); Entry && Slots.IsLive(Entry->Handle))
	{
		auto& Block=Blocks[Entry->Handle.Index/BlockSize]; const int32 I=Entry->Handle.Index%BlockSize;
		Block.Colors[I]=FLinearColor::Transparent; Block.Generations[I]=0; Block.bColorDirty=true;
		Slots.Release(Entry->Handle);
	}
	Entries.Remove(Id);
}

void UGuLiBulletTrailPresentation::Reset()
{
	for (auto& Block:Blocks) ReleaseComponent(Block.Component);
	ReleaseComponent(Lights); Blocks.Reset(); Entries.Reset(); Slots.Reset();
	NearLights.Reset(); SeenLights.Reset(); LightCandidates.Reset(); LightCount=0; bClockInitialized=false;
}

int32 UGuLiBulletTrailPresentation::GetComponentCount() const
{
	int32 Count=IsValid(Lights) ? 1 : 0; for (const auto& Block:Blocks) if (IsValid(Block.Component)) ++Count; return Count;
}
