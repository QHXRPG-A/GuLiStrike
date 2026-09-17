#include "Gameplay/Building/GuLiGroundAccessRampComponent.h"
#include "Gameplay/Building/GuLiBuildingTypes.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/ConstructorHelpers.h"

UGuLiGroundAccessRampComponent::UGuLiGroundAccessRampComponent()
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	SetStaticMesh(Cube.Object);
	SetMobility(EComponentMobility::Movable);
	SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	SetCanEverAffectNavigation(true);
	FitRamp(GroundEdge);
}

void UGuLiGroundAccessRampComponent::FitRamp(const FVector& LowerEdge)
{
	const FVector Along = LowerEdge - ApronEdge;
	const FRotator Rotation = Along.Rotation();
	const FVector Center = (LowerEdge + ApronEdge) * 0.5f - Rotation.RotateVector(FVector(0, 0, Thickness * 0.5f));
	SetRelativeTransform(FTransform(Rotation, Center, FVector(Along.Length() / 100.0f, Width / 100.0f, Thickness / 100.0f)));
}

void UGuLiGroundAccessRampComponent::BeginPlay()
{
	Super::BeginPlay();
	RefreshGroundFit();
}

bool UGuLiGroundAccessRampComponent::ResolveGroundEdge(UWorld& World, const FTransform& Building,
	const FCollisionQueryParams& Query, FVector& OutLocalEdge, FString& OutReason) const
{
	OutReason.Reset();
	const FVector LocalAlong = GroundEdge - ApronEdge;
	if (Building.ContainsNaN() || GroundEdge.ContainsNaN() || ApronEdge.ContainsNaN()
		|| !FMath::IsFinite(Width) || !FMath::IsFinite(Thickness) || Width <= 0 || Thickness <= 0
		|| LocalAlong.SizeSquared2D() < 1.0)
	{
		OutReason = TEXT("Invalid access-ramp geometry settings.");
		return false;
	}
	const FVector Side = FVector(-LocalAlong.Y, LocalAlong.X, 0).GetSafeNormal() * Width * 0.5;
	const double BaseZ = Building.GetLocation().Z;
	// Probe above all allowed terrain, then validate the height contract explicitly.
	// Starting only 500 cm above the base can start underground on an uphill entrance.
	for (const double SideFactor : { 0.0, -1.0, 1.0 })
	{
		const FVector Point = Building.TransformPosition(GroundEdge + Side * SideFactor);
		FHitResult Ground;
		if (!World.LineTraceSingleByObjectType(Ground, FVector(Point.X, Point.Y, BaseZ + GroundTraceDistance),
			FVector(Point.X, Point.Y, BaseZ - GroundTraceDistance), FCollisionObjectQueryParams(ECC_WorldStatic), Query)
			|| Ground.bStartPenetrating)
		{
			OutReason = FString::Printf(TEXT("No ground at ramp entrance %s."), *Point.ToCompactString());
			return false;
		}
		const double Height = Ground.ImpactPoint.Z - BaseZ;
		if (Height > MaximumGroundRise || Height < -MaximumGroundDrop)
		{
			OutReason = FString::Printf(TEXT("Ramp entrance height %.1f cm is outside [-%.0f, +%.0f] at %s."),
				Height, MaximumGroundDrop, MaximumGroundRise, *Ground.ImpactPoint.ToCompactString());
			return false;
		}
		const FVector UpperEdge = Building.TransformPosition(ApronEdge + Side * SideFactor);
		const FVector Along = Ground.ImpactPoint - UpperEdge;
		const double Slope = FMath::RadiansToDegrees(FMath::Atan2(FMath::Abs(Along.Z), Along.Size2D()));
		if (Slope > GuLiBuildingPlacementPolicy::MaximumSlopeDegrees
			|| !GuLiBuildingPlacementPolicy::IsSlopeAllowed(Ground.ImpactNormal))
		{
			OutReason = FString::Printf(TEXT("Ramp or entrance ground exceeds the %.0f degree building slope limit at %s (ramp %.1f)."),
				GuLiBuildingPlacementPolicy::MaximumSlopeDegrees, *Ground.ImpactPoint.ToCompactString(), Slope);
			return false;
		}
		if (SideFactor == 0.0) OutLocalEdge = Building.InverseTransformPosition(Ground.ImpactPoint);
	}
	return true;
}

bool UGuLiGroundAccessRampComponent::RefreshGroundFit()
{
	if (!GetOwner() || !GetWorld()) return false;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GuLiAccessRampGround), false, GetOwner());
	Params.AddIgnoredActor(GetOwner()->GetParentActor());
	FVector LocalEdge;
	FString Reason;
	if (!ResolveGroundEdge(*GetWorld(), GetOwner()->GetActorTransform(), Params, LocalEdge, Reason))
	{
		SetCanEverAffectNavigation(false);
		SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SetVisibility(false);
		UE_LOG(LogTemp, Warning, TEXT("[GULI_ACCESS_RAMP] Disabled %s: %s"), *GetPathName(), *Reason);
		return false;
	}
	FitRamp(LocalEdge);
	SetVisibility(true);
	SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	SetCanEverAffectNavigation(true);
	return true;
}

bool UGuLiGroundAccessRampComponent::ValidatePresentationGround(UWorld& World, UClass* PresentationClass,
	const FTransform& Building, const FCollisionQueryParams& Query, FString& OutReason)
{
	if (!PresentationClass || !PresentationClass->IsChildOf(AActor::StaticClass()))
	{
		OutReason = TEXT("Building presentation class is unavailable.");
		return false;
	}
	TArray<UGuLiGroundAccessRampComponent*> Ramps;
	PresentationClass->GetDefaultObject<AActor>()->GetComponents(Ramps);
	auto* ActualClass = Cast<UBlueprintGeneratedClass>(PresentationClass);
	for (UClass* Class = PresentationClass; Class; Class = Class->GetSuperClass())
	{
		const auto* BlueprintClass = Cast<UBlueprintGeneratedClass>(Class);
		if (!BlueprintClass || !BlueprintClass->SimpleConstructionScript) continue;
		for (const USCS_Node* Node : BlueprintClass->SimpleConstructionScript->GetAllNodes())
			if (auto* Ramp = Cast<UGuLiGroundAccessRampComponent>(Node->GetActualComponentTemplate(ActualClass)))
				Ramps.AddUnique(Ramp);
	}
	for (const auto* Ramp : Ramps)
	{
		FVector Edge;
		if (!Ramp->ResolveGroundEdge(World, Building, Query, Edge, OutReason)) return false;
	}
	return true;
}
