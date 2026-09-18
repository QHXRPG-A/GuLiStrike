// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlinkVFX.h"

#include "Components/PoseableMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

ABlinkVFX::ABlinkVFX()
{
	// 特效自行 Tick 更新淡出与扩张；它不会参与碰撞或导航。
	PrimaryActorTick.bCanEverTick = true;
	SetActorEnableCollision(false);

	// 根组件只负责保存特效生成位置与朝向。
	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SceneRoot);

	// PoseableMesh 可复制角色当前骨骼姿势，随后保持静止来形成残影。
	AfterimageMesh = CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("AfterimageMesh"));
	AfterimageMesh->SetupAttachment(SceneRoot);
	AfterimageMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AfterimageMesh->SetCastShadow(false);
	AfterimageMesh->SetCanEverAffectNavigation(false);

	// 球体套用折射材质并向外放大，形成热波扩散。
	HeatwaveMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HeatwaveMesh"));
	HeatwaveMesh->SetupAttachment(SceneRoot);
	HeatwaveMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HeatwaveMesh->SetCastShadow(false);
	HeatwaveMesh->SetCanEverAffectNavigation(false);

	// 使用引擎自带球体，无需额外导入网格资源。
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		HeatwaveMesh->SetStaticMesh(SphereMesh.Object);
	}

	// 加载刚刚生成的两份材质；资源缺失时特效安全地退化为不可见。
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> AfterimageMaterial(
		TEXT("/Game/GuLiStrike/FX/M_BlinkAfterimage.M_BlinkAfterimage"));
	if (AfterimageMaterial.Succeeded())
	{
		AfterimageBaseMaterial = AfterimageMaterial.Object;
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> HeatwaveMaterialAsset(
		TEXT("/Game/GuLiStrike/FX/M_BlinkHeatwave.M_BlinkHeatwave"));
	if (HeatwaveMaterialAsset.Succeeded())
	{
		HeatwaveBaseMaterial = HeatwaveMaterialAsset.Object;
	}
}

void ABlinkVFX::InitializeFromCharacter(
	ACharacter* SourceCharacter,
	bool bShowAfterimage,
	bool bShowHeatwave,
	float InitialAfterimageOpacity,
	float InLifetime)
{
	AfterimageOpacity = FMath::Clamp(InitialAfterimageOpacity, 0.0f, 1.0f);
	Lifetime = FMath::Max(InLifetime, KINDA_SMALL_NUMBER);

	if (bShowAfterimage && SourceCharacter && SourceCharacter->GetMesh())
	{
		// 复制角色模型、相对变换和当前骨骼姿势；Actor 留在闪现起点。
		USkeletalMeshComponent* SourceMesh = SourceCharacter->GetMesh();
		AfterimageMesh->SetSkinnedAssetAndUpdate(SourceMesh->GetSkeletalMeshAsset());
		AfterimageMesh->SetRelativeTransform(SourceMesh->GetRelativeTransform());
		AfterimageMesh->CopyPoseFromSkeletalComponent(SourceMesh);

		if (AfterimageBaseMaterial)
		{
			// 所有材质槽都替换为同一种残影材质，以保证轮廓和淡出一致。
			for (int32 MaterialIndex = 0; MaterialIndex < AfterimageMesh->GetNumMaterials(); ++MaterialIndex)
			{
				if (UMaterialInstanceDynamic* Material = AfterimageMesh->CreateDynamicMaterialInstance(MaterialIndex, AfterimageBaseMaterial))
				{
					//Material->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.08f, 0.62f, 1.0f));
					Material->SetScalarParameterValue(TEXT("Opacity"), AfterimageOpacity);
					AfterimageMaterials.Add(Material);
				}
			}
		}
	}
	else
	{
		// 终点只需要热波，不显示第二个角色残影。
		AfterimageMesh->SetVisibility(false, true);
	}

	if (bShowHeatwave && HeatwaveBaseMaterial)
	{
		// 动态材质实例让每次闪现的热波可独立淡出。
		HeatwaveMaterial = HeatwaveMesh->CreateDynamicMaterialInstance(0, HeatwaveBaseMaterial);
		if (HeatwaveMaterial)
		{
			//HeatwaveMaterial->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.15f, 0.85f, 1.0f));
		}
	}
	else
	{
		HeatwaveMesh->SetVisibility(false, true);
	}

	// 热波从较小尺寸开始，在 Tick 中快速扩张。
	HeatwaveMesh->SetRelativeScale3D(FVector(0.05f));
}

void ABlinkVFX::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 用 0~1 的播放进度同时驱动残影淡出和热波扩张。
	ElapsedTime += DeltaTime;
	const float Progress = FMath::Clamp(ElapsedTime / Lifetime, 0.0f, 1.0f);
	const float Fade = 1.0f - Progress;

	for (UMaterialInstanceDynamic* Material : AfterimageMaterials)
	{
		// 残影从初始透明度平滑淡出到完全透明。
		Material->SetScalarParameterValue(TEXT("Opacity"), Fade * AfterimageOpacity);
	}

	if (HeatwaveMaterial)
	{
		// 热波和残影同步消失。
		HeatwaveMaterial->SetScalarParameterValue(TEXT("Opacity"), Fade * 0.38f);
	}

	// EaseOut 让热波先快速扩张，结束时自然减速。
	const float HeatwaveScale = FMath::InterpEaseOut(0.05f, 0.48f, Progress, 2.0f);
	HeatwaveMesh->SetRelativeScale3D(FVector(HeatwaveScale));

	if (Progress >= 1.0f)
	{
		// 生命周期结束后立即销毁，避免特效 Actor 残留在关卡中。
		Destroy();
	}
}
