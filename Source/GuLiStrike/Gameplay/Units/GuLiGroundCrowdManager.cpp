#include "Gameplay/Units/GuLiGroundCrowdManager.h"
#include "NavMesh/RecastNavMesh.h"
#include "Network/GuLiNetworkSettings.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Engine/World.h"

void UGuLiGroundCrowdObstacle::GetCrowdAgentCollisions(float& Radius, float& HalfHeight) const
{
	Radius = Body.Radius;
	HalfHeight = Body.Radius;
}

UGuLiGroundCrowdManager::UGuLiGroundCrowdManager(const FObjectInitializer& Initializer) : Super(Initializer)
{
	MaxAgents = 8192;
	MaxAgentRadius = 625;
	MaxAvoidedAgents = 32;
	MaxAvoidedWalls = 16;
	bResolveCollisions = false;
}

bool UGuLiGroundCrowdManager::IsSuitableNavData(const ANavigationData& NavData) const
{
	return NavData.IsA<ARecastNavMesh>() && NavData.GetConfig().Name == TEXT("CommanderSoldier");
}
void UGuLiGroundCrowdManager::Tick(float Dt)
{
    // Gameplay advancement is driven once per local world step by CommanderTasks.
}
void UGuLiGroundCrowdManager::AdvanceHybrid(uint64 At,float Dt)
{
	const auto* Battle=GetWorld()?GetWorld()->GetGameState<AGuLiBattleGameState>():nullptr;
	if (!Battle || !UGuLiNetworkSettings::IsHybrid(GetWorld())) return;
	const uint64 Epoch=Battle->GetMatchEpoch();
	if (Epoch!=LastHybridEpoch) {LastHybridEpoch=Epoch; LastHybridTick=0;}
	if (At<=LastHybridTick) return;
	LastHybridTick=At; Super::Tick(Dt);
}

void UGuLiGroundCrowdManager::SetGroundObstacles(TConstArrayView<FGuLiGroundAvoidanceBody> Bodies)
{
	TSet<uint32> Present;
	Present.Reserve(Bodies.Num());
	for (const auto& Body : Bodies) Present.Add(Body.Id);
	for (auto It = Obstacles.CreateIterator(); It; ++It)
	{
		if (!Present.Contains(It.Key()))
		{
			UnregisterAgent(It.Value());
			It.RemoveCurrent();
		}
	}
	for (const auto& Body : Bodies)
	{
		if (auto* Existing = Obstacles.Find(Body.Id))
		{
			const bool bRadiusChanged = (*Existing)->Body.Radius != Body.Radius;
			(*Existing)->Body = Body;
			if (bRadiusChanged) UpdateAgentParams(*Existing);
		}
		else
		{
			checkf(ActiveAgents.Num() < MaxAgents, TEXT("Ground crowd capacity %d exhausted; update CrowdManager MaxAgents."), MaxAgents);
			auto* Obstacle = NewObject<UGuLiGroundCrowdObstacle>(this);
			Obstacle->Body = Body;
			Obstacles.Add(Body.Id, Obstacle);
			RegisterAgent(Obstacle);
		}
	}
}
