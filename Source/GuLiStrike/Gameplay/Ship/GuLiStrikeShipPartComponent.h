// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "GuLiStrikeShipPartComponent.generated.h"

class AGuLiStrikeShip;
class UMaterialInterface;
class UMeshComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class UStaticMesh;

UENUM(BlueprintType)
enum class EGuLiStrikeShipPartVisualType : uint8
{
	StaticMesh,
	SkeletalMesh
};

/** 装配统计聚合：各部件把自己的数值贡献进来，飞船只做汇总与应用 */
USTRUCT(BlueprintType)
struct FGuLiStrikeShipStats
{
	GENERATED_BODY()

	/** 全部部件的质量和 */
	float PartMassSum = 0.0f;

	/** 全部引擎部件的推力和 */
	float ThrustSum = 0.0f;
};

/**
 *  所有可安装飞船部件的基类。
 *  具体部件由蓝图子类配置：网格、数值，以及允许接入的舰体 socket 集合。
 *  部件行为（数值贡献、开火等）写在部件类里，装上即拥有；
 *  ContributeStats/Fire 为 BlueprintNativeEvent——蓝图子类可直接重写实现新行为。
 */
UCLASS(Abstract, BlueprintType, Blueprintable, ClassGroup=(Ship), meta=(BlueprintSpawnableComponent))
class GULISTRIKE_API UGuLiStrikeShipPartComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	/** 安装点和部件行为由本组件管理；网格由独立的视觉子组件显示。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship Part|Visual")
	EGuLiStrikeShipPartVisualType VisualType = EGuLiStrikeShipPartVisualType::StaticMesh;

	/** 保留原 UStaticMeshComponent 的属性名和类型，使旧蓝图的网格引用可以迁移。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship Part|Visual", meta=(EditCondition="VisualType == EGuLiStrikeShipPartVisualType::StaticMesh", EditConditionHides))
	TObjectPtr<UStaticMesh> StaticMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship Part|Visual", meta=(EditCondition="VisualType == EGuLiStrikeShipPartVisualType::SkeletalMesh", EditConditionHides))
	TObjectPtr<USkeletalMesh> SkeletalMesh;

	/** 同样保留旧材质覆盖数组的属性名；空项沿用网格自身的材质。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship Part|Visual")
	TArray<TObjectPtr<UMaterialInterface>> OverrideMaterials;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship Part|Visual")
	bool CastShadow = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship Part|Visual", AdvancedDisplay)
	bool bReceivesDecals = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship Part|Visual", AdvancedDisplay)
	bool bRenderCustomDepth = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship Part|Visual", AdvancedDisplay, meta=(ClampMin="0", ClampMax="255"))
	int32 CustomDepthStencilValue = 0;

	/** 部件数值表的行键（一般 = 部件蓝图资产名）；空 = 不走数据表，安装时沿用蓝图默认数值 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship Part")
	FName PartId;

	/** 本部件允许安装的舰体 socket 名集合（一个部件可接入多个槽位） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship Part")
	TArray<FName> CompatibleSockets;

	/** 挂接到 socket 之后叠加的变换微调（位置/朝向/缩放） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship Part")
	FTransform PartRelativeTransform;

	/** 安装后为飞船增加的质量 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship Part", meta=(ClampMin = 0, Units = "kg"))
	float PartMass = 10.0f;

	/** 配装/调试界面显示的部件名 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship Part")
	FText PartDisplayName;

	/** 构造函数 */
	UGuLiStrikeShipPartComponent();

	UFUNCTION(BlueprintPure, Category="Ship Part|Visual")
	UMeshComponent* GetVisualMeshComponent() const { return VisualMeshComponent; }

	UFUNCTION(BlueprintPure, Category="Ship Part|Visual")
	USkeletalMeshComponent* GetSkeletalVisualComponent() const;

	UFUNCTION(BlueprintPure, Category="Ship Part|Visual")
	UStaticMesh* GetStaticMesh() const { return StaticMesh; }

	UFUNCTION(BlueprintCallable, Category="Ship Part|Visual")
	bool SetStaticMesh(UStaticMesh* NewMesh);

	UFUNCTION(BlueprintCallable, Category="Ship Part|Visual")
	bool SetPartSkeletalMesh(USkeletalMesh* NewMesh);

	UFUNCTION(BlueprintCallable, Category="Ship Part|Visual")
	void SetMaterial(int32 ElementIndex, UMaterialInterface* Material);

	UFUNCTION(BlueprintPure, Category="Ship Part|Visual")
	UMaterialInterface* GetMaterial(int32 ElementIndex) const;

	/** 重新建立视觉子组件，不修改安装父项、socket、部件变换或任何网格资产。 */
	UFUNCTION(BlueprintCallable, Category="Ship Part|Visual")
	bool RebuildVisualMesh();

	bool IsVisualMeshReady() const;

	/** Socket 变换相对于部件容器，可与服务器权威部件变换相乘。缺失时返回 false。 */
	UFUNCTION(BlueprintPure, Category="Ship Part|Visual")
	bool GetVisualSocketTransform(FName SocketName, FTransform& OutTransform) const;

	virtual FTransform GetSocketTransform(FName InSocketName, ERelativeTransformSpace TransformSpace = RTS_World) const override;
	virtual bool DoesSocketExist(FName InSocketName) const override;
	virtual bool HasAnySockets() const override;
	virtual void QuerySupportedSockets(TArray<FComponentSocketDescription>& OutSockets) const override;

	/** 本部件是否允许安装在指定的舰体 socket 上 */
	UFUNCTION(BlueprintPure, Category="Ship Part")
	bool CanAttachToSocket(FName SocketName) const;

	/** 把本部件的数值贡献进聚合统计（C++ 默认贡献质量；蓝图/C++ 子类重写叠加自己的贡献） */
	UFUNCTION(BlueprintNativeEvent, Category="Ship Part")
	void ContributeStats(UPARAM(ref) FGuLiStrikeShipStats& OutStats);

	/** 响应开火输入（C++ 默认不响应；武器子类重写为出弹）——行为属于组件，装上即拥有 */
	UFUNCTION(BlueprintNativeEvent, Category="Ship Part")
	void Fire(AActor* Instigator);

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void OnVisibilityChanged() override;
	virtual void OnHiddenInGameChanged() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** 部件销毁时通知宿主飞船同步注册表（部件可能被外部直接销毁，如部件击毁玩法） */
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

private:
	/** Actor 拥有的临时视觉组件；本部件注销/销毁时显式清理，避免换装后残留。 */
	UPROPERTY(Transient, DuplicateTransient)
	TObjectPtr<UMeshComponent> VisualMeshComponent;

	void DestroyVisualMesh();
};
