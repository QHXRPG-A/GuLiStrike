// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BlinkVFX.generated.h"

class ACharacter;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPoseableMeshComponent;
class UStaticMeshComponent;

/** 闪现的短生命周期特效：冻结角色姿势作为残影，并播放向外扩张的热波。 */
UCLASS()
class GULISTRIKE_API ABlinkVFX : public AActor
{
	GENERATED_BODY()

public:
	ABlinkVFX();

	/** 需要残影时，复制来源角色的当前骨骼姿势。 */
	void InitializeFromCharacter(
		ACharacter* SourceCharacter,
		bool bShowAfterimage,
		bool bShowHeatwave = true,
		float InitialAfterimageOpacity = 0.55f,
		float InLifetime = 0.38f);

	virtual void Tick(float DeltaTime) override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Blink VFX")
	/** 保存角色瞬间姿势的骨骼网格组件。 */
	TObjectPtr<UPoseableMeshComponent> AfterimageMesh;

	UPROPERTY(VisibleAnywhere, Category = "Blink VFX")
	/** 承载折射材质并向外扩张的球形网格组件。 */
	TObjectPtr<UStaticMeshComponent> HeatwaveMesh;

	UPROPERTY()
	/** 残影的基础材质；运行时会由它创建动态材质实例。 */
	TObjectPtr<UMaterialInterface> AfterimageBaseMaterial;

	UPROPERTY()
	/** 热波的基础材质；运行时会由它创建动态材质实例。 */
	TObjectPtr<UMaterialInterface> HeatwaveBaseMaterial;

	UPROPERTY(Transient)
	/** 每个残影材质槽对应的动态材质实例，用于同步淡出。 */
	TArray<TObjectPtr<UMaterialInstanceDynamic>> AfterimageMaterials;

	UPROPERTY(Transient)
	/** 热波的动态材质实例，用于控制透明度。 */
	TObjectPtr<UMaterialInstanceDynamic> HeatwaveMaterial;

	/** 已播放的时间与完整特效持续时间，单位为秒。 */
	float ElapsedTime = 0.0f;
	float AfterimageOpacity = 0.55f;
	float Lifetime = 0.38f;
};
