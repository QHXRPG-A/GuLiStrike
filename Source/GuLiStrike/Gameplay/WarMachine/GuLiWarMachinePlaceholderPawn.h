// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GuLiWarMachinePlaceholderPawn.generated.h"

class UCameraComponent;
class UInputComponent;
class USpringArmComponent;
class UStaticMeshComponent;

/**
 * Ground 角色的最小原生占位 Pawn：使用 CharacterMovement 完成基础行走及 UE 移动复制。
 * 只验证公共战局身份、操控和复活接入；尚无战争机器武器或完整载具规则。
 */
UCLASS()
class GULISTRIKE_API AGuLiWarMachinePlaceholderPawn : public ACharacter
{
	GENERATED_BODY()

public:
	AGuLiWarMachinePlaceholderPawn();

	// 拥有客户端重新占有时设置本地输入模式；不操作其他玩家视口，也不发送玩法 RPC。
	virtual void PawnClientRestart() override;

	/** 本端输入资格；服务器出生/席位权限仍由 BattleGameMode 与公共握手分别维护。 */
	UFUNCTION(BlueprintPure, Category = "WarMachine|Input")
	bool CanAcceptGroundInput() const;

	/** 武器尚未实现，任何角色/网络模式下均返回 false。 */
	UFUNCTION(BlueprintPure, Category = "WarMachine|Weapon")
	bool CanFire() const;

	/** 占位武器入口：始终拒绝，不生成投射物、不修改战斗状态、不发送 RPC。 */
	UFUNCTION(BlueprintCallable, Category = "WarMachine|Weapon")
	bool RequestFire();

protected:
	virtual void SetupPlayerInputComponent(UInputComponent* IncomingInputComponent) override;

private:
	void HandleMoveForward(float AxisValue);
	void HandleMoveBackward(float AxisValue);
	void HandleMoveRight(float AxisValue);
	void HandleMoveLeft(float AxisValue);
	void HandleLookYaw(float AxisValue);
	void HandleLookPitch(float AxisValue);
	void ApplyMoveInput(float AxisValue, bool bForwardAxis);

	UPROPERTY(VisibleAnywhere, Category = "WarMachine|Presentation")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, Category = "WarMachine|Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, Category = "WarMachine|Camera")
	TObjectPtr<UCameraComponent> FollowCamera;
};
