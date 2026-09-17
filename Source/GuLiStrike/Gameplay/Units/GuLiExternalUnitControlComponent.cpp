#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Components/MeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"

UGuLiExternalUnitControlComponent::UGuLiExternalUnitControlComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}
void UGuLiExternalUnitControlComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UGuLiExternalUnitControlComponent, State);
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
bool UGuLiExternalUnitControlComponent::ApplyServerState(FGuid Token, bool bPhased, bool bLocked,
	const FTransform& Baseline, bool bDisplace, UMaterialInterface* PhaseMaterial)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Token.IsValid() || Baseline.ContainsNaN()
		|| (State.bActionsLocked && State.OwnerToken != Token)) { return false; }
	if (!State.bActionsLocked && bLocked)
	{
		if (const auto* Character = Cast<ACharacter>(GetOwner()))
		{ State.RestoreMovementMode = Character->GetCharacterMovement()->MovementMode; }
	}
	State.OwnerToken = Token; State.bPhased = bPhased; State.bActionsLocked = bLocked;
	if (PhaseMaterial) { State.PhaseMaterial = PhaseMaterial; }
	if (bDisplace) { State.Baseline = Baseline; ++State.DisplacementRevision; }
	++State.Revision;
	ApplyState(); GetOwner()->ForceNetUpdate();
	// Existing channels receive the discontinuity promptly even while initial troop
	// replication consumes bandwidth. The retained property still serves late join.
	MulticastState(State); return true;
}
void UGuLiExternalUnitControlComponent::OnRep_State(FGuLiExternalUnitControlState Previous)
{
	// An older property update must not undo an already received reliable event.
	if (int32(State.Revision-Previous.Revision) < 0) { State = Previous; return; }
	ApplyState();
}
void UGuLiExternalUnitControlComponent::MulticastState_Implementation(FGuLiExternalUnitControlState NewState)
{
	if (!GetOwner() || GetOwner()->HasAuthority() || int32(NewState.Revision-State.Revision) < 0) { return; }
	State = NewState; ApplyState();
}
void UGuLiExternalUnitControlComponent::ApplyLocalPhaseAppearance(bool bPhased, UMaterialInterface* Material)
{
	State.bPhased = bPhased;
	if (Material) { State.PhaseMaterial = Material; }
	ApplyState();
}
void UGuLiExternalUnitControlComponent::ApplyMaterials()
{
	if (GetNetMode() == NM_DedicatedServer || !State.PhaseMaterial) { return; }
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
		if (Actor->HasAuthority())
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
	if (Movement)
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
	RestoreMaterials(); Super::EndPlay(Reason);
}
