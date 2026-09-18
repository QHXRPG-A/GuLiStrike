// Copyright Epic Games, Inc. All Rights Reserved.

#include "GuLiStrikeShipPartComponent.h"
#include "GuLiStrikeShip.h"
#include "GuLiStrike.h"
#include "Gameplay/Data/GuLiObjectScale.h"
#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UGuLiStrikeShipPartComponent::UGuLiStrikeShipPartComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetMobility(EComponentMobility::Movable);
}

void UGuLiStrikeShipPartComponent::OnRegister()
{
	Super::OnRegister();
	RebuildVisualMesh();
}

void UGuLiStrikeShipPartComponent::OnUnregister()
{
	DestroyVisualMesh();
	Super::OnUnregister();
}

void UGuLiStrikeShipPartComponent::OnVisibilityChanged()
{
	Super::OnVisibilityChanged();
	if (IsValid(VisualMeshComponent)) { VisualMeshComponent->SetVisibility(IsVisible(), true); }
}

void UGuLiStrikeShipPartComponent::OnHiddenInGameChanged()
{
	Super::OnHiddenInGameChanged();
	if (IsValid(VisualMeshComponent)) { VisualMeshComponent->SetHiddenInGame(bHiddenInGame, true); }
}

void UGuLiStrikeShipPartComponent::DestroyVisualMesh()
{
	UMeshComponent* Previous = VisualMeshComponent;
	VisualMeshComponent = nullptr;
	if (IsValid(Previous)) { Previous->DestroyComponent(); }
}

bool UGuLiStrikeShipPartComponent::RebuildVisualMesh()
{
	if (IsTemplate() || !IsRegistered() || !GetOwner() || !GetWorld()) { return false; }
	DestroyVisualMesh();
	UMeshComponent* Visual = nullptr;
	if (VisualType == EGuLiStrikeShipPartVisualType::StaticMesh && StaticMesh)
	{
		UStaticMeshComponent* StaticVisual = NewObject<UStaticMeshComponent>(GetOwner(), NAME_None, RF_Transient);
		StaticVisual->SetStaticMesh(StaticMesh);
		Visual = StaticVisual;
	}
	else if (VisualType == EGuLiStrikeShipPartVisualType::SkeletalMesh && SkeletalMesh)
	{
		USkeletalMeshComponent* SkeletalVisual = NewObject<USkeletalMeshComponent>(GetOwner(), NAME_None, RF_Transient);
		SkeletalVisual->SetSkeletalMeshAsset(SkeletalMesh);
		// Socket 必须反映服务器当前姿态，即使服务器没有渲染此部件。
		SkeletalVisual->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Visual = SkeletalVisual;
	}
	if (!Visual) { return false; }
	VisualMeshComponent = Visual;
	Visual->SetMobility(EComponentMobility::Movable);
	Visual->SetupAttachment(this);
	Visual->SetRelativeTransform(FTransform::Identity);
	Visual->SetCollisionProfileName(TEXT("NoCollision"));
	Visual->SetGenerateOverlapEvents(false);
	Visual->SetCastShadow(CastShadow);
	Visual->SetReceivesDecals(bReceivesDecals);
	Visual->SetRenderCustomDepth(bRenderCustomDepth);
	Visual->SetCustomDepthStencilValue(CustomDepthStencilValue);
	Visual->SetVisibility(IsVisible());
	Visual->SetHiddenInGame(bHiddenInGame);
	for (int32 Index = 0; Index < OverrideMaterials.Num(); ++Index)
	{
		if (OverrideMaterials[Index]) { Visual->SetMaterial(Index, OverrideMaterials[Index]); }
	}
	Visual->RegisterComponentWithWorld(GetWorld());
	GuLiObjectScale::ApplyOutlineScale(Visual);
	if (!Visual->IsRegistered()) { DestroyVisualMesh(); return false; }
	return true;
}

bool UGuLiStrikeShipPartComponent::IsVisualMeshReady() const
{
	return IsValid(VisualMeshComponent) && VisualMeshComponent->IsRegistered();
}

USkeletalMeshComponent* UGuLiStrikeShipPartComponent::GetSkeletalVisualComponent() const
{
	return Cast<USkeletalMeshComponent>(VisualMeshComponent);
}

