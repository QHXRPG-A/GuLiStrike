#include "Gameplay/CombatEffects/GuLiUnitWreck.h"
#include "Gameplay/Presentation/GuLiUnitRenderPolicy.h"

#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

AGuLiUnitWreck::AGuLiUnitWreck()
{
	bReplicates = false;
	SetReplicateMovement(false);
	PrimaryActorTick.bCanEverTick = false;
	SetCanBeDamaged(false);
	PhysicsBody = CreateDefaultSubobject<UBoxComponent>(TEXT("WreckPhysics"));
	SetRootComponent(PhysicsBody);
	PhysicsBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PhysicsBody->SetCollisionObjectType(ECC_PhysicsBody);
	PhysicsBody->SetCollisionResponseToAllChannels(ECR_Ignore);
	PhysicsBody->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	PhysicsBody->SetGenerateOverlapEvents(false);
	PhysicsBody->SetCanEverAffectNavigation(false);
	PhysicsBody->SetLinearDamping(0.0f);
	PhysicsBody->SetAngularDamping(0.15f);
	PhysicsBody->SetUseCCD(true);
	PhysicsBody->SetNotifyRigidBodyCollision(true);
	PhysicsBody->OnComponentHit.AddDynamic(this, &ThisClass::HandleImpact);
}

void AGuLiUnitWreck::ConfigureVisual(UMeshComponent* Mesh, const FTransform& Transform, UMaterialInterface* Material)
{
	Mesh->SetupAttachment(PhysicsBody);
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetRenderCustomDepth(false);
	Mesh->SetOverlayMaterial(nullptr);
	for (int32 Slot = 0; Slot < FMath::Max(1, Mesh->GetNumMaterials()); ++Slot) Mesh->SetMaterial(Slot, Material);
	AddInstanceComponent(Mesh);
	Mesh->RegisterComponent();
	Mesh->SetWorldTransform(Transform);
}

bool AGuLiUnitWreck::InitializeFromActor(AActor* Source, UMaterialInterface* Material)
{
	if (!Source || !Material || GetNetMode() == NM_DedicatedServer) return false;
	const FTransform Axes(Source->GetActorQuat(), Source->GetActorLocation(), FVector::OneVector);
	FBox Bounds(ForceInit);
	TInlineComponentArray<UMeshComponent*> Sources;
	Source->GetComponents(Sources, true);
	TArray<UMeshComponent*> Models;
	for (UMeshComponent* Mesh : Sources)
	{
		// Never copy Niagara, selection widgets, or an entire instanced population.
		if (!IsValid(Mesh) || !Mesh->IsVisible() || Cast<UInstancedStaticMeshComponent>(Mesh)) continue;
		FBox LocalBounds(ForceInit);
		if (const auto* Static = Cast<UStaticMeshComponent>(Mesh); Static && Static->GetStaticMesh())
			LocalBounds = Static->GetStaticMesh()->GetBoundingBox();
		else if (const auto* Skeletal = Cast<USkeletalMeshComponent>(Mesh); Skeletal && Skeletal->GetSkeletalMeshAsset())
			LocalBounds = Skeletal->CalcBounds(FTransform::Identity).GetBox();
		if (!LocalBounds.IsValid || Mesh->GetComponentTransform().ContainsNaN()) continue;
		Bounds += LocalBounds.TransformBy(Mesh->GetComponentTransform().GetRelativeTransform(Axes));
		Models.Add(Mesh);
	}
	if (!Bounds.IsValid || Models.IsEmpty()) return false;
	SetActorTransform(FTransform(Axes.GetRotation(), Axes.TransformPosition(Bounds.GetCenter())));
	PhysicsBody->SetBoxExtent(Bounds.GetExtent().ComponentMax(FVector(1.0f)), false);
	for (UMeshComponent* SourceMesh : Models)
	{
		// Preserve the source body's capture policy; player ship bodies are not opted in here.
		const bool bExcludedUnit = !SourceMesh->bVisibleInReflectionCaptures
			&& !SourceMesh->bVisibleInRealTimeSkyCaptures && !SourceMesh->bVisibleInRayTracing;
		if (auto* Static = Cast<UStaticMeshComponent>(SourceMesh))
		{
			auto* Copy = NewObject<UStaticMeshComponent>(this);
			Copy->SetStaticMesh(Static->GetStaticMesh());
			if (bExcludedUnit) GuLiUnitRenderPolicy::ApplyReflectionExclusions(*Copy);
			ConfigureVisual(Copy, Static->GetComponentTransform(), Material);
		}
		else if (auto* Skeletal = Cast<USkeletalMeshComponent>(SourceMesh))
		{
			auto* Copy = NewObject<UPoseableMeshComponent>(this);
			Copy->SetSkinnedAssetAndUpdate(Skeletal->GetSkeletalMeshAsset());
			if (bExcludedUnit) GuLiUnitRenderPolicy::ApplyReflectionExclusions(*Copy);
			ConfigureVisual(Copy, Skeletal->GetComponentTransform(), Material);
			Copy->CopyPoseFromSkeletalComponent(Skeletal);
			Copy->RefreshBoneTransforms();
			Copy->SetComponentTickEnabled(false);
		}
	}
	return true;
}

bool AGuLiUnitWreck::InitializeFromStaticMesh(UStaticMesh* Mesh, const FTransform& Transform, UMaterialInterface* Material)
{
	if (!Mesh || !Material || Transform.ContainsNaN() || GetNetMode() == NM_DedicatedServer) return false;
	const FBox Bounds = Mesh->GetBoundingBox();
	if (!Bounds.IsValid) return false;
	SetActorTransform(FTransform(Transform.GetRotation(), Transform.TransformPosition(Bounds.GetCenter())));
	PhysicsBody->SetBoxExtent((Bounds.GetExtent() * Transform.GetScale3D().GetAbs()).ComponentMax(FVector(1.0f)), false);
	auto* Copy = NewObject<UStaticMeshComponent>(this);
	Copy->SetStaticMesh(Mesh);
	GuLiUnitRenderPolicy::ApplyReflectionExclusions(*Copy);
	ConfigureVisual(Copy, Transform, Material);
	return true;
}

void AGuLiUnitWreck::StartFalling(const FVector& InitialVelocity, float MaximumLifetime)
{
	if (GetNetMode() == NM_DedicatedServer || InitialVelocity.ContainsNaN()) { Destroy(); return; }
	// The flight model has no authored collision. A bounds-sized body owns Chaos simulation.
	// Only scenery blocks scrap: no collision, navigation or damage interaction with living units.
	PhysicsBody->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
	PhysicsBody->SetEnableGravity(true);
	PhysicsBody->SetSimulatePhysics(true);
	PhysicsBody->SetPhysicsLinearVelocity(InitialVelocity);
	PhysicsBody->SetPhysicsAngularVelocityInDegrees(GetActorQuat().RotateVector(FVector(18.0f, 9.0f, 0.0f)));
	PhysicsBody->WakeAllRigidBodies();
	SetLifeSpan(FMath::Max(1.0f, MaximumLifetime)); // Missing terrain / falling beyond the map cannot leak actors.
}

void AGuLiUnitWreck::HandleImpact(UPrimitiveComponent* HitComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit)
{
	// Walls may deflect the body; a supporting surface (including slopes and roofs) ends the fall.
	if (Hit.bBlockingHit && Hit.ImpactNormal.Z > 0.25f) Destroy();
}
