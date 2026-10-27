#pragma once
#include "AnimGraphNode_SkeletalControlBase.h"
#include "Gameplay/GroundMech/Animation/AnimNode_GuLiGroundMechGrounding.h"
#include "AnimGraphNode_GuLiGroundMechGrounding.generated.h"
UCLASS()
class GULISTRIKEANIMGRAPH_API UAnimGraphNode_GuLiGroundMechGrounding : public UAnimGraphNode_SkeletalControlBase
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,Category="Settings") FAnimNode_GuLiGroundMechGrounding Node;
    virtual FText GetNodeTitle(ENodeTitleType::Type) const override { return FText::FromString(TEXT("Ground Mech: stride and planted soles")); }
    virtual FText GetControllerDescription() const override { return GetNodeTitle(ENodeTitleType::FullTitle); }
    virtual const FAnimNode_SkeletalControlBase* GetNode() const override { return &Node; }
};
