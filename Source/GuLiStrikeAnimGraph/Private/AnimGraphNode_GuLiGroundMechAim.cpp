#include "AnimGraphNode_GuLiGroundMechAim.h"
FText UAnimGraphNode_GuLiGroundMechAim::GetNodeTitle(ENodeTitleType::Type) const
{ return FText::FromString(TEXT("Ground Mech: independent muzzle aim and recoil")); }
FText UAnimGraphNode_GuLiGroundMechAim::GetControllerDescription() const
{ return GetNodeTitle(ENodeTitleType::FullTitle); }
