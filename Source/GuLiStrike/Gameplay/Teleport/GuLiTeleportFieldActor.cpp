#include "Gameplay/Teleport/GuLiTeleportFieldActor.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"

AGuLiTeleportFieldActor* AGuLiTeleportFieldActor::FindCast(UWorld& World, FGuid CommanderId)
{
	AGuLiTeleportFieldActor* Latest = nullptr;
	for (TActorIterator<AGuLiTeleportFieldActor> It(&World); It; ++It)
	{
		if (!It->bPreview && !It->IsActorBeingDestroyed() && It->State.CommanderId == CommanderId
			&& (!Latest || It->State.PhaseStartTime > Latest->State.PhaseStartTime)) { Latest = *It; }
	}
	return Latest;
}

void AGuLiTeleportFieldActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds); TickVisuals();
}

double AGuLiTeleportFieldActor::GetSynchronizedTime(const UWorld& World)
{
	const auto* H = World.GetSubsystem<UGuLiHybridWorldSubsystem>();
	return H ? H->GetLogicalSeconds() : World.GetTimeSeconds();
}

void AGuLiTeleportFieldActor::ApplyHybridView(const FGuLiTeleportCastState& View)
{
	State = View;
	bPreview = false;
}
