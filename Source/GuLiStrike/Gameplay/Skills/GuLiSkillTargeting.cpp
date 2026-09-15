#include "Gameplay/Skills/GuLiSkillTargeting.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"

bool GuLiSkillTargeting::ResolveGround(UWorld& World, const FVector& Point, FVector& Out, double* OutSurfaceHeight)
{
	if (Point.ContainsNaN() || Point.GetAbsMax() > 10000000) { return false; }
	FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(GuLiSkillGround),false);
	// Vehicle hulls are not terrain, even when their camera collision uses WorldStatic.
	for (TActorIterator<APawn> It(&World); It; ++It) { Params.AddIgnoredActor(*It); }
	FCollisionObjectQueryParams Objects(ECC_WorldStatic);
	if (!World.LineTraceSingleByObjectType(Hit,Point+FVector(0,0,20000),Point-FVector(0,0,30000),Objects,Params)
		|| !Hit.bBlockingHit || Hit.ImpactNormal.Z < .65f) { return false; }
	if (OutSurfaceHeight) { *OutSurfaceHeight = Hit.ImpactPoint.Z; }
	if (World.GetNetMode() == NM_Client) { Out = Hit.ImpactPoint; return true; }
	const auto* Authority = World.GetSubsystem<UGuLiBattleAuthoritySubsystem>();
	return Authority && Authority->ProjectExternalUnitLocation(Hit.ImpactPoint,Out)
		&& FVector::DistSquared2D(Point,Out) <= FMath::Square(100.0);
}
