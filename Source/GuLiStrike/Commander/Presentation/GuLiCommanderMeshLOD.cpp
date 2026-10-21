#include "Commander/Presentation/GuLiCommanderLODSubsystem.h"

#include "Commander/Presentation/GuLiCommanderOverviewSubsystem.h"
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
#include "Gameplay/Building/GuLiBuildingPlacementPreview.h"
#include "Gameplay/CombatEffects/GuLiUnitWreck.h"
#include "Gameplay/Wingman/Presentation/GuLiWingmanPresentationActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkinnedAsset.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
TAutoConsoleVariable<int32> CVarCommanderMeshLOD(TEXT("gs.Commander.MeshLOD"), 1,
	TEXT("Apply four-tier mesh precision and LOD0-only model outlines. 0 restores component settings and outline materials."));

bool IsModelOutlineSlot(const UMeshComponent& Component, int32 Index)
{
	// Dedicated contour materials are distinct from body line-art textures, placement
	// guides and selection rings. Include slot names so temporary hit/ghost materials
	// cannot turn the contour shell into a visible solid object in a reduced tier.
	const auto Slots = Component.GetMaterialSlotNames();
	const auto* Material = Component.GetMaterial(Index);
	const FString Name = (Slots.IsValidIndex(Index) ? Slots[Index].ToString() : FString())
		+ TEXT(" ") + GetPathNameSafe(Material);
	return Name.Contains(TEXT("Contour")) || Name.Contains(TEXT("Outline"))
		|| Name.Contains(TEXT("InvertedHull")) || Name.Contains(TEXT("Ink_Backfaces"));
}

bool IsPresentationActor(const AActor* Actor)
{
	return IsValid(Actor) && (UGuLiCommanderOverviewSubsystem::IsUnitOrBuilding(Actor)
		|| Actor->IsA<AGuLiCommanderPresentationActor>() || Actor->IsA<AGuLiWingmanPresentationActor>()
		|| Actor->IsA<AGuLiBuildingPlacementPreview>() || Actor->IsA<AGuLiUnitWreck>());
}

bool HasPresentationOwner(const AActor* Actor)
{
	if (IsPresentationActor(Actor)) return true;
	TSet<const AActor*> Visited;
	for (const AActor* Parent = Actor ? Actor->GetAttachParentActor() : nullptr;
		Parent && !Visited.Contains(Parent); Parent = Parent->GetAttachParentActor())
	{
		if (IsPresentationActor(Parent)) return true;
		Visited.Add(Parent);
	}
	Visited.Reset();
	for (const AActor* Owner = Actor ? Actor->GetOwner() : nullptr;
		Owner && !Visited.Contains(Owner); Owner = Owner->GetOwner())
	{
		if (IsPresentationActor(Owner)) return true;
		Visited.Add(Owner);
	}
	return false;
}
}

void UGuLiCommanderLODSubsystem::NotifyActorChanged(AActor* Actor)
{
	if (!IsValid(Actor) || Actor->IsTemplate() || !Actor->GetWorld()) return;
	if (auto* Policy = Actor->GetWorld()->GetSubsystem<UGuLiCommanderLODSubsystem>())
		Policy->PendingMeshActors.AddUnique(Actor);
}

void UGuLiCommanderLODSubsystem::NotifyMeshChanged(UMeshComponent* Component)
{
	if (!IsValid(Component) || !Component->GetWorld()) return;
	if (auto* Policy = Component->GetWorld()->GetSubsystem<UGuLiCommanderLODSubsystem>())
		Policy->RegisterMesh(Component);
}

void UGuLiCommanderLODSubsystem::ForgetActor(AActor* Actor)
{
	if (Actor && Actor->GetWorld())
		if (auto* Policy = Actor->GetWorld()->GetSubsystem<UGuLiCommanderLODSubsystem>())
			Policy->UnregisterActorMeshes(Actor);
}

void UGuLiCommanderLODSubsystem::ForgetMesh(UMeshComponent* Component)
{
	if (Component && Component->GetWorld())
		if (auto* Policy = Component->GetWorld()->GetSubsystem<UGuLiCommanderLODSubsystem>())
			Policy->UnregisterMesh(Component);
}

UMaterialInterface* UGuLiCommanderLODSubsystem::GetMaterialForPresentationCopy(
	const UMeshComponent& Component, int32 Index)
{
	if (const UWorld* World = Component.GetWorld())
		if (const auto* Policy = World->GetSubsystem<UGuLiCommanderLODSubsystem>())
			if (const auto* Entry = Policy->MeshEntries.Find(&Component))
				if (const auto* Slot = Entry->OutlineOverrides.Find(Index)) return Slot->Resolved.Get();
	return Component.GetMaterial(Index);
}

