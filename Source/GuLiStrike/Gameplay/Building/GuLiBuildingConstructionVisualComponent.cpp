#include "Gameplay/Building/GuLiBuildingConstructionVisualComponent.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Gameplay/Building/GuLiBuildingVisuals.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "Components/ChildActorComponent.h"
#include "Components/LightComponent.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/Actor.h"
#include "Gameplay/Presentation/GuLiUnitRenderPolicy.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "TimerManager.h"

UGuLiBuildingConstructionVisualComponent::UGuLiBuildingConstructionVisualComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UGuLiBuildingConstructionVisualComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetOwner()->GetNetMode() == NM_DedicatedServer) return;
	Lifecycle = GetOwner()->FindComponentByClass<UGuLiBuildingLifecycleComponent>();
	if (Lifecycle) StateChangedHandle = Lifecycle->OnConstructionStateChanged.AddUObject(this, &ThisClass::RefreshState);
	RefreshState();
}

void UGuLiBuildingConstructionVisualComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Lifecycle) Lifecycle->OnConstructionStateChanged.Remove(StateChangedHandle);
	GetWorld()->GetTimerManager().ClearTimer(SourceRetryTimer);
	ClearPresentation();
	Super::EndPlay(Reason);
}

void UGuLiBuildingConstructionVisualComponent::RefreshVisualSources()
{
	if (!HasBegunPlay() || GetOwner()->GetNetMode() == NM_DedicatedServer) return;
	StopTopEffect();
	ClearMeshes(); // A model rebind must neither kill nor replay an in-flight completion burst.
	bAssetsFailed = false;
	RefreshState();
}

void UGuLiBuildingConstructionVisualComponent::RefreshState()
{
	if (!Lifecycle || !Lifecycle->GetState().InstanceId || GetOwner()->GetNetMode() == NM_DedicatedServer) return;
	const auto& State = Lifecycle->GetState();
	const auto* Settings = GetDefault<UGuLiBuildingConstructionSettings>();
	if (ObservedInstance != State.InstanceId)
	{
		ClearPresentation();
		ObservedInstance = State.InstanceId;
		bObservedUnderConstruction = false;
		bCompletionPlayed = false;
	}
	if (!Settings->bEnabled || State.Phase == EGuLiBuildingPhase::Destroyed)
	{
		ClearPresentation();
		return;
	}
	if (State.Phase == EGuLiBuildingPhase::Completed)
	{
		if (bObservedUnderConstruction && !bCompletionPlayed)
		{
			bCompletionPlayed = true; // Set before playing; model rebinding and duplicate notifications are harmless.
			StopTopEffect();
			PlayCompletionEffect();
			if (bConstructing) FinishConstruction();
		}
		return; // A completed initial snapshot never replays the finishing effect.
	}
	bObservedUnderConstruction = true;
	if (!bConstructing && !BeginConstruction()) return;
	InterpolationStart = DisplayedProgress;
	InterpolationElapsed = 0.0f;
	TargetProgress = FMath::Max(DisplayedProgress, Lifecycle->GetConstructionProgress());
	UpdateTopEffect();
	SetComponentTickEnabled(TargetProgress > DisplayedProgress);
}

bool UGuLiBuildingConstructionVisualComponent::BeginConstruction()
{
	if (bAssetsFailed) return false;
	const auto* Settings = GetDefault<UGuLiBuildingConstructionSettings>();
	auto* Hologram = GuLiVfx::Load<UMaterialInterface>(this, Settings->HologramVfxId);
	auto* Finish = GuLiVfx::Load<UMaterialInterface>(this, Settings->FinishGlowVfxId);
	if (!Hologram || !Finish)
	{
		bAssetsFailed = true;
		return false;
	}
	TArray<AActor*> Actors{GetOwner()};
	GetOwner()->GetAllChildActors(Actors, true);
	Actors.AddUnique(GetOwner());
	TArray<UMeshComponent*> Sources;
	FBox Bounds(ForceInit);
	for (AActor* Actor : Actors)
	{
		TInlineComponentArray<UMeshComponent*> Meshes(Actor);
		for (UMeshComponent* Mesh : Meshes)
			if (Mesh->IsVisible() && !Mesh->bHiddenInGame && !Mesh->ComponentHasTag(TEXT("GuLiConstructionProxy")))
			{
				const auto* Static = Cast<UStaticMeshComponent>(Mesh);
				const auto* Skeletal = Cast<USkeletalMeshComponent>(Mesh);
				if (!(Static && Static->GetStaticMesh()) && !(Skeletal && Skeletal->GetSkeletalMeshAsset())) continue;
				Sources.Add(Mesh);
				Bounds += Mesh->Bounds.GetBox();
			}
	}
	if (Sources.IsEmpty() || !Bounds.IsValid)
	{
		if (!GetWorld()->GetTimerManager().IsTimerActive(SourceRetryTimer))
			GetWorld()->GetTimerManager().SetTimer(SourceRetryTimer, this, &ThisClass::RefreshState, .2f, true);
		return false;
	}
	GetWorld()->GetTimerManager().ClearTimer(SourceRetryTimer);
	HologramMaterial = UMaterialInstanceDynamic::Create(Hologram, this);
	FinishMaterial = UMaterialInstanceDynamic::Create(Finish, this);
	HologramMaterial->SetScalarParameterValue(TEXT("EffectAlpha"), 1.0f);
	FinishMaterial->SetScalarParameterValue(TEXT("EffectAlpha"), 0.0f);
	auto MakeRoot = [&]()
	{
		auto* Root = NewObject<USceneComponent>(GetOwner(), NAME_None, RF_Transient);
		Root->SetMobility(EComponentMobility::Movable);
		Root->SetupAttachment(GetOwner()->GetRootComponent());
		GetOwner()->AddInstanceComponent(Root);
		Root->RegisterComponent();
		return Root;
	};
	SolidRoot = MakeRoot(); GhostRoot = MakeRoot();
	for (UMeshComponent* Source : Sources)
	{
		const FTransform Relative = Source->GetComponentTransform().GetRelativeTransform(GetOwner()->GetActorTransform());
		auto* Solid = GuLiBuildingVisuals::CopyMesh(*GetOwner(), *SolidRoot, *Source, Relative, nullptr, true);
		auto* Ghost = GuLiBuildingVisuals::CopyMesh(*GetOwner(), *GhostRoot, *Source, Relative, HologramMaterial, true);
		if (!Solid || !Ghost)
		{
			if (Solid) Solid->DestroyComponent();
			if (Ghost) Ghost->DestroyComponent();
			continue;
		}
		Solids.Add(Solid); Ghosts.Add(Ghost);
		OriginalVisibility.Add({Source, Source->IsVisible()});
		Source->SetVisibility(false, false);
	}
	if (Solids.IsEmpty()) { ClearPresentation(); return false; }
	for (AActor* Actor : Actors)
	{
		TInlineComponentArray<ULightComponent*> Lights(Actor);
		for (ULightComponent* Light : Lights)
		{
			OriginalVisibility.Add({Light, Light->IsVisible()});
			Light->SetVisibility(false, false);
		}
	}
	ModelTop = float(Bounds.Max.Z);
	RiseHeight = FMath::Max(1.0f, ModelTop - float(Lifecycle->GetGroundLocation().Z) + 1.0f);
	ResolveShape();
	DisplayedProgress = TargetProgress = Lifecycle->GetConstructionProgress();
	bConstructing = true;
	ApplyProgress();
	return true;
}

