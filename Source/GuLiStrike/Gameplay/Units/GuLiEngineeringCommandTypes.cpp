#include "Gameplay/Units/GuLiEngineeringCommandTypes.h"

bool GuLiEngineeringCommands::IsAccepted(EGuLiTransitOrderResult Result)
{
	return Result == EGuLiTransitOrderResult::Accepted || Result == EGuLiTransitOrderResult::DeferredUntilFactoryExit;
}
FString GuLiEngineeringCommands::Describe(EGuLiTransitOrderResult Result)
{
	switch (Result)
	{
	case EGuLiTransitOrderResult::Accepted: return TEXT("已开始据点运输");
	case EGuLiTransitOrderResult::DeferredUntilFactoryExit: return TEXT("安全出厂后执行运输");
	case EGuLiTransitOrderResult::SameTerritory: return TEXT("车辆已在该据点领域，保留原任务");
	case EGuLiTransitOrderResult::Unauthorized: return TEXT("无权指挥该车辆");
	case EGuLiTransitOrderResult::InvalidVehicle: return TEXT("车辆不存在或已摧毁");
	case EGuLiTransitOrderResult::ActionsLocked: return TEXT("车辆正在相位或受行动锁定");
	case EGuLiTransitOrderResult::InvalidSource: return TEXT("车辆不在己方据点领域");
	case EGuLiTransitOrderResult::SourceEncircled: return TEXT("起点据点被围，无法接入");
	case EGuLiTransitOrderResult::InvalidTarget: return TEXT("目标据点不存在或已非己方");
	case EGuLiTransitOrderResult::TargetEncircled: return TEXT("目标据点被围，无法接入");
	case EGuLiTransitOrderResult::NoRoute: return TEXT("本方快速通道没有合法路线");
	case EGuLiTransitOrderResult::StaleRequest: return TEXT("运输请求已过期");
	default: return TEXT("运输请求参数无效");
	}
}
