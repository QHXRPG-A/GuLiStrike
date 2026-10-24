#pragma once
#include "AnimGraphNode_SkeletalControlBase.h"
#include "Gameplay/GroundMech/AnimNode_GuLiGroundMechAim.h"
#include "AnimGraphNode_GuLiGroundMechAim.generated.h"

UCLASS()
class GULISTRIKEANIMGRAPH_API UAnimGraphNode_GuLiGroundMechAim : public UAnimGraphNode_SkeletalControlBase
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere,Category="Settings") FAnimNode_GuLiGroundMechAim Node;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetControllerDescription() const override;
	virtual const FAnimNode_SkeletalControlBase* GetNode() const override { return &Node; }
};