void UGuLiCommanderLODSubsystem::RegisterActorMeshes(AActor* Actor)
{
	if (!IsValid(Actor) || Actor->GetWorld() != GetWorld()) return;
	// Prune at membership changes, never by a steady-state world/component scan.
	for (auto It = MeshEntries.CreateIterator(); It; ++It)
		if (!It.Key().IsValid() || (It.Key()->GetOwner() == Actor
			&& (!It.Key()->IsRegistered() || !HasPresentationOwner(Actor))))
		{
			RestoreMeshEntry(It.Value());
			It.RemoveCurrent();
			++MeshMembershipChanges;
		}
	if (!HasPresentationOwner(Actor)) return;
	TInlineComponentArray<UMeshComponent*> Components;
	Actor->GetComponents(Components, true);
	for (UMeshComponent* Component : Components) RegisterMesh(Component);
}

void UGuLiCommanderLODSubsystem::RegisterMesh(UMeshComponent* Component)
{
	if (!IsValid(Component) || Component->IsTemplate() || Component->GetWorld() != GetWorld()
		|| !Component->IsRegistered() || !HasPresentationOwner(Component->GetOwner())) return;
	UObject* Resource = nullptr;
	auto* Static = Cast<UStaticMeshComponent>(Component);
	auto* Skin = Cast<USkinnedMeshComponent>(Component);
	if (Static) Resource = Static->GetStaticMesh();
	else if (Skin) Resource = Skin->GetSkinnedAsset();
	else return;
	const bool Nanite = Static && Static->GetStaticMesh()
		&& Static->GetStaticMesh()->NaniteSettings.bEnabled && !Static->bDisallowNanite;
	if (auto* Existing = MeshEntries.Find(Component))
	{
		if (Existing->Resource.Get() == Resource && Existing->bNanite == Nanite)
		{
			ApplyModelOutline(*Existing, !ShouldShowModelOutlines());
			return;
		}
		RestoreMeshEntry(*Existing);
		MeshEntries.Remove(Component);
	}
	if (!Resource) return;
	auto& Entry = MeshEntries.Add(Component);
	Entry.Component = Component;
	Entry.Resource = Resource;
	Entry.OriginalOverrideCount = Component->OverrideMaterials.Num();
	if (Static)
	{
		Entry.OriginalMinLOD = Static->MinLOD;
		Entry.OriginalForcedLOD = Static->GetForcedLodModel();
		Entry.bOriginalOverride = Static->bOverrideMinLOD;
		Entry.LODCount = Static->GetStaticMesh()->GetNumLODs();
		Entry.ResourceMinLOD = Static->GetStaticMesh()->GetMinLODIdx();
		Entry.bNanite = Nanite;
	}
	else
	{
		Entry.OriginalMinLOD = Skin->MinLodModel;
		Entry.OriginalForcedLOD = Skin->GetForcedLOD();
		Entry.bOriginalOverride = Skin->bOverrideMinLod;
		Entry.LODCount = Skin->GetNumLODs();
		Entry.ResourceMinLOD = Skin->GetSkinnedAsset()->GetMinLodIdx();
	}
	++MeshMembershipChanges;
	ApplyMeshEntry(Entry, bMeshPolicyEnabled && GuLiPresentationLOD::ReduceMesh(AppliedMeshTier));
}

void UGuLiCommanderLODSubsystem::RestoreMeshEntry(FMeshEntry& Entry)
{
	ApplyModelOutline(Entry, false);
	if (!Entry.bApplied) return;
	if (auto* Static = Cast<UStaticMeshComponent>(Entry.Component.Get()))
	{
		Static->MinLOD = Entry.OriginalMinLOD;
		Static->bOverrideMinLOD = Entry.bOriginalOverride;
		Static->SetForcedLodModel(Entry.OriginalForcedLOD);
		Static->MarkRenderStateDirty();
	}
	else if (auto* Skin = Cast<USkinnedMeshComponent>(Entry.Component.Get()))
	{
		Skin->MinLodModel = Entry.OriginalMinLOD;
		Skin->bOverrideMinLod = Entry.bOriginalOverride;
		Skin->SetForcedLOD(Entry.OriginalForcedLOD);
	}
	Entry.bApplied = false;
	++MeshStateChanges;
}

