#include "Gameplay/Models/GuLiLocalTeamColorSubsystem.h"
#include "Gameplay/Models/GuLiModelRegistrySubsystem.h"
#include "Gameplay/Presentation/GuLiLocalTeamColors.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Components/MeshComponent.h"
#include "Commander/UI/GuLiSceneUISourceRegistry.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "UObject/UObjectIterator.h"
#include "Gameplay/Presentation/GuLiTeamOutlineComponent.h"
#include "GameFramework/PlayerController.h"

namespace
{
uint32 TeamPrimitiveDataHash(const UMeshComponent* Component)
{
	const auto& Data=Component->GetCustomPrimitiveData().Data;
	uint32 Hash=0;
	for (int32 Index=8; Index<18; ++Index)
		Hash=HashCombine(Hash,GetTypeHash(Data.IsValidIndex(Index) ? Data[Index] : 0.f));
	return Hash;
}
}

bool UGuLiLocalTeamColorSubsystem::IsTickable() const
{ return !IsTemplate() && GetWorld() && GetWorld()->IsGameWorld() && GetWorld()->GetNetMode() != NM_DedicatedServer; }
TStatId UGuLiLocalTeamColorSubsystem::GetStatId() const
{ RETURN_QUICK_DECLARE_CYCLE_STAT(GuLiLocalTeamColors, STATGROUP_Tickables); }
void UGuLiLocalTeamColorSubsystem::Deinitialize()
{
	if (Identity.IsValid()) Identity->OnCommanderPlayerStateChanged.RemoveDynamic(this,&ThisClass::HandleIdentityChanged);
	Identity.Reset(); Entries.Reset(); BoundWorld.Reset(); Super::Deinitialize();
}
void UGuLiLocalTeamColorSubsystem::HandleIdentityChanged()
{ RefreshRemaining = 0; for (auto& Pair : Entries) Pair.Value.bDirty = true; ApplyLocalTeamColors(); }
void UGuLiLocalTeamColorSubsystem::Register(const UObject* Context, UMeshComponent* C, int32 Id, const FString& Part, EGuLiTeam Team)
{
	const auto* W = Context ? Context->GetWorld() : nullptr;
	if (!W || W->GetNetMode() == NM_DedicatedServer) return;
	if (const auto* Game = W->GetGameInstance()) for (auto* Player : Game->GetLocalPlayers())
		if (auto* S = Player->GetSubsystem<UGuLiLocalTeamColorSubsystem>()) S->RegisterModelComponent(C,Id,Part,Team);
}
void UGuLiLocalTeamColorSubsystem::RegisterModelComponent(UMeshComponent* C, int32 Id, const FString& Part, EGuLiTeam Team, bool Candidate)
{
	if (!C || !GetWorld() || C->GetWorld() != GetWorld() || (Candidate && GetWorld()->IsGameWorld())) return;
	auto* Registry = GetWorld()->GetSubsystem<UGuLiModelRegistrySubsystem>();
	if (!Registry) return;
	FGuLiStrikeModelsModelsRow D;
	if (!Registry->GetModelDefinition(Id,D) || !D.bTeamColorEnabled) { Entries.Remove(C); return; }
	if (auto* Existing = Entries.Find(C); Existing && Existing->ModelId == Id && Existing->PartKey == Part && Existing->ExplicitTeam == Team && Existing->bCandidate == Candidate) return;
	UGuLiSceneUISourceRegistry::Notify(C->GetOwner(), EGuLiSceneUIChange::Membership);
	auto& E = Entries.FindOrAdd(C); E = {}; E.Component=C; E.ModelId=Id; E.PartKey=Part; E.ExplicitTeam=Team; E.Definition=D; E.bCandidate=Candidate;
	E.Bindings = Registry->GetMaterialParameters(Id,Candidate).FilterByPredicate([&](const auto& B)
	{ return B.bTeamManaged && (B.PartKey == Part || (B.Driver == TEXT("CPD") && B.PartKey == TEXT("Root") && B.MaterialSlotName == TEXT("*"))); });
	Refresh(E,GuLiLocalTeamColors::GetViewTeam(GetLocalPlayer()->GetPlayerController(GetWorld())));
}
void UGuLiLocalTeamColorSubsystem::UnregisterModelComponent(UMeshComponent* C) { if (Entries.Remove(C) && C) UGuLiSceneUISourceRegistry::Notify(C->GetOwner(), EGuLiSceneUIChange::Membership); }
void UGuLiLocalTeamColorSubsystem::Refresh(FEntry& E, EGuLiTeam View)
{
	auto* C = E.Component.Get(); if (!C) return;
	const EGuLiTeam Team = GuLiLocalTeamColors::IsAssigned(E.ExplicitTeam) ? E.ExplicitTeam : GuLiLocalTeamColors::GetActorTeam(C->GetOwner());
	uint32 Hash=0; for (int32 I=0; I<C->GetNumMaterials(); ++I) Hash=HashCombine(Hash,PointerHash(C->GetMaterial(I)));
	if (!E.bDirty && E.LastTeam == Team && E.MaterialHash == Hash && E.PrimitiveDataHash == TeamPrimitiveDataHash(C)) return;
	if (E.LastTeam != Team) UGuLiSceneUISourceRegistry::Notify(C->GetOwner(), EGuLiSceneUIChange::Membership);
	const bool Assigned = GuLiLocalTeamColors::IsAssigned(Team) && GuLiLocalTeamColors::IsAssigned(View);
	const bool Friendly = Assigned && Team == View;
	FLinearColor Primary = FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#2C3735")));
	FLinearColor Secondary = Primary;
	UGuLiModelRegistrySubsystem::ParseColor(Assigned ? (Friendly ? E.Definition.BluePrimaryHex : E.Definition.EnemyPrimaryHex) : TEXT("#2C3735"),Primary);
	UGuLiModelRegistrySubsystem::ParseColor(Assigned ? (Friendly ? E.Definition.BlueSecondaryHex : E.Definition.EnemySecondaryHex) : TEXT("#2C3735"),Secondary);
	for (const auto& B : E.Bindings)
	{
		if (B.ParameterType == TEXT("Vector")) UGuLiModelRegistrySubsystem::WriteVector(C,B,B.ParameterKey == TEXT("TeamSecondary") ? Secondary : Primary,true);
		else UGuLiModelRegistrySubsystem::WriteScalar(C,B,B.ParameterKey == TEXT("TeamEnabled") ? 1.f : (Assigned ? B.DefaultScalar : 0.f),true);
	}
	E.bDirty=false; E.LastTeam=Team;
	E.MaterialHash=0; for (int32 I=0; I<C->GetNumMaterials(); ++I) E.MaterialHash=HashCombine(E.MaterialHash,PointerHash(C->GetMaterial(I)));
	E.PrimitiveDataHash=TeamPrimitiveDataHash(C);
}
void UGuLiLocalTeamColorSubsystem::ApplyLocalTeamColors()
{
	if (!GetWorld()) return;
	const auto View = GuLiLocalTeamColors::GetViewTeam(GetLocalPlayer()->GetPlayerController(GetWorld()));
	if (View != LastViewTeam) { for (auto& Pair : Entries) Pair.Value.bDirty=true; LastViewTeam=View; }
	for (auto It=Entries.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || It.Key()->GetWorld() != GetWorld()) { It.RemoveCurrent(); continue; }
		Refresh(It.Value(),View);
	}
}
void UGuLiLocalTeamColorSubsystem::Tick(float Delta)
{
	if (BoundWorld.Get() != GetWorld())
	{
		if (Identity.IsValid()) Identity->OnCommanderPlayerStateChanged.RemoveDynamic(this,&ThisClass::HandleIdentityChanged);
		Identity.Reset(); BoundWorld=GetWorld();
		for (auto It=Entries.CreateIterator(); It; ++It) if (!It.Key().IsValid() || It.Key()->GetWorld()!=GetWorld()) It.RemoveCurrent();
		LastViewTeam=EGuLiTeam::Unassigned;
	}
	auto* PC=GetLocalPlayer()->GetPlayerController(GetWorld());
	auto* State=PC ? PC->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
	if (Identity.Get()!=State)
	{
		if (Identity.IsValid()) Identity->OnCommanderPlayerStateChanged.RemoveDynamic(this,&ThisClass::HandleIdentityChanged);
		Identity=State; if (State) State->OnCommanderPlayerStateChanged.AddUniqueDynamic(this,&ThisClass::HandleIdentityChanged);
		HandleIdentityChanged();
	}
	RefreshRemaining-=Delta;
	DiscoveryRemaining-=Delta;
	if (DiscoveryRemaining<=0)
	{
		DiscoveryRemaining=.5f;
		auto* Registry=GetWorld()->GetSubsystem<UGuLiModelRegistrySubsystem>();
		for (TObjectIterator<UMeshComponent> It; It; ++It)
		{
			auto* C=*It; auto* Owner=C->GetOwner();
			if (C->GetWorld()!=GetWorld() || !Owner || !C->IsRegistered() || Entries.Contains(C) || C->IsA<UInstancedStaticMeshComponent>()) continue;
			if (!Owner->GetClass()->GetName().StartsWith(TEXT("GuLi")) && !Owner->FindComponentByClass<UGuLiTeamOutlineComponent>()) continue;
			UObject* Resource=nullptr;
			if (auto* Static=Cast<UStaticMeshComponent>(C)) Resource=Static->GetStaticMesh();
			else if (auto* Skeletal=Cast<USkeletalMeshComponent>(C)) Resource=Skeletal->GetSkeletalMeshAsset();
			// Recreated Blueprint components must retain their assembly's catalogue
			// binding; a child mesh can be a fixed-color leaf when used on its own.
			const int32 AssemblyId=Registry->FindModelIdForResource(Owner->GetClass());
			if (AssemblyId)
			{
				bool Found=false;
				for (const auto& Part : Registry->GetModelParts(AssemblyId))
					if (UGuLiModelRegistrySubsystem::FindPart(Owner,Part.ComponentPath)==C)
					{ RegisterModelComponent(C,AssemblyId,Part.PartKey); Found=true; break; }
				if (Found) continue;
			}
			const int32 Id=Registry->FindModelIdForResource(Resource);
			if (Id) RegisterModelComponent(C,Id);
		}
	}
	if (RefreshRemaining<=0) { ApplyLocalTeamColors(); RefreshRemaining=.1f; }
}
