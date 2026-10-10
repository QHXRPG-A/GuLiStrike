#include "Gameplay/Performance/GuLiPerformanceSubsystem.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "Gameplay/Vfx/GuLiClientPresentationPolicy.h"
#include "Commander/Presentation/GuLiCommanderLODSubsystem.h"
#include "Commander/Presentation/GuLiCommanderOverviewSubsystem.h"
#include "Engine/World.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectPresentationSubsystem.h"
#include "Gameplay/CombatEffects/GuLiFlightVisualActor.h"

#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarLaserBatchSize(TEXT("gs.LaserPool.BatchSize"), 256,
		TEXT("Particle slots per new laser pool; resolved only while the pool is empty. 1024 restores the old capacity."));
	constexpr int32 MaximumGroundTracerLights = 6;
	struct FGroundTracerLightCandidate
	{
		int32 Slot = INDEX_NONE;
		double DistanceSquared = 0;
		FVector Position = FVector::ZeroVector;
		FLinearColor Color = FLinearColor::Transparent;
		float Radius = 0;
		bool IsBefore(const FGroundTracerLightCandidate& Other) const
		{
			return DistanceSquared < Other.DistanceSquared
				|| (DistanceSquared == Other.DistanceSquared && Slot < Other.Slot);
		}
	};
}

int32 UGuLiCombatEffectPresentationSubsystem::AllocateLaserSlot(const int32 VfxId)
{
	const FVector BaseScale = GuLiVfx::Scale(this, VfxId);
	if (!UGuLiVfxRegistrySubsystem::IsValidScale(BaseScale)) return INDEX_NONE;
	if (LaserBlocks.IsEmpty()) LaserBlockSize = FMath::Clamp(CVarLaserBatchSize.GetValueOnGameThread(), 32, 1024);
	auto& FreeSlots = FreeLaserSlots.FindOrAdd(VfxId);
	if (FreeSlots.IsEmpty())
	{
		const int32 First = LaserBlocks.Num() * LaserBlockSize;
		auto& Block = LaserBlocks.AddDefaulted_GetRef();
		Block.VfxId = VfxId; Block.BaseScale = BaseScale;
		Block.Positions.Init(FVector::ZeroVector, LaserBlockSize); Block.MuzzlePositions = Block.Positions;
		Block.Directions.Init(FVector::ForwardVector, LaserBlockSize);
		Block.Sizes.Init(FVector2D::ZeroVector, LaserBlockSize); Block.MuzzleSizes = Block.Sizes;
		Block.Colors.Init(FLinearColor::Transparent, LaserBlockSize); Block.MuzzleColors = Block.Colors;
		Block.LightPositions.Init(FVector::ZeroVector, LaserBlockSize);
		Block.LightColors.Init(FLinearColor::Transparent, LaserBlockSize);
		Block.LightRadii.Init(0.0f, LaserBlockSize); Block.LightEnabled.Init(false, LaserBlockSize);
		Block.UploadedRows.SetNum(LaserBlockSize);
		for (int32 Index = First + LaserBlockSize - 1; Index >= First; --Index) FreeSlots.Add(Index);
	}
	return FreeSlots.Pop(EAllowShrinking::No);
}

void UGuLiCombatEffectPresentationSubsystem::FreeLaserSlot(const int32 Slot)
{
	if (Slot < 0 || !LaserBlocks.IsValidIndex(Slot / LaserBlockSize)) return;
	auto& Block = LaserBlocks[Slot / LaserBlockSize]; const int32 Index = Slot % LaserBlockSize;
	Block.Colors[Index] = Block.MuzzleColors[Index] = FLinearColor::Transparent;
	Block.Sizes[Index] = Block.MuzzleSizes[Index] = FVector2D::ZeroVector;
	Block.LightPositions[Index] = FVector::ZeroVector;
	Block.LightColors[Index] = FLinearColor::Transparent;
	Block.LightRadii[Index] = 0.0f; Block.LightEnabled[Index] = false;
	FreeLaserSlots.FindOrAdd(Block.VfxId).Add(Slot);
}

void UGuLiCombatEffectPresentationSubsystem::ResetLaserPool()
{
	for (auto& Block : LaserBlocks) if (IsValid(Block.Component))
	{ Block.Component->DeactivateImmediate(); UGuLiCommanderOverviewSubsystem::ForgetVisual(Block.Component); Block.Component->ReleaseToPool(); }
	LaserBlocks.Reset(); FreeLaserSlots.Reset();
	Counters.GroundMachineGunLights = 0;
}