void UGuLiCommanderLODSubsystem::ApplyMeshEntry(FMeshEntry& Entry, const bool bReduce)
{
	if (!bReduce) { RestoreMeshEntry(Entry); return; }
	// Missing MeshLOD1 or Nanite only affects geometry selection. The runtime tier's
	// outline rule still applies to these models and their assembled components.
	ApplyModelOutline(Entry, true);
	if (Entry.bApplied || Entry.bNanite || Entry.LODCount < 2) return;
	const int32 Minimum = FMath::Max(1, Entry.bOriginalOverride ? Entry.OriginalMinLOD : Entry.ResourceMinLOD);
	if (auto* Static = Cast<UStaticMeshComponent>(Entry.Component.Get()))
	{
		Static->MinLOD = Minimum;
		Static->bOverrideMinLOD = true;
		if (Entry.OriginalForcedLOD > 0) Static->SetForcedLodModel(FMath::Max(2, Entry.OriginalForcedLOD));
		Static->MarkRenderStateDirty();
	}
	else if (auto* Skin = Cast<USkinnedMeshComponent>(Entry.Component.Get()))
	{
		Skin->OverrideMinLOD(Minimum);
		if (Entry.OriginalForcedLOD > 0) Skin->SetForcedLOD(FMath::Max(2, Entry.OriginalForcedLOD));
	}
	else return;
	Entry.bApplied = true;
	++MeshStateChanges;
}

void UGuLiCommanderLODSubsystem::ApplyModelOutline(FMeshEntry& Entry, const bool bHide)
{
	auto* Component = Entry.Component.Get();
	if (!Component) { Entry.OutlineOverrides.Reset(); return; }
	if (!bHide)
	{
		for (const auto& Pair : Entry.OutlineOverrides)
			if (Component->GetMaterial(Pair.Key) == NoModelOutlineMaterial)
				Component->SetMaterial(Pair.Key, Pair.Value.Override.Get());
		if (!Entry.OutlineOverrides.IsEmpty())
		{
			// Preserve changes made by other presentation owners while suppressed.
			while (Component->OverrideMaterials.Num() > Entry.OriginalOverrideCount
				&& Component->OverrideMaterials.Last() == nullptr)
				Component->OverrideMaterials.Pop(EAllowShrinking::No);
			++MeshStateChanges;
		}
		Entry.OutlineOverrides.Reset();
		return;
	}
	if (!NoModelOutlineMaterial)
		NoModelOutlineMaterial = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/GuLiStrike/Presentation/Materials/M_NoModelOutline.M_NoModelOutline"));
	if (!NoModelOutlineMaterial) return;
	for (int32 Index = 0; Index < Component->GetNumMaterials(); ++Index)
	{
		if (Component->GetMaterial(Index) == NoModelOutlineMaterial) continue;
		if (!Entry.OutlineOverrides.Contains(Index) && !IsModelOutlineSlot(*Component, Index)) continue;
		// An event can replace a team/hit material while the same mesh stays installed.
		// Retain that newest override for restoration, not an obsolete team palette.
		UMaterialInterface* Original = Component->OverrideMaterials.IsValidIndex(Index)
			? Component->OverrideMaterials[Index] : nullptr;
		auto& Slot = Entry.OutlineOverrides.FindOrAdd(Index);
		Slot.Override.Reset(Original);
		Slot.Resolved.Reset(Component->GetMaterial(Index));
		Component->SetMaterial(Index, NoModelOutlineMaterial);
		++MeshStateChanges;
	}
}

void UGuLiCommanderLODSubsystem::UnregisterMesh(UMeshComponent* Component)
{
	if (auto* Entry = MeshEntries.Find(Component)) RestoreMeshEntry(*Entry);
	if (MeshEntries.Remove(Component)) ++MeshMembershipChanges;
}

void UGuLiCommanderLODSubsystem::UnregisterActorMeshes(AActor* Actor)
{
	PendingMeshActors.Remove(Actor);
	for (auto It = MeshEntries.CreateIterator(); It; ++It)
		if (!It.Key().IsValid() || It.Key()->GetOwner() == Actor)
		{
			RestoreMeshEntry(It.Value());
			It.RemoveCurrent();
			++MeshMembershipChanges;
		}
}

TStatId UGuLiCommanderLODSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(GuLiCommanderMeshLOD, STATGROUP_Tickables);
}

