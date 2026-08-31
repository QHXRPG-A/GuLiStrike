// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "Battle/Framework/GuLiBattlePlayerController.h"
#include "GuLiCommanderPlayerController.generated.h"

class UGuLiCommanderNetSyncComponent;
class AGuLiCommanderCameraPawn;

/** Client-only feedback state for the currently displayed commander move line. */
enum class EGuLiCommandLineState : uint8
{
	None,
	Pending,
	Accepted,
	Rejected
};

/** Local-only commander tool selected by keyboard or HUD input. */
enum class EGuLiCommanderToolMode : uint8
{
	Select,
	Move
};

/** Pure local-input decisions shared by the controller and automation tests. */
namespace GuLiCommanderToolPolicy
{
	inline constexpr EGuLiCommanderToolMode DefaultToolMode = EGuLiCommanderToolMode::Select;

	enum class ECancelAction : uint8
	{
		ClearSelection,
		CancelMove
	};

	bool CanArmMove(
		bool bCanIssueOrders,
		bool bHasNetSync,
		bool bHasConfirmedSelection);
	EGuLiSelectionRadiusPreset ResolveRadiusStep(
		EGuLiCommanderToolMode ToolMode,
		EGuLiSelectionRadiusPreset CurrentPreset);
	ECancelAction ResolveCancelAction(EGuLiCommanderToolMode ToolMode);
	EGuLiCommanderToolMode ResolveModeAfterMoveAttempt(
		EGuLiCommanderToolMode CurrentMode,
		bool bMoveSubmitted);
	EGuLiCommanderToolMode ResolveModeForSelectionAvailability(
		EGuLiCommanderToolMode CurrentMode,
		bool bHasConfirmedSelection);
	bool AllowsWorldIntent(bool bCursorOverCommanderUI);
}

DECLARE_MULTICAST_DELEGATE_OneParam(
	FGuLiCommanderToolModeChanged,
	EGuLiCommanderToolMode);
DECLARE_MULTICAST_DELEGATE_OneParam(
	FGuLiSelectionRadiusPresetChanged,
	EGuLiSelectionRadiusPreset);

/** 指挥官输入与连接所有者；服务器和拥有客户端各有实例，RPC 实际声明在其 NetSync 默认组件中。 */
UCLASS()
class AGuLiCommanderPlayerController : public AGuLiBattlePlayerController
{
	GENERATED_BODY()

public:
	AGuLiCommanderPlayerController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

	// 本端输入资格查询：PlayerState 为已同步的指挥官才返回 true；本地控制上下文由调用方保证，服务器仍会重验。
	UFUNCTION(BlueprintPure, Category = "Commander")
	bool CanIssueCommanderOrders() const;

	/** 本地指挥视图资格；只依赖角色，网络未就绪时仍可移动相机，但不能发命令。 */
	bool IsCommanderViewActive() const;

	UFUNCTION(BlueprintPure, Category = "Commander|Network")
	UGuLiCommanderNetSyncComponent* GetCommanderNetSyncComponent() const { return NetSyncComponent; }

	void ActivateSelectionTool();
	bool ArmMoveTool();
	void StepSelectionRadiusUp();
	EGuLiCommanderToolMode GetCommanderToolMode() const { return CommanderToolMode; }

	void SetSelectionRadiusPreset(EGuLiSelectionRadiusPreset NewPreset);
	EGuLiSelectionRadiusPreset GetSelectionRadiusPreset() const { return SelectionRadiusPreset; }
	bool GetCursorGroundLocation(FVector& OutLocation) const;
	bool GetActiveCommandLine(
		FVector& OutStart,
		FVector& OutEnd,
		float& OutAlpha,
		EGuLiCommandLineState& OutState) const;

	FGuLiCommanderToolModeChanged OnCommanderToolModeChanged;
	FGuLiSelectionRadiusPresetChanged OnSelectionRadiusPresetChanged;

#if WITH_EDITOR
	/** Local PIE QA must share normal input's sequence instead of poisoning its high-water mark. */
	uint32 AllocateEditorQASelectionRequestId() { return AllocateSelectionRequestId(); }
#endif

private:
	void HandlePrimaryActionAtCursor();
	void HandleSecondaryActionAtCursor();
	// 键盘入口单独守角色门；public tool/radius helper 仍可用于本地复位和纯逻辑测试。
	void HandleActivateSelectionToolInput();
	void HandleStepSelectionRadiusInput();
	void HandleArmMoveToolInput();
	void HandleCancelInput();
	// 本地把光标落点封装成选兵意图；返回 true 仅表示已提交，不代表服务器接受。
	bool TryIssueSelectionAtCursor();
	// 本地创建移动请求并启动有限表现预测；网络结果稍后通过 ACK 更新。
	bool TryIssueMoveAtCursor();
	void ClearSelection();
	void ZoomCameraIn();
	void ZoomCameraOut();
	bool IsCursorOverCommanderUI() const;
	bool HasConfirmedSelection() const;
	bool TraceGroundUnderCursor(FVector& OutLocation) const;
	FVector FindConfirmedSelectionCenter() const;
	uint32 AllocateSelectionRequestId();
	uint32 AllocateMoveCommandId();
	void UpdateCommanderInputMode();
	void UpdateCameraInput(float DeltaTime);
	// 消费 ACK FIFO，只把匹配当前待显示移动的回执交给表现层，选兵 ACK 不得清除移动反馈。
	void UpdateAcceptedCommandVisual();

	// 默认子对象借用此 PlayerController 的 Owning Connection；指针不是额外的通信连接。
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Commander|Network", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UGuLiCommanderNetSyncComponent> NetSyncComponent;

	EGuLiCommanderToolMode CommanderToolMode = GuLiCommanderToolPolicy::DefaultToolMode;
	EGuLiSelectionRadiusPreset SelectionRadiusPreset = EGuLiSelectionRadiusPreset::Small;
	FVector CachedCursorGroundLocation = FVector::ZeroVector;
	bool bHasCursorGroundLocation = false;
	uint32 NextSelectionRequestId = 1u;
	uint32 NextMoveCommandId = 1u;
	uint32 PendingMoveCommandId = 0u;
	uint32 LastVisualizedMoveCommandId = 0u;
	FVector PendingMoveTarget = FVector::ZeroVector;
	FVector CommandLineStart = FVector::ZeroVector;
	FVector CommandLineEnd = FVector::ZeroVector;
	double CommandLineExpireTime = 0.0;
	float CommandLineFadeDurationSeconds = 0.0f;
	EGuLiCommandLineState CommandLineState = EGuLiCommandLineState::None;
	bool bCommanderInputModeInitialized = false;
	bool bCommanderInputActive = false;
};
