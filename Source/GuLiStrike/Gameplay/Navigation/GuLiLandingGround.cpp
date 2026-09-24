#include "Gameplay/Navigation/GuLiLandingGround.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"

bool GuLiLandingGround::IsWalkableSupport(const FHitResult& Hit, const UCharacterMovementComponent* Movement)
{
	const UPrimitiveComponent* Component = Hit.GetComponent();
	if (!Hit.IsValidBlockingHit() || !Component || Component->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block)
		return false;
	for (const AActor* Actor = Hit.GetActor(); Actor; Actor = Actor->GetAttachParentActor())
	{
		if (Cast<IGuLiBuildingOwner>(Actor) || Cast<APawn>(Actor)) return false;
	}
	return Movement ? Movement->IsWalkable(Hit)
		: Hit.ImpactNormal.Z >= Component->GetWalkableSlopeOverride().ModifyWalkableFloorZ(.65f);
}

bool GuLiLandingGround::Resolve(UWorld& World, const FVector& Point, FVector& OutNavLocation,
	double* OutSurfaceHeight, const APawn* Pawn)
{
	if (Point.ContainsNaN() || Point.GetAbsMax() > 10000000) return false;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(GuLiLandingGround), false);
	for (TActorIterator<APawn> It(&World); It; ++It) Query.AddIgnoredActor(*It);
	const FCollisionObjectQueryParams Objects(ECC_WorldStatic);
	const auto* Character = Cast<ACharacter>(Pawn);
	const auto* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	FHitResult Support;
	if (!World.LineTraceSingleByObjectType(Support, Point+FVector(0,0,20000), Point-FVector(0,0,30000), Objects, Query)
		|| !IsWalkableSupport(Support, Movement)) return false;
	FVector Ground = Support.ImpactPoint;
	if (World.GetNetMode() != NM_Client)
	{
		// A tall projection box can pick another floor or a disconnected roof.
		constexpr double MaxSurfaceDelta = 50.0;
		if (Pawn)
		{
			auto* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
			const auto* Data = Navigation ? Navigation->GetNavDataForProps(Pawn->GetNavAgentPropertiesRef(), Ground) : nullptr;
			FNavLocation Projected;
			if (!Data || !Navigation->ProjectPointToNavigation(Ground, Projected, FVector(20,20,MaxSurfaceDelta), Data)) return false;
			Ground = Projected.Location;
		}
		else
		{
			const auto* Authority = World.GetSubsystem<UGuLiBattleAuthoritySubsystem>();
			if (!Authority || !Authority->ProjectExternalUnitLocation(Support.ImpactPoint, Ground)) return false;
		}
		if (FVector::DistSquared2D(Support.ImpactPoint, Ground) > FMath::Square(20.0)
			|| FMath::Abs(Support.ImpactPoint.Z-Ground.Z) > MaxSurfaceDelta) return false;
		// Projection may shift XY. Check the actual landing column as well, so an
		// adjacent ledge/roof cannot supply the final support or its height.
		if (!World.LineTraceSingleByObjectType(Support, Ground+FVector(0,0,20000), Ground-FVector(0,0,30000), Objects, Query)
			|| !IsWalkableSupport(Support, Movement)
			|| FMath::Abs(Support.ImpactPoint.Z-Ground.Z) > MaxSurfaceDelta) return false;
	}
	OutNavLocation = Ground;
	if (OutSurfaceHeight) *OutSurfaceHeight = Support.ImpactPoint.Z;
	return true;
}