void UGuLiCommanderLODSubsystem::Tick(float)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCommanderMeshLOD_Update);
	EnsureViews();
	EGuLiPresentationTier Target = EGuLiPresentationTier::Overview;
	bool bAllCommander = !Views.IsEmpty();
	for (const auto& View : Views)
	{
		if (!View.PresentationTier.IsSet()) { bAllCommander = false; break; }
		if (uint8(View.PresentationTier.GetValue()) < uint8(Target)) Target = View.PresentationTier.GetValue();
	}
	if (!bAllCommander) Target = EGuLiPresentationTier::Full;
	RequestedMeshTier = Target;
	const bool Enabled = CVarCommanderMeshLOD.GetValueOnGameThread() != 0;
	if (!Enabled) Target = EGuLiPresentationTier::Full;
	const double Now = GetWorld()->GetTimeSeconds();
	const bool CanChange = !bMeshPolicyInitialized || !Enabled || !bAllCommander || Now < LastMeshTierChangeSeconds
		|| Now - LastMeshTierChangeSeconds >= EffectiveSettings.MinimumResidenceSeconds;
	if (CanChange && (!bMeshPolicyInitialized || Target != AppliedMeshTier || Enabled != bMeshPolicyEnabled))
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCommanderMeshLOD_Apply);
		const bool Reduce = Enabled && GuLiPresentationLOD::ReduceMesh(Target);
		for (auto It = MeshEntries.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid()) { It.RemoveCurrent(); continue; }
			ApplyMeshEntry(It.Value(), Reduce);
		}
		AppliedMeshTier = Target;
		bMeshPolicyEnabled = Enabled;
		LastMeshTierChangeSeconds = Now;
		bMeshPolicyInitialized = true;
	}
	if (!PendingMeshActors.IsEmpty())
	{
		TArray<TWeakObjectPtr<AActor>> Pending = MoveTemp(PendingMeshActors);
		PendingMeshActors.Reset();
		for (const auto& Actor : Pending) RegisterActorMeshes(Actor.Get());
	}
}

FString UGuLiCommanderLODSubsystem::GetMeshLODStatsJson() const
{
	auto Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("requested_tier"), uint8(RequestedMeshTier));
	Root->SetNumberField(TEXT("applied_tier"), uint8(AppliedMeshTier));
	Root->SetBoolField(TEXT("enabled"), bMeshPolicyEnabled);
	Root->SetBoolField(TEXT("model_outlines"), ShouldShowModelOutlines());
	Root->SetBoolField(TEXT("outline_mask_loaded"), NoModelOutlineMaterial != nullptr);
	Root->SetNumberField(TEXT("mesh_reduced_start_height_m"), EffectiveSettings.MeshReducedStartHeightMeters);
	Root->SetNumberField(TEXT("membership_changes"), double(MeshMembershipChanges));
	Root->SetNumberField(TEXT("state_changes"), double(MeshStateChanges));
	TArray<TSharedPtr<FJsonValue>> Rows;
	for (const auto& Pair : MeshEntries)
	{
		const auto& Entry = Pair.Value;
		const auto* Component = Entry.Component.Get();
		if (!Component) continue;
		auto Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("component"), Component->GetPathName());
		Row->SetStringField(TEXT("resource"), GetPathNameSafe(Entry.Resource.Get()));
		Row->SetNumberField(TEXT("resource_lods"), Entry.LODCount);
		Row->SetBoolField(TEXT("mesh_cap_applied"), Entry.bApplied);
		Row->SetNumberField(TEXT("suppressed_outline_slots"), Entry.OutlineOverrides.Num());
		Row->SetStringField(TEXT("fallback"), Entry.bNanite ? TEXT("nanite") : Entry.LODCount < 2 ? TEXT("no_mesh_lod1") : TEXT(""));
		Row->SetNumberField(TEXT("original_min_lod"), Entry.OriginalMinLOD);
		Row->SetNumberField(TEXT("original_forced_lod"), Entry.OriginalForcedLOD);
		Row->SetBoolField(TEXT("original_override"), Entry.bOriginalOverride);
		if (const auto* Static = Cast<UStaticMeshComponent>(Component))
		{
			Row->SetNumberField(TEXT("min_lod"), Static->MinLOD);
			Row->SetNumberField(TEXT("forced_lod"), Static->GetForcedLodModel());
			Row->SetBoolField(TEXT("override_min_lod"), Static->bOverrideMinLOD);
		}
		else if (const auto* Skin = Cast<USkinnedMeshComponent>(Component))
		{
			Row->SetNumberField(TEXT("min_lod"), Skin->MinLodModel);
			Row->SetNumberField(TEXT("forced_lod"), Skin->GetForcedLOD());
			Row->SetBoolField(TEXT("override_min_lod"), Skin->bOverrideMinLod);
		}
		Rows.Add(MakeShared<FJsonValueObject>(Row));
	}
	Root->SetArrayField(TEXT("components"), Rows);
	FString Result;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Result));
	return Result;
}
