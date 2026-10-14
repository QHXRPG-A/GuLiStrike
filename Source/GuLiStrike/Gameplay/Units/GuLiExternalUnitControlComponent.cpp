#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "Commander/Orders/GuLiUnitTaskSubsystem.h"
#include "Engine/World.h"
#include "Components/MeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Network/GuLiNetworkSettings.h"
#include "Gameplay/Units/GuLiExternalControlRegistry.h"
#include "Gameplay/Skills/GuLiSkillTargeting.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Materials/MaterialInterface.h"

UGuLiExternalUnitControlComponent::UGuLiExternalUnitControlComponent()
{
	SetIsReplicatedByDefault(false);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UGuLiExternalUnitControlComponent::BeginPlay()
{
    Super::BeginPlay();const auto* B=UGuLiHybridActorComponent::Find(GetOwner());if (!B) return;
    auto* Registry=GetWorld()->GetSubsystem<UGuLiExternalControlRegistry>();TWeakObjectPtr<UGuLiExternalUnitControlComponent> Self(this);
    if (!Registry || !Registry->Bind(B->GetObjectKey(),*this,[Self](FGuLiExternalBody& Out)
    {
        auto* C=Self.Get();auto* A=C?C->GetOwner():nullptr;if (!A) return false;
        Out.Transform=A->GetActorTransform();Out.Actor=A;Out.bGroundPivot=false;Out.bVehicle=C->bPlayerVehicle;Out.bAirborne=C->bAirborne;Out.bLocked=C->AreActionsLocked();
        if (const auto* Character=Cast<ACharacter>(A)) Character->GetCapsuleComponent()->GetScaledCapsuleSize(Out.Radius,Out.HalfHeight);
        FVector Ground;double Height=0;if (!GuLiSkillTargeting::ResolveGround(*A->GetWorld(),A->GetActorLocation(),Ground,&Height)) return false;
        Out.Altitude=C->bAirborne?float(A->GetActorLocation().Z-Height):Out.HalfHeight+3;return true;
    },[Self](const FGuLiExternalControl& In)
    {
        auto* C=Self.Get();if (!C || (C->State.bActionsLocked && C->State.OwnerToken!=In.Token)) return false;
        if (In.bLocked || In.bDisplace)
            if (const auto* Vehicle=Cast<IGuLiEngineeringVehicle>(C->GetOwner()))
                if (auto* Tasks=C->GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>())
                    Tasks->CancelPendingMove(FGuLiTaskUnitId::Actor(Vehicle->GetStableActorId(),C->GetWorld()));
        auto S=C->State;S.OwnerToken=In.Token;S.bPhased=In.bPhased;S.bActionsLocked=In.bLocked;
        if (!C->State.bActionsLocked && In.bLocked) if (const auto* Character=Cast<ACharacter>(C->GetOwner())) S.RestoreMovementMode=Character->GetCharacterMovement()->MovementMode;
        if (In.bDisplace) {S.Baseline=In.Transform;++S.DisplacementRevision;}
        ++S.Revision;if (In.bPhased) S.PhaseMaterial=GuLiVfx::Load<UMaterialInterface>(C,GuLiVfxIds::TeleportBody);
        C->ApplyHybridState(S);return true;
    })) GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()->Suspend(TEXT("External control endpoint registration failed"));
}
bool UGuLiExternalUnitControlComponent::IsActorPhased(const AActor* Actor)
{
	const auto* C = Actor ? Actor->FindComponentByClass<UGuLiExternalUnitControlComponent>() : nullptr;
	return C && C->IsPhased();
}
bool UGuLiExternalUnitControlComponent::AreActorActionsLocked(const AActor* Actor)
{
	const auto* C = Actor ? Actor->FindComponentByClass<UGuLiExternalUnitControlComponent>() : nullptr;
	return C && C->AreActionsLocked();
}
bool UGuLiExternalUnitControlComponent::ApplyOwnedState(FGuid Token, bool bPhased, bool bLocked,
	const FTransform& Baseline, bool bDisplace, UMaterialInterface* PhaseMaterial)
{
	if (!UGuLiHybridActorComponent::HasSimulation(GetOwner()) || !Token.IsValid() || Baseline.ContainsNaN()
		|| (State.bActionsLocked && State.OwnerToken != Token)) { return false; }
	if (bLocked || bDisplace)
		if (const auto* Vehicle = Cast<IGuLiEngineeringVehicle>(GetOwner()))
			if (auto* Tasks = GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>())
				Tasks->CancelPendingMove(FGuLiTaskUnitId::Actor(Vehicle->GetStableActorId(),GetWorld()));
	if (!State.bActionsLocked && (bLocked || bDisplace))
	{
		if (const auto* Character = Cast<ACharacter>(GetOwner()))
		{ State.RestoreMovementMode = Character->GetCharacterMovement()->MovementMode; }
	}
	State.OwnerToken = Token; State.bPhased = bPhased; State.bActionsLocked = bLocked;
	if (PhaseMaterial) { State.PhaseMaterial = PhaseMaterial; }
	if (bDisplace) { State.Baseline = Baseline; ++State.DisplacementRevision; }
	++State.Revision;
	ApplyState();
    return true;
}
void UGuLiExternalUnitControlComponent::ApplyHybridState(const FGuLiExternalUnitControlState& InState)
{
    if (!UGuLiNetworkSettings::IsHybrid(GetWorld()) || !UGuLiHybridActorComponent::Find(GetOwner())) return;
    State=InState; ApplyState();
}
void UGuLiExternalUnitControlComponent::RestoreHybridState(const FGuLiExternalUnitControlState& InState,const FGuLiExternalControlRestoreSettings& Settings)
{
    // The family already restored the current pose. Its most recent teleport
    // baseline may be older than that pose and must not be displaced again.
    AppliedDisplacementRevision=InState.DisplacementRevision;
    bCapturedSettings=Settings.Captured;bPreviousCollision=Settings.Collision;bPreviousCanBeDamaged=Settings.CanBeDamaged;
    ApplyHybridState(InState);
}
void UGuLiExternalUnitControlComponent::ApplyLocalPhaseAppearance(bool bPhased, UMaterialInterface* Material)
{
	State.bPhased = bPhased;
	if (Material) { State.PhaseMaterial = Material; }
	ApplyState();
}
void UGuLiExternalUnitControlComponent::ApplyMaterials()
{
	if (!UGuLiNetworkSettings::CanPresent(GetWorld()) || !State.PhaseMaterial) { return; }
	if (AppliedPhaseMaterial && AppliedPhaseMaterial != State.PhaseMaterial) { RestoreMaterials(); }
	AppliedPhaseMaterial = State.PhaseMaterial;
	TInlineComponentArray<UMeshComponent*> Meshes(GetOwner());
	for (auto* Mesh : Meshes)
	{
		if (!Mesh || SavedMaterials.ContainsByPredicate([Mesh](const auto& Entry) { return Entry.Mesh == Mesh; })) { continue; }
		auto& Entry = SavedMaterials.AddDefaulted_GetRef(); Entry.Mesh = Mesh;
		for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
		{ Entry.Materials.Add(Mesh->GetMaterial(Slot)); Mesh->SetMaterial(Slot,AppliedPhaseMaterial); }
	}
}
void UGuLiExternalUnitControlComponent::RestoreMaterials()
{
	for (const auto& Entry : SavedMaterials)
	{
		if (auto* Mesh = Entry.Mesh.Get())
		{
			for (int32 Slot = 0; Slot < Entry.Materials.Num(); ++Slot)
			{
				// A different effect may have replaced a slot while this state was active.
				if (Mesh->GetMaterial(Slot) == AppliedPhaseMaterial) { Mesh->SetMaterial(Slot, Entry.Materials[Slot]); }
			}
		}
	}
	SavedMaterials.Reset(); AppliedPhaseMaterial = nullptr;
}
void UGuLiExternalUnitControlComponent::ApplyState()
{
	AActor* Actor = GetOwner(); if (!Actor) { return; }
	if (!bCapturedSettings && (State.bActionsLocked || State.bPhased))
	{
		bPreviousCollision = Actor->GetActorEnableCollision(); bPreviousCanBeDamaged = Actor->CanBeDamaged();
		bCapturedSettings = true;
	}
	if (bCapturedSettings && State.bPhased)
	{
		Actor->SetActorEnableCollision(false);
		Actor->SetCanBeDamaged(false);
	}
	if (State.bPhased) { ApplyMaterials(); } else { RestoreMaterials(); }
	auto* Character = Cast<ACharacter>(Actor);
	auto* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (State.DisplacementRevision != AppliedDisplacementRevision)
	{
		AppliedDisplacementRevision = State.DisplacementRevision;
		const auto* Binding=UGuLiHybridActorComponent::Find(Actor);
		const auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
        if (Hybrid && Binding && Hybrid->HasSimulation(Binding->GetObjectKey()))
		{
			if (auto* Target = Cast<IGuLiExternalDisplacementTarget>(Movement)) { Target->ApplyExternalDisplacement(State.Baseline); }
			else { Actor->SetActorTransform(State.Baseline,false,nullptr,ETeleportType::TeleportPhysics); }
		}
		else
		{
			Actor->SetActorTransform(State.Baseline,false,nullptr,ETeleportType::TeleportPhysics);
			if (Movement && Character->IsLocallyControlled())
			{
				auto* P = Movement->GetPredictionData_Client_Character();
				P->SavedMoves.Reset(); P->PendingMove.Reset(); P->LastAckedMove.Reset(); P->bUpdatePosition = false;
				P->MeshTranslationOffset = P->OriginalMeshTranslationOffset = FVector::ZeroVector;
				P->MeshRotationOffset = P->OriginalMeshRotationOffset = P->MeshRotationTarget = FQuat::Identity;
			}
		}
		if (Movement) { Movement->StopMovementImmediately(); Character->SetBase(nullptr); Movement->bJustTeleported = true; }
	}
	if (Movement && (State.bActionsLocked || bCapturedSettings))
	{
		Character->ConsumeMovementInputVector(); Movement->StopMovementImmediately();
		Movement->SetMovementMode(State.bActionsLocked ? MOVE_None : EMovementMode(State.RestoreMovementMode));
	}
	if (bCapturedSettings && !State.bPhased)
	{
		// Restore at the committed position so source overlap callbacks cannot hit
		// a participant in the brief interval before its displacement is applied.
		Actor->SetActorEnableCollision(bPreviousCollision);
		Actor->SetCanBeDamaged(bPreviousCanBeDamaged);
	}
	SetComponentTickEnabled(State.bActionsLocked || State.bPhased);
	if (!State.bActionsLocked && !State.bPhased) { bCapturedSettings = false; }
	OnStateApplied.Broadcast();
}
void UGuLiExternalUnitControlComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
	if (State.bPhased) { ApplyMaterials(); }
	if (State.bActionsLocked)
	{
		if (auto* Character = Cast<ACharacter>(GetOwner()))
		{ Character->ConsumeMovementInputVector(); Character->GetCharacterMovement()->StopMovementImmediately(); Character->GetCharacterMovement()->DisableMovement(); }
	}
}
void UGuLiExternalUnitControlComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (const auto* B=UGuLiHybridActorComponent::Find(GetOwner())) if (auto* R=GetWorld()->GetSubsystem<UGuLiExternalControlRegistry>()) R->Unbind(B->GetObjectKey(),*this);
	RestoreMaterials(); Super::EndPlay(Reason);
}
