#include "Gameplay/Stronghold/GuLiOutpostPresentationComponent.h"

#include "Components/MaterialBillboardComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInstanceDynamic.h"

UGuLiOutpostPresentationSettings::UGuLiOutpostPresentationSettings()
{
	Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/GuLiStrike/Buildings/Meshes/SM_OutpostPlaceholder.SM_OutpostPlaceholder")));
	BodyMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/GuLiStrike/FX/StrongholdOutpost/M_OutpostGlow.M_OutpostGlow")));
	HaloMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/GuLiStrike/FX/StrongholdOutpost/M_OutpostHalo.M_OutpostHalo")));
}

UGuLiOutpostPresentationComponent::UGuLiOutpostPresentationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

double UGuLiOutpostPresentationComponent::SampleFloatHeight(
	const double Time, const double Epoch, const double Amplitude, const double Period)
{
	return Amplitude * (1.0 - FMath::Cos(UE_DOUBLE_TWO_PI * (Time - Epoch) / Period));
}

FLinearColor UGuLiOutpostPresentationComponent::OwnerColor(const EGuLiTeam Team)
{
	static const FLinearColor Colors[] = {
		FLinearColor::White, FLinearColor(0.8f, 0.035f, 0.02f, 1.0f), FLinearColor(0.01f, 0.16f, 0.9f, 1.0f)
	};
	return Colors[static_cast<uint8>(Team)];
}

double UGuLiOutpostPresentationComponent::ServerTime() const
{
	return GetWorld()->GetGameState()->GetServerWorldTimeSeconds();
}

void UGuLiOutpostPresentationComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() == NM_DedicatedServer) return;
	CreatePresentation();
	RefreshColor();
	UpdatePose();
}

void UGuLiOutpostPresentationComponent::CreatePresentation()
{
	const auto& Config = *GetDefault<UGuLiOutpostPresentationSettings>();
	UStaticMesh* Mesh = Config.Mesh.LoadSynchronous();
	const FBoxSphereBounds Bounds = Mesh->GetBounds();
	const double Scale = Config.ModelHeightCm / (2.0 * Bounds.BoxExtent.Z);
	RestMeshLocation = FVector(-Bounds.Origin.X * Scale, -Bounds.Origin.Y * Scale,
		-500.0 - (Bounds.Origin.Z - Bounds.BoxExtent.Z) * Scale);
	BodyMaterial = UMaterialInstanceDynamic::Create(Config.BodyMaterial.LoadSynchronous(), this);
	HaloMaterial = UMaterialInstanceDynamic::Create(Config.HaloMaterial.LoadSynchronous(), this);
	BodyMaterial->SetScalarParameterValue(TEXT("GlowIntensity"), Config.GlowIntensity);
	HaloMaterial->SetScalarParameterValue(TEXT("HaloOpacity"), Config.HaloOpacity);

	VisualMesh = NewObject<UStaticMeshComponent>(GetOwner(), TEXT("OutpostVisual"), RF_Transient);
	GetOwner()->AddInstanceComponent(VisualMesh);
	VisualMesh->SetMobility(EComponentMobility::Movable);
	VisualMesh->SetupAttachment(GetOwner()->GetRootComponent());
	VisualMesh->SetStaticMesh(Mesh);
	VisualMesh->SetMaterial(0, BodyMaterial);
	VisualMesh->SetRelativeScale3D(FVector(Scale));
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	VisualMesh->SetCollisionObjectType(ECC_WorldDynamic);
	VisualMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	VisualMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	VisualMesh->SetCanEverAffectNavigation(false);
	VisualMesh->RegisterComponent();

	Halo = NewObject<UMaterialBillboardComponent>(GetOwner(), TEXT("OutpostHalo"), RF_Transient);
	GetOwner()->AddInstanceComponent(Halo);
	Halo->SetMobility(EComponentMobility::Movable);
	Halo->SetupAttachment(GetOwner()->GetRootComponent());
	Halo->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Halo->SetCanEverAffectNavigation(false);
	Halo->SetCastShadow(false);
	Halo->AddElement(HaloMaterial, nullptr, false, 800.0f, 900.0f, nullptr);
	Halo->RegisterComponent();
}

void UGuLiOutpostPresentationComponent::ApplyOwnerState(const FGuLiOutpostOwnerState& InState)
{
	State = InState;
	// State can arrive before BeginPlay; component initialization consumes that snapshot.
	if (!HasBegunPlay() || GetNetMode() == NM_DedicatedServer) return;
	RefreshColor();
	UpdatePose();
}

void UGuLiOutpostPresentationComponent::RefreshColor()
{
	const FLinearColor Color = OwnerColor(State.Team);
	BodyMaterial->SetVectorParameterValue(TEXT("GlowColor"), Color);
	HaloMaterial->SetVectorParameterValue(TEXT("GlowColor"), Color);
}

void UGuLiOutpostPresentationComponent::UpdatePose()
{
	const auto& Config = *GetDefault<UGuLiOutpostPresentationSettings>();
	const double Now = ServerTime();
	double Height = 0.0;
	bool bAnimate = State.Team != EGuLiTeam::Unassigned;
	if (bAnimate)
	{
		Height = SampleFloatHeight(Now, State.FloatEpochServerTime, Config.FloatAmplitudeCm, Config.FloatPeriodSeconds);
	}
	else if (State.ChangedServerTime > State.FloatEpochServerTime)
	{
		const double LandingAlpha = FMath::Clamp((Now - State.ChangedServerTime) / Config.LandingSeconds, 0.0, 1.0);
		const double SmoothAlpha = LandingAlpha * LandingAlpha * (3.0 - 2.0 * LandingAlpha);
		Height = SampleFloatHeight(State.ChangedServerTime, State.FloatEpochServerTime,
			Config.FloatAmplitudeCm, Config.FloatPeriodSeconds) * (1.0 - SmoothAlpha);
		bAnimate = LandingAlpha < 1.0;
	}
	VisualMesh->SetRelativeLocation(RestMeshLocation + FVector(0, 0, Height));
	Halo->SetRelativeLocation(FVector(0, 0, -500.0 + Config.ModelHeightCm * 0.5 + Height));
	SetComponentTickEnabled(bAnimate);
}

void UGuLiOutpostPresentationComponent::TickComponent(
	const float DeltaTime, const ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	UpdatePose();
}

void UGuLiOutpostPresentationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	SetComponentTickEnabled(false);
	if (GetNetMode() != NM_DedicatedServer)
	{
		Halo->DestroyComponent();
		VisualMesh->DestroyComponent();
	}
	Super::EndPlay(Reason);
}
