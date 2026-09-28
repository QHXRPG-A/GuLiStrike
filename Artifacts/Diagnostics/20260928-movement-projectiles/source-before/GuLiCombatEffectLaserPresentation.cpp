#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectPresentationSubsystem.h"

#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

namespace { constexpr int32 LaserBlockSize = 1024; }

int32 UGuLiCombatEffectPresentationSubsystem::AllocateLaserSlot(const int32 VfxId)
{
	const FVector BaseScale = GuLiVfx::Scale(this, VfxId);
	if (!UGuLiVfxRegistrySubsystem::IsValidScale(BaseScale)) return INDEX_NONE;
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
	FreeLaserSlots.FindOrAdd(Block.VfxId).Add(Slot);
}

void UGuLiCombatEffectPresentationSubsystem::ResetLaserPool()
{
	for (auto& Block : LaserBlocks) if (IsValid(Block.Component))
	{ Block.Component->DeactivateImmediate(); Block.Component->ReleaseToPool(); }
	LaserBlocks.Reset(); FreeLaserSlots.Reset();
}

void UGuLiCombatEffectPresentationSubsystem::UpdateLaserPool(const float Now, const bool bEnabled)
{
	Counters.LaserCapacity = LaserBlocks.Num() * LaserBlockSize;
	Counters.LaserActive = Counters.LaserCapacity; Counters.LaserVisible = 0;
	for (const auto& Pair : FreeLaserSlots) Counters.LaserActive -= Pair.Value.Num();
	for (auto& Block : LaserBlocks)
	{
		Block.Colors.Init(FLinearColor::Transparent, LaserBlockSize); Block.MuzzleColors.Init(FLinearColor::Transparent, LaserBlockSize);
		Block.Sizes.Init(FVector2D::ZeroVector, LaserBlockSize); Block.MuzzleSizes.Init(FVector2D::ZeroVector, LaserBlockSize);
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
	if (bEnabled && Catalog) for (auto& Pair : Visuals)
	{
		auto& Visual = Pair.Value; const auto& State = Visual.State;
		if (State.Kind != EGuLiCombatEffectKind::LinearProjectile || Visual.LaserSlot == INDEX_NONE) continue;
		const bool bFinished = State.Phase == EGuLiCombatEffectPhase::Finished;
		const bool bGround = State.Source.Kind == EGuLiTargetKind::CommanderSoldier;
		const float Age = FMath::Clamp(Now - State.StartTime, 0.0f, State.EndTime - State.StartTime);
		const FVector Head = bFinished ? FVector(State.Location) : FVector(State.LaunchLocation) + FVector(State.Velocity) * Age;
		Visual.RenderLocation = Head;
		const bool bBoltVisible = IsVisibleLocation(Head);
		FVector Muzzle = State.LaunchLocation;
		FGuLiCombatShotCue Cue; Cue.Source = State.Source; Cue.MuzzleOffset = State.MuzzleOffset;
		const bool bMuzzleVisible = !bGround && LocalNow < Visual.LaserMuzzleUntil && ResolveMuzzlePosition(Cue, Muzzle) && IsVisibleLocation(Muzzle);
		if (!bBoltVisible && !bMuzzleVisible) continue;
		auto& Block = LaserBlocks[Visual.LaserSlot / LaserBlockSize]; const int32 Index = Visual.LaserSlot % LaserBlockSize;
		const FVector Direction = FVector(State.LaunchDirection).GetSafeNormal();
		const float AuthoredLength = Catalog->LaserLength * Block.BaseScale.X;
		const float Length = FMath::Min(AuthoredLength, static_cast<float>(FVector::Distance(Head, State.LaunchLocation)));
		FLinearColor Tint = LocalTeam == EGuLiTeam::Unassigned ? FLinearColor::White
			: (State.SourceTeam == LocalTeam
				? (bGround ? Catalog->FriendlyGroundMachineGunTint : Catalog->FriendlyLaserTint)
				: (bGround ? Catalog->EnemyGroundMachineGunTint : Catalog->EnemyLaserTint));
		const float Fade = bFinished ? FMath::Clamp((Visual.LaserFadeUntil - LocalNow) / 0.04f, 0.0f, 1.0f) : 1.0f;
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
	for (auto& Block : LaserBlocks)
	{
		if (!Block.bVisible)
		{
			if (IsValid(Block.Component)) { Block.Component->DeactivateImmediate(); Block.Component->ReleaseToPool(); Block.Component = nullptr; }
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
			Block.Component->SetCastShadow(false); bActivate = true;
		}
		using Arrays = UNiagaraDataInterfaceArrayFunctionLibrary;
		Arrays::SetNiagaraArrayPosition(Block.Component, TEXT("User.LaserPositions"), Block.Positions);
		Arrays::SetNiagaraArrayVector(Block.Component, TEXT("User.LaserDirections"), Block.Directions);
		Arrays::SetNiagaraArrayVector2D(Block.Component, TEXT("User.LaserSizes"), Block.Sizes);
		Arrays::SetNiagaraArrayColor(Block.Component, TEXT("User.LaserColors"), Block.Colors);
		Arrays::SetNiagaraArrayPosition(Block.Component, TEXT("User.MuzzlePositions"), Block.MuzzlePositions);
		Arrays::SetNiagaraArrayVector2D(Block.Component, TEXT("User.MuzzleSizes"), Block.MuzzleSizes);
		Arrays::SetNiagaraArrayColor(Block.Component, TEXT("User.MuzzleColors"), Block.MuzzleColors);
		Block.Component->SetSystemFixedBounds(Block.Bounds.ExpandBy(FMath::Max(Catalog->LaserCoreWidth * 3.0f * Block.BaseScale.Y, 20.0f)));
		if (bActivate) Block.Component->Activate(true);
	}
}