void UGuLiCombatEffectPresentationSubsystem::UpdateLaserPool(const float Now, const bool bEnabled)
{
	Counters.LaserCapacity = LaserBlocks.Num() * LaserBlockSize;
	Counters.LaserActive = Counters.LaserCapacity; Counters.LaserVisible = 0;
	Counters.GroundMachineGunLights = 0;
	const int32 LightBudget = Catalog ? FMath::Clamp(Catalog->MaximumTracerLightsPerFrame, 0, MaximumGroundTracerLights) : 0;
	TArray<FGroundTracerLightCandidate, TInlineAllocator<MaximumGroundTracerLights>> LightCandidates;
	for (const auto& Pair : FreeLaserSlots) Counters.LaserActive -= Pair.Value.Num();
	for (auto& Block : LaserBlocks)
	{
		for (const int32 I : Block.PreviousUsedSlots)
		{
			Block.Colors[I]=Block.MuzzleColors[I]=Block.LightColors[I]=FLinearColor::Transparent;
			Block.Sizes[I]=Block.MuzzleSizes[I]=FVector2D::ZeroVector;
			Block.LightPositions[I]=FVector::ZeroVector; Block.LightRadii[I]=0; Block.LightEnabled[I]=false;
		}
		Block.UsedSlots.Reset();
		Block.LightCount = 0;
		Block.Bounds = FBox(ForceInit); Block.bVisible = false;
	}
	EGuLiTeam LocalTeam = EGuLiTeam::Unassigned;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const auto* PC = It->Get();
		if (!PC || !PC->IsLocalController()) continue;
		if (const auto* PS = PC->GetPlayerState<AGuLiBattlePlayerState>()) LocalTeam = PS->GetTeam();
		// The possessed pawn's replicated combat identity can arrive before PlayerState.
		if (LocalTeam == EGuLiTeam::Unassigned && PC->GetPawn())
			if (const auto* Health = PC->GetPawn()->FindComponentByClass<UGuLiCombatHealthComponent>()) LocalTeam = Health->GetCombatTeam();
		if (LocalTeam != EGuLiTeam::Unassigned) break;
	}
	const float LocalNow = GetWorld()->GetTimeSeconds();
	if (bEnabled && Catalog) for (const auto& Id : LaserVisualIds)
	{
		auto* Entry = Visuals.Find(Id); if (!Entry) continue;
		auto& Visual = *Entry; const auto& State = Visual.State;
		if (!GuLiFlightWire::IsFlight(State.Kind) || Visual.LaserSlot == INDEX_NONE) continue;
		const bool bFinished = State.Phase == EGuLiCombatEffectPhase::Finished;
		const bool bGround = State.Source.Kind == EGuLiTargetKind::CommanderSoldier;
		const float RenderTime = Now;
		const bool bReachedTerminal = bFinished;
		const float Age = FMath::Clamp(RenderTime - State.StartTime, 0.0f, State.EndTime - State.StartTime);
		const FVector PredictedHead = Visual.bHasPrediction ? Visual.RenderLocation
            : FVector(State.LaunchLocation)+FVector(State.Velocity)*Age;
        const FVector Head = bReachedTerminal ? FVector(State.Location) : PredictedHead
            + (bGround ? EvaluateLaunchVisualOffset(Visual,TEXT("BasicAttack"),RenderTime) : FVector::ZeroVector);
		Visual.RenderLocation = Head;
		auto& Block = LaserBlocks[Visual.LaserSlot / LaserBlockSize]; const int32 Index = Visual.LaserSlot % LaserBlockSize;
		const FVector Direction = Visual.bHasPrediction ? FVector(Visual.Prediction.Velocity).GetSafeNormal() : FVector(State.LaunchDirection).GetSafeNormal();
		const float AuthoredLength = Catalog->LaserLength * Block.BaseScale.X;
		const float Length = FMath::Min(AuthoredLength, bGround ? float(FVector(State.Velocity).Size()) * Age
			: static_cast<float>(FVector::Distance(Head, State.LaunchLocation)));
		FBox EffectBounds(ForceInit); EffectBounds += Head; EffectBounds += Head-Direction*Length;
		const float Radius=FMath::Max(Catalog->LaserCoreWidth*3.f*Block.BaseScale.Y, bGround ? Catalog->TracerLightRadius*Block.BaseScale.Z : 0.f);
		FGuLiCommanderLODQuery DetailQuery; DetailQuery.Bounds=EffectBounds.ExpandBy(FMath::Max(1.f,Radius));
		DetailQuery.CurrentLevel=Visual.FlightDetailLevel; DetailQuery.LastChangeWorldSeconds=Visual.FlightDetailChangedAt;
		FGuLiCommanderLODDecision Detail; Detail.bVisible=true; Detail.TargetLevel=EGuLiCommanderLODLevel::Full;
		if (auto* LOD=GetWorld()->GetSubsystem<UGuLiCommanderLODSubsystem>()) Detail=LOD->EvaluateWorldEffectBounds(DetailQuery,20000);
		if (Visual.FlightDetailChangedAt<0 || (Detail.bCanTransition && Visual.FlightDetailLevel!=Detail.TargetLevel))
		{ Visual.FlightDetailLevel=Detail.TargetLevel; Visual.FlightDetailChangedAt=LocalNow; }
		const bool bBoltVisible=Detail.bVisible;
		FVector Muzzle = State.LaunchLocation;
		FGuLiCombatShotCue Cue; Cue.Source = State.Source; Cue.MuzzleOffset = State.MuzzleOffset;
		const bool bMuzzleVisible = !bGround && LocalNow < Visual.LaserMuzzleUntil && ResolveMuzzlePosition(Cue, Muzzle) && IsVisibleBounds(FBox(Muzzle-FVector(Catalog->LaserCoreWidth*3.f*Block.BaseScale.Y+30.f), Muzzle+FVector(Catalog->LaserCoreWidth*3.f*Block.BaseScale.Y+30.f)));
		if (!bBoltVisible && !bMuzzleVisible) continue;
		Block.UsedSlots.Add(Index);
		FLinearColor Tint = LocalTeam == EGuLiTeam::Unassigned ? FLinearColor::White
			: (State.SourceTeam == LocalTeam
				? (bGround ? Catalog->FriendlyGroundMachineGunTint : Catalog->FriendlyLaserTint)
				: (bGround ? Catalog->EnemyGroundMachineGunTint : Catalog->EnemyLaserTint));
		const float Fade = bFinished ? FMath::Clamp((Visual.LaserFadeUntil - LocalNow) / 0.04f, 0.0f, 1.0f) : 1.0f;
		// Keep lighting independent of the sprite's emissive multiplier and width/length scale.
		const FLinearColor LightTint(Tint.R, Tint.G, Tint.B, Catalog->TracerLightBrightness * Fade);
		Tint.R *= Catalog->LaserIntensity; Tint.G *= Catalog->LaserIntensity; Tint.B *= Catalog->LaserIntensity; Tint.A = Fade;
		Block.Directions[Index] = Direction;
		if (bBoltVisible)
		{
			Block.Positions[Index] = Head - Direction * (Length * 0.5f);
			// The analytic material's central half is the authored core; the outside supplies a soft halo.
			Block.Sizes[Index] = FVector2D(Catalog->LaserCoreWidth * 2.0f * Block.BaseScale.Y,
				bGround ? Length : FMath::Max(0.2f * Block.BaseScale.X, Length));
			Block.Colors[Index] = Tint; Block.Bounds += Head; Block.Bounds += Head - Direction * Length;
			++Counters.LaserVisible;
			const float LightRadius = Catalog->TracerLightRadius * Block.BaseScale.Z;
			if (bGround && Visual.FlightDetailLevel!=EGuLiCommanderLODLevel::Minimal && LightBudget > 0 && Length > UE_KINDA_SMALL_NUMBER && Fade > 0.0f
				&& FMath::IsFinite(LightRadius) && LightRadius > 0.0f
				&& FMath::IsFinite(LightTint.R) && FMath::IsFinite(LightTint.G) && FMath::IsFinite(LightTint.B)
				&& FMath::IsFinite(LightTint.A) && LightTint.A > 0.0f)
			{
				const FGroundTracerLightCandidate Candidate{Visual.LaserSlot, ClosestLocalCameraDistanceSquared(Head), Head, LightTint, LightRadius};
				// Retain only the nearest global budget. Slot breaks ties independently of TMap order.
				int32 InsertIndex = 0;
				while (InsertIndex < LightCandidates.Num() && !Candidate.IsBefore(LightCandidates[InsertIndex])) ++InsertIndex;
				if (InsertIndex < LightBudget)
				{
					if (LightCandidates.Num() == LightBudget) LightCandidates.Pop(EAllowShrinking::No);
					LightCandidates.Insert(Candidate, InsertIndex);
				}
			}
		}
		if (bMuzzleVisible)
		{
			Block.MuzzlePositions[Index] = Muzzle + Direction * 15.0f;
			Block.MuzzleSizes[Index] = FVector2D(Catalog->LaserCoreWidth * 3.0f * Block.BaseScale.Y, 6.0f * Block.BaseScale.X);
			Block.MuzzleColors[Index] = Tint; Block.MuzzleColors[Index].A = FMath::Clamp((Visual.LaserMuzzleUntil - LocalNow) / Catalog->LaserMuzzleSeconds, 0.0f, 1.0f);
			Block.Bounds += Muzzle; Block.Bounds += Muzzle + Direction * 30.0f;
		}
		Block.bVisible = true;
	}
	for (const FGroundTracerLightCandidate& Light : LightCandidates)
	{
		auto& Block = LaserBlocks[Light.Slot / LaserBlockSize];
		const int32 Index = Light.Slot % LaserBlockSize;
		Block.LightPositions[Index] = Light.Position; Block.LightColors[Index] = Light.Color;
		Block.LightRadii[Index] = Light.Radius; Block.LightEnabled[Index] = true;
		++Block.LightCount;
		Block.Bounds += FBox(Light.Position - FVector(Light.Radius), Light.Position + FVector(Light.Radius));
	}
	for (auto& Block : LaserBlocks)
	{
		bool Dirty = false;
		auto CompareRow = [&](const int32 I)
		{
			const FGuLiLaserUploadedRow Row{Block.Positions[I],Block.Directions[I],Block.MuzzlePositions[I],Block.LightPositions[I],
				Block.Sizes[I],Block.MuzzleSizes[I],Block.Colors[I],Block.MuzzleColors[I],Block.LightColors[I],Block.LightRadii[I],Block.LightEnabled[I]};
			if (!(Row==Block.UploadedRows[I])) { Dirty=true; Block.UploadedRows[I]=Row; }
		};
		for (const int32 I : Block.PreviousUsedSlots) CompareRow(I);
		for (const int32 I : Block.UsedSlots) CompareRow(I);
		Swap(Block.PreviousUsedSlots,Block.UsedSlots);
		if (!Block.bVisible)
		{
			if (IsValid(Block.Component)) { Block.Component->DeactivateImmediate(); UGuLiCommanderOverviewSubsystem::ForgetVisual(Block.Component); Block.Component->ReleaseToPool(); Block.Component = nullptr; }
			continue;
		}
		bool bActivate = false;
		if (!IsValid(Block.Component))
		{
			UNiagaraSystem* System = GuLiVfx::Load<UNiagaraSystem>(this, Block.VfxId);
			if (!System) continue;
			// World-space positions stay unchanged; apply base scale only to the particle size arrays.
			Block.Component = UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), System, FVector::ZeroVector,
				FRotator::ZeroRotator, FVector::OneVector, false, false, ENCPoolMethod::ManualRelease, false);
			if (!Block.Component) continue;
			Block.Component->SetVariableInt(TEXT("User.LaserSlotCount"), LaserBlockSize);
			if (auto* Overview = GetWorld()->GetSubsystem<UGuLiCommanderOverviewSubsystem>()) Overview->RegisterVisual(Block.Component);
			Block.Component->SetCastShadow(false); bActivate = true;
		}
		Counters.GroundMachineGunLights += Block.LightCount;
		if (Dirty || bActivate)
		{
		TRACE_CPUPROFILER_EVENT_SCOPE(GuLiLaserPool_Upload);
		FGuLiPerformanceScope Timing(GetWorld(), TEXT("Laser.UploadMs"));
		using Arrays = UNiagaraDataInterfaceArrayFunctionLibrary;
		Arrays::SetNiagaraArrayPosition(Block.Component, TEXT("User.LaserPositions"), Block.Positions);
		Arrays::SetNiagaraArrayVector(Block.Component, TEXT("User.LaserDirections"), Block.Directions);
		Arrays::SetNiagaraArrayVector2D(Block.Component, TEXT("User.LaserSizes"), Block.Sizes);
		Arrays::SetNiagaraArrayColor(Block.Component, TEXT("User.LaserColors"), Block.Colors);
		Arrays::SetNiagaraArrayPosition(Block.Component, TEXT("User.LaserLightPositions"), Block.LightPositions);
		Arrays::SetNiagaraArrayColor(Block.Component, TEXT("User.LaserLightColors"), Block.LightColors);
		Arrays::SetNiagaraArrayFloat(Block.Component, TEXT("User.LaserLightRadii"), Block.LightRadii);
		Arrays::SetNiagaraArrayBool(Block.Component, TEXT("User.LaserLightEnabled"), Block.LightEnabled);
		Arrays::SetNiagaraArrayPosition(Block.Component, TEXT("User.MuzzlePositions"), Block.MuzzlePositions);
		Arrays::SetNiagaraArrayVector2D(Block.Component, TEXT("User.MuzzleSizes"), Block.MuzzleSizes);
		Arrays::SetNiagaraArrayColor(Block.Component, TEXT("User.MuzzleColors"), Block.MuzzleColors);
		Counters.NiagaraArrayUploads += 11;
		if (auto* Capture=GetWorld()->GetSubsystem<UGuLiPerformanceSubsystem>()) Capture->Record(TEXT("Laser.ArrayUploads"),11);
		}
		const auto Bounds=Block.Bounds.ExpandBy(FMath::Max(Catalog->LaserCoreWidth*3.f*Block.BaseScale.Y,20.f));
		if (bActivate || Block.UploadedBounds!=Bounds) { Block.Component->SetSystemFixedBounds(Bounds); Block.UploadedBounds=Bounds; }
		if (bActivate) Block.Component->Activate(true);
	}
}