bool UGuLiStrikeShipPartComponent::SetStaticMesh(UStaticMesh* NewMesh)
{
	StaticMesh = NewMesh;
	VisualType = EGuLiStrikeShipPartVisualType::StaticMesh;
	return !IsRegistered() || RebuildVisualMesh();
}

bool UGuLiStrikeShipPartComponent::SetPartSkeletalMesh(USkeletalMesh* NewMesh)
{
	SkeletalMesh = NewMesh;
	VisualType = EGuLiStrikeShipPartVisualType::SkeletalMesh;
	return !IsRegistered() || RebuildVisualMesh();
}

void UGuLiStrikeShipPartComponent::SetMaterial(int32 ElementIndex, UMaterialInterface* Material)
{
	if (ElementIndex < 0) { return; }
	if (OverrideMaterials.Num() <= ElementIndex) { OverrideMaterials.SetNum(ElementIndex + 1); }
	OverrideMaterials[ElementIndex] = Material;
	if (IsValid(VisualMeshComponent)) { VisualMeshComponent->SetMaterial(ElementIndex, Material); }
}

UMaterialInterface* UGuLiStrikeShipPartComponent::GetMaterial(int32 ElementIndex) const
{
	return IsValid(VisualMeshComponent) ? VisualMeshComponent->GetMaterial(ElementIndex)
		: (OverrideMaterials.IsValidIndex(ElementIndex) ? OverrideMaterials[ElementIndex].Get() : nullptr);
}

bool UGuLiStrikeShipPartComponent::GetVisualSocketTransform(FName SocketName, FTransform& OutTransform) const
{
	OutTransform = FTransform::Identity;
	if (!IsValid(VisualMeshComponent) || SocketName.IsNone() || !VisualMeshComponent->DoesSocketExist(SocketName)) { return false; }
	OutTransform = VisualMeshComponent->GetSocketTransform(SocketName, RTS_Component) * VisualMeshComponent->GetRelativeTransform();
	return true;
}

FTransform UGuLiStrikeShipPartComponent::GetSocketTransform(FName InSocketName, ERelativeTransformSpace TransformSpace) const
{
	FTransform Local;
	if (!GetVisualSocketTransform(InSocketName, Local)) { return Super::GetSocketTransform(InSocketName, TransformSpace); }
	switch (TransformSpace)
	{
	case RTS_World: return Local * GetComponentTransform();
	case RTS_Actor: return GetOwner() ? Local * GetComponentTransform().GetRelativeTransform(GetOwner()->GetActorTransform()) : Local;
	case RTS_ParentBoneSpace: return VisualMeshComponent->GetSocketTransform(InSocketName, TransformSpace);
	default: return Local;
	}
}

bool UGuLiStrikeShipPartComponent::DoesSocketExist(FName InSocketName) const
{
	return IsValid(VisualMeshComponent) && VisualMeshComponent->DoesSocketExist(InSocketName);
}

bool UGuLiStrikeShipPartComponent::HasAnySockets() const
{
	return IsValid(VisualMeshComponent) && VisualMeshComponent->HasAnySockets();
}

void UGuLiStrikeShipPartComponent::QuerySupportedSockets(TArray<FComponentSocketDescription>& OutSockets) const
{
	if (IsValid(VisualMeshComponent)) { VisualMeshComponent->QuerySupportedSockets(OutSockets); }
}

#if WITH_EDITOR
void UGuLiStrikeShipPartComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (IsRegistered()) { RebuildVisualMesh(); }
}
#endif

bool UGuLiStrikeShipPartComponent::CanAttachToSocket(FName SocketName) const
{
	return CompatibleSockets.Contains(SocketName);
}

void UGuLiStrikeShipPartComponent::ContributeStats_Implementation(FGuLiStrikeShipStats& OutStats)
{
	// 基类贡献：所有部件都增加质量
	OutStats.PartMassSum += PartMass;
}

void UGuLiStrikeShipPartComponent::Fire_Implementation(AActor* Instigator)
{
	// 非武器部件不响应开火输入；蓝图子类可重写实现自己的行为
}

void UGuLiStrikeShipPartComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	DestroyVisualMesh();
	// 通知宿主飞船同步注册表（绕过 UninstallPart 的销毁路径由此兜底）
	if (AGuLiStrikeShip* Ship = GetOwner<AGuLiStrikeShip>())
	{
		Ship->NotifyPartDestroyed(this);
	}

	Super::OnComponentDestroyed(bDestroyingHierarchy);
}