void UGuLiBuildingConstructionVisualComponent::ApplyProgress()
{
	if (SolidRoot) SolidRoot->SetRelativeLocation(GetOwner()->GetActorTransform().InverseTransformVector(
		FVector(0, 0, -RiseHeight * (1.0f - DisplayedProgress))));
	for (UMeshComponent* Solid : Solids) Solid->SetVisibility(DisplayedProgress > 0.0f);
	UpdateTopEffect();
}

void UGuLiBuildingConstructionVisualComponent::RestoreOriginals()
{
	for (const auto& Entry : OriginalVisibility)
		if (auto* Component = Entry.Component.Get()) Component->SetVisibility(Entry.bVisible, false);
	OriginalVisibility.Reset();
}

void UGuLiBuildingConstructionVisualComponent::FinishConstruction()
{
	bConstructing = false; bFinishing = true;
	DisplayedProgress = TargetProgress = 1.0f;
	RestoreOriginals();
	for (UMeshComponent* Solid : Solids) if (Solid) Solid->DestroyComponent();
	Solids.Reset();
	if (SolidRoot) SolidRoot->DestroyComponent();
	SolidRoot = nullptr;
	for (UMeshComponent* Ghost : Ghosts)
		for (int32 Slot = 0; Slot < Ghost->GetNumMaterials(); ++Slot) Ghost->SetMaterial(Slot, FinishMaterial);
	FinishMaterial->SetScalarParameterValue(TEXT("EffectAlpha"), 1.0f);
	FinishElapsed = 0.0f;
	SetComponentTickEnabled(true);
}

void UGuLiBuildingConstructionVisualComponent::TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* Function)
{
	Super::TickComponent(Dt, TickType, Function);
	if (bFinishing)
	{
		FinishElapsed += Dt;
		const float Alpha = 1.0f - FMath::Clamp(FinishElapsed / FMath::Max(0.01f, GetDefault<UGuLiBuildingConstructionSettings>()->FinishSeconds), 0.0f, 1.0f);
		FinishMaterial->SetScalarParameterValue(TEXT("EffectAlpha"), Alpha);
		if (Alpha <= 0.0f) ClearMeshes(); // The longer bottom burst finishes on its own.
	}
	else if (bConstructing)
	{
		InterpolationElapsed += Dt;
		DisplayedProgress = FMath::Lerp(InterpolationStart, TargetProgress, FMath::Clamp(InterpolationElapsed / 0.1f, 0.0f, 1.0f));
		ApplyProgress();
		if (InterpolationElapsed >= 0.1f) SetComponentTickEnabled(false);
	}
}

void UGuLiBuildingConstructionVisualComponent::ClearPresentation()
{
	GetWorld()->GetTimerManager().ClearTimer(SourceRetryTimer);
	StopTopEffect();
	if (IsValid(CompletionEffect)) CompletionEffect->DestroyComponent();
	CompletionEffect = nullptr;
	ClearMeshes();
}

void UGuLiBuildingConstructionVisualComponent::ClearMeshes()
{
	RestoreOriginals();
	for (UMeshComponent* Mesh : Solids) if (Mesh) Mesh->DestroyComponent();
	for (UMeshComponent* Mesh : Ghosts) if (Mesh) Mesh->DestroyComponent();
	Solids.Reset(); Ghosts.Reset();
	if (SolidRoot) SolidRoot->DestroyComponent();
	if (GhostRoot) GhostRoot->DestroyComponent();
	SolidRoot = nullptr; GhostRoot = nullptr;
	HologramMaterial = nullptr; FinishMaterial = nullptr;
	Shape = nullptr; WorldContours.Reset(); ContourLengths.Reset();
	EffectPoints.Reset(); EffectTangents.Reset(); EffectLengths.Reset();
	++ShapeRevision;
	bConstructing = false; bFinishing = false;
	SetComponentTickEnabled(false);
}
