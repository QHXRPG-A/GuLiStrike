#include "Gameplay/Stronghold/GuLiStrongholdDiagnostics.h"
#include "Gameplay/Stronghold/GuLiStrongholdCaptureComponent.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Gameplay/Building/GuLiBuildingRegistrySubsystem.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Resources/GuLiResourceWorldState.h"
#include "Gameplay/Resources/GuLiResourceActors.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"

FString GuLiStrongholds::DescribeWorld(UWorld& World)
{
	const auto& Resources = *World.GetSubsystem<UGuLiResourceWorldSubsystem>();
	if (!Resources.IsRuntimeReady()) return TEXT("据点世界未就绪。");
	TArray<UGuLiBuildingLifecycleComponent*> Buildings;
	World.GetSubsystem<UGuLiBuildingRegistrySubsystem>()->Query(Buildings);
	FString Result = FString::Printf(TEXT("建筑 %d；空中连接 %d\n"),Buildings.Num(),Resources.GetStrongholdTopology().GetTransportEdges().Num());
	if (World.GetNetMode() != NM_Client)
	{
		const auto& Battle = *World.GetSubsystem<UGuLiBattleAuthoritySubsystem>();
		for (auto Team : {EGuLiTeam::Red,EGuLiTeam::Blue})
		{
			const auto Population = Battle.GetTeamPopulation(Team);
			const auto Inventory = Resources.GetTeamInventory(Team);
			Result += FString::Printf(TEXT("%s：存活 %d + 预留 %d / %d；蓝 %d 红 %d\n"),
				Team == EGuLiTeam::Red ? TEXT("红方") : TEXT("蓝方"),Population.X,Population.Y,Battle.GetTeamUnitCap(),Inventory.Blue,Inventory.Red);
		}
	}
	for (TActorIterator<AGuLiTerritoryOutpostActor> It(&World); It; ++It)
	{
		const int32 Index = It->GetTerritoryIndex();
		const auto& State = Resources.GetResourceWorldState()->GetTerritories()[Index];
		const auto& Capture = *It->FindComponentByClass<UGuLiStrongholdCaptureComponent>();
		Result += FString::Printf(TEXT("据点 %02d 阵营 %d 进度 %+.2f 红/蓝 %d/%d 维护 %s 包围 %s\n"),
			Index,int32(State.Owner),Capture.GetCaptureProgress(),Capture.GetRedCount(),Capture.GetBlueCount(),
			State.bSupplied ? TEXT("正常") : TEXT("欠费"),State.bEncircled ? TEXT("是") : TEXT("否"));
	}
	for (TActorIterator<APawn> It(&World); It; ++It)
	{
		const auto* Vehicle = Cast<IGuLiEngineeringVehicle>(*It);
		if (!Vehicle) continue;
		const auto State = It->FindComponentByClass<UGuLiEngineeringTravelComponent>()->GetTransitState();
		Result += FString::Printf(TEXT("工程车 %u 阶段 %s 目标 %d 路线节点 %d 紧急退出 %d\n"),
			Vehicle->GetStableActorId().Value,*StaticEnum<EGuLiTransitPhase>()->GetNameStringByValue(int64(State.Phase)),
			State.DestinationTerritory,State.Route.Num(),State.bEmergencyExit);
	}
	return Result;
}
#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorld StrongholdStatusCommand(TEXT("guli.stronghold.Status"),TEXT("Print local replicated stronghold and transit snapshots."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{ if (World) UE_LOG(LogTemp,Display,TEXT("%s"),*GuLiStrongholds::DescribeWorld(*World)); }));
#endif
