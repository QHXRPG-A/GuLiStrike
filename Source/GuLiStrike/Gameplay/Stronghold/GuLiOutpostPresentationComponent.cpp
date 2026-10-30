#include "Gameplay/Stronghold/GuLiOutpostPresentationComponent.h"
#include "Commander/UI/GuLiSceneUISourceRegistry.h"
#include "Gameplay/Building/GuLiBuildingCatalog.h"
#include "Gameplay/Models/GuLiModelRegistrySubsystem.h"
#include "Gameplay/Models/GuLiLocalTeamColorSubsystem.h"
#include "Gameplay/Presentation/GuLiLocalTeamColors.h"

#include "Components/MaterialBillboardComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInstanceDynamic.h"

UGuLiOutpostPresentationSettings::UGuLiOutpostPresentationSettings()
{
	ModelId = GuLiModelIds::ManualOutpost;
	BodyMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/GuLiStrike/FX/StrongholdOutpost/M_OutpostGlow.M_OutpostGlow")));
	HaloMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/GuLiStrike/FX/StrongholdOutpost/M_OutpostHalo.M_OutpostHalo")));
	OutlineMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/GuLiStrike/Buildings/OutpostSymmetric/Materials/M_OutpostSymmetric_Outline.M_OutpostSymmetric_Outline")));
}

UGuLiOutpostPresentationComponent::UGuLiOutpostPresentationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

double UGuLiOutpostPresentationComponent::SampleFloatHeight(
	const double Time, const double Epoch, const double Amplitude, const double Period)
{
	if (!FMath::IsFinite(Time) || !FMath::IsFinite(Epoch) || !FMath::IsFinite(Amplitude)
		|| !FMath::IsFinite(Period) || Period <= UE_DOUBLE_SMALL_NUMBER || Amplitude < 0.0) return 0.0;
	return Amplitude * (1.0 - FMath::Cos(UE_DOUBLE_TWO_PI * (Time - Epoch) / Period));
}

double UGuLiOutpostPresentationComponent::SamplePresentationHeight(const double Time,
	const FGuLiOutpostOwnerState& OwnerState, const UGuLiOutpostPresentationSettings& Config)
{
	const double Elapsed = FMath::Max(0.0, Time - OwnerState.FloatEpochServerTime);
	const double Center = FMath::Max(1.0, double(Config.HoverHeightCm));
	const double Amplitude = FMath::Clamp(double(Config.FloatAmplitudeCm), 0.0, Center - 1.0);
	// Always start above ground, including neutral ownership and the first presented frame.
	return Center - Amplitude + SampleFloatHeight(Elapsed, 0.0, Amplitude, Config.FloatPeriodSeconds);
}

double UGuLiOutpostPresentationComponent::SamplePresentationYaw(const double Time,
	const FGuLiOutpostOwnerState& OwnerState, const UGuLiOutpostPresentationSettings& Config)
{
	const double Period = Config.SelfRotationPeriodSeconds;
	if (!FMath::IsFinite(Time) || !FMath::IsFinite(OwnerState.FloatEpochServerTime)
		|| !FMath::IsFinite(Period) || Period <= UE_DOUBLE_SMALL_NUMBER) return 0.0;
	const double Elapsed = FMath::Max(0.0, Time - OwnerState.FloatEpochServerTime);
	return FMath::Fmod(Elapsed, Period) * 360.0 / Period;
}

bool UGuLiOutpostPresentationComponent::ResolveVisualLayout(UStaticMesh* Mesh,
	FGuLiOutpostVisualLayout& Layout, FString& Warning)
{
	Layout = FGuLiOutpostVisualLayout();
	Warning.Reset();
	const auto IsPositiveSize = [](const FVector& Size)
	{
		return !Size.ContainsNaN() && Size.GetMin() > UE_DOUBLE_SMALL_NUMBER;
	};
	if (!Mesh || !IsPositiveSize(Mesh->GetBounds().BoxExtent * 2.0))
	{
		Warning = TEXT("Outpost mesh requires finite, positive bounds on all three axes.");
		return false;
	}
	const auto& Config = *GetDefault<UGuLiOutpostPresentationSettings>();
	const auto* Catalog = UGuLiBuildingCatalog::LoadDefaultCatalog();
	const auto* Reference = Catalog ? Catalog->FindById(Config.ReferenceBuildingId) : nullptr;
	if (Reference && Reference->Mesh && IsPositiveSize(Reference->MeshScale.GetAbs())
		&& IsPositiveSize(Reference->Mesh->GetBounds().BoxExtent * 2.0)
		&& FMath::IsFinite(Config.ReferenceSizeMultiplier) && Config.ReferenceSizeMultiplier > 0.0f)
	{
		Layout.ReferenceMesh = Reference->Mesh;
		Layout.ReferenceMeshScale = Reference->MeshScale;
		Layout.ReferenceGroundOffset = Reference->VisualOffset;
		// MeshScale already contains the catalog's grid quantization. Apply it exactly once.
		Layout.ReferenceSizeCm = Reference->Mesh->GetBounds().BoxExtent * 2.0 * Reference->MeshScale.GetAbs();
		Layout.SizeCm = Layout.ReferenceSizeCm * Config.ReferenceSizeMultiplier;
		Layout.MeshScale = Layout.SizeCm / (Mesh->GetBounds().BoxExtent * 2.0);
		Layout.bUsesReferenceSize = true;
	}
	else
	{
		Warning = FString::Printf(TEXT("Outpost reference building %d or its size multiplier is unavailable; using legacy height %.1f cm."),
			Config.ReferenceBuildingId, Config.ModelHeightCm);
		if (!FMath::IsFinite(Config.ModelHeightCm) || Config.ModelHeightCm <= 0.0f) return false;
		Layout.MeshScale = FVector(Config.ModelHeightCm / (Mesh->GetBounds().BoxExtent.Z * 2.0));
		Layout.SizeCm = Mesh->GetBounds().BoxExtent * 2.0 * Layout.MeshScale;
	}
	const auto Bounds = Mesh->GetBounds();
	Layout.GroundOffset = FVector(-Bounds.Origin.X, -Bounds.Origin.Y, Bounds.BoxExtent.Z - Bounds.Origin.Z) * Layout.MeshScale;
	return true;
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
	const auto* StateObject=GetWorld() ? GetWorld()->GetGameState() : nullptr;
	return StateObject ? StateObject->GetServerWorldTimeSeconds() : 0.0;
}

bool UGuLiOutpostPresentationComponent::GetSceneUIHalo(FVector& Center, EGuLiTeam& Team, FVector2D& RadiiCm) const
{
	if (!VisualMesh || !VisualMesh->IsVisible() || GetOwner()->IsHidden()) return false;
	const FVector Extent = VisualMesh->Bounds.BoxExtent;
	Center=VisualMesh->Bounds.Origin; Team=State.Team;
	RadiiCm=FVector2D(Extent.Size2D(), Extent.Size()) * 1.05;
	return true;
}

void UGuLiOutpostPresentationComponent::OnRegister()
{
	Super::OnRegister();
	if (GetWorld()) if (auto* R = GetWorld()->GetSubsystem<UGuLiSceneUISourceRegistry>())
		R->RegisterSource(this, EGuLiSceneUISourceKind::OutpostHalo);
}
void UGuLiOutpostPresentationComponent::OnUnregister()
{
	if (GetWorld()) if (auto* R = GetWorld()->GetSubsystem<UGuLiSceneUISourceRegistry>()) R->UnregisterSource(this);
	Super::OnUnregister();
}

void UGuLiOutpostPresentationComponent::BeginPlay()
{
	Super::BeginPlay();
	if (auto* Registry = GetWorld()->GetSubsystem<UGuLiSceneUISourceRegistry>()) Registry->RegisterSource(this, EGuLiSceneUISourceKind::OutpostHalo);
	if (GetNetMode() == NM_DedicatedServer) return;
	CreatePresentation();
	if (!VisualMesh) return;
	RefreshColor();
	UpdatePose();
}

void UGuLiOutpostPresentationComponent::CreatePresentation()
{
	const auto& Config = *GetDefault<UGuLiOutpostPresentationSettings>();
	UStaticMesh* Mesh = GuLiModels::Load<UStaticMesh>(this,Config.ModelId);
	if (!Mesh) return;
	FGuLiOutpostVisualLayout Layout;
	FString Warning;
	const bool bValidLayout = ResolveVisualLayout(Mesh, Layout, Warning);
	if (!Warning.IsEmpty()) UE_LOG(LogTemp, Warning, TEXT("%s: %s"), *GetOwner()->GetName(), *Warning);
	if (!bValidLayout) return;
	ScaledMeshBoundsOrigin = Mesh->GetBounds().Origin * Layout.MeshScale;
	RestMeshCenterLocation = Layout.GroundOffset + ScaledMeshBoundsOrigin + FVector(0, 0, -500.0);
	BodyMaterial = UMaterialInstanceDynamic::Create(Config.BodyMaterial.LoadSynchronous(), this);
	HaloMaterial = UMaterialInstanceDynamic::Create(Config.HaloMaterial.LoadSynchronous(), this);
	if (!BodyMaterial || !HaloMaterial) return;
	BodyMaterial->SetScalarParameterValue(TEXT("GlowIntensity"), Config.GlowIntensity);
	HaloMaterial->SetScalarParameterValue(TEXT("HaloOpacity"), Config.HaloOpacity);

	VisualMesh = NewObject<UStaticMeshComponent>(GetOwner(), TEXT("OutpostVisual"), RF_Transient);
	GetOwner()->AddInstanceComponent(VisualMesh);
	VisualMesh->SetMobility(EComponentMobility::Movable);
	VisualMesh->SetupAttachment(GetOwner()->GetRootComponent());
	VisualMesh->SetStaticMesh(Mesh);
	// Legacy glow remains a compatibility effect. An approved region material owns its fixed paint.
	if (VisualMesh->GetCustomPrimitiveDataIndexForVectorParameter(TEXT("GuLi_TeamPrimary"))!=8) VisualMesh->SetMaterial(0, BodyMaterial);
	VisualMesh->SetOverlayMaterial(Config.OutlineMaterial.LoadSynchronous());
	VisualMesh->SetRelativeScale3D(Layout.MeshScale);
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	VisualMesh->SetCollisionObjectType(ECC_WorldDynamic);
	VisualMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	VisualMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	VisualMesh->SetCanEverAffectNavigation(false);
	VisualMesh->RegisterComponent();
	UGuLiLocalTeamColorSubsystem::Register(this,VisualMesh,Config.ModelId,TEXT("Root"),State.Team);

	Halo = NewObject<UMaterialBillboardComponent>(GetOwner(), TEXT("OutpostHalo"), RF_Transient);
	GetOwner()->AddInstanceComponent(Halo);
	Halo->SetMobility(EComponentMobility::Movable);
	Halo->SetupAttachment(GetOwner()->GetRootComponent());
	Halo->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Halo->SetCanEverAffectNavigation(false);
	Halo->SetCastShadow(false);
	Halo->AddElement(HaloMaterial, nullptr, false, Layout.SizeCm.Size2D() * 1.05, Layout.SizeCm.Size() * 1.05, nullptr);
	Halo->RegisterComponent();
	UGuLiSceneUISourceRegistry::Notify(GetOwner(), EGuLiSceneUIChange::Membership);
	Halo->SetVisibility(false); // Ownership halo is drawn by the post-scene Slate layer.
}

void UGuLiOutpostPresentationComponent::ApplyOwnerState(const FGuLiOutpostOwnerState& InState)
{
	UGuLiSceneUISourceRegistry::Notify(this, EGuLiSceneUIChange::Membership);
	State = InState;
	// State can arrive before BeginPlay; component initialization consumes that snapshot.
	if (!HasBegunPlay() || GetNetMode() == NM_DedicatedServer) return;
	RefreshColor();
	UpdatePose();
}

void UGuLiOutpostPresentationComponent::RefreshColor()
{
	if (!BodyMaterial || !HaloMaterial) return;
	UGuLiLocalTeamColorSubsystem::Register(this,VisualMesh,GetDefault<UGuLiOutpostPresentationSettings>()->ModelId,TEXT("Root"),State.Team);
	const auto ViewTeam=GuLiLocalTeamColors::GetViewTeam(GetWorld()->GetFirstPlayerController());
	const bool Assigned=GuLiLocalTeamColors::IsAssigned(ViewTeam) && GuLiLocalTeamColors::IsAssigned(State.Team);
	const FLinearColor Color=Assigned ? OwnerColor(State.Team==ViewTeam ? EGuLiTeam::Blue : EGuLiTeam::Red) : FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#2C3735")));
	if (Color==LastGlowColor) return;
	LastGlowColor=Color;
	BodyMaterial->SetVectorParameterValue(TEXT("GlowColor"), Color);
	HaloMaterial->SetVectorParameterValue(TEXT("GlowColor"), Color);
}

void UGuLiOutpostPresentationComponent::UpdatePose()
{
	if (!VisualMesh || !Halo) return;
	const auto& Config = *GetDefault<UGuLiOutpostPresentationSettings>();
	const double Now = ServerTime();
	const double Height = SamplePresentationHeight(Now, State, Config);
	const FRotator Rotation(0.0, SamplePresentationYaw(Now, State, Config), 0.0);
	// Rotate around the centered landmark, including assets with an offset source pivot.
	VisualMesh->SetRelativeLocationAndRotation(RestMeshCenterLocation + FVector(0, 0, Height)
		- Rotation.RotateVector(ScaledMeshBoundsOrigin), Rotation);
	Halo->SetWorldLocation(VisualMesh->Bounds.Origin);
	SetComponentTickEnabled(true); // Also refreshes a late local identity/ownership display.
}

void UGuLiOutpostPresentationComponent::TickComponent(
	const float DeltaTime, const ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	RefreshColor();
	UpdatePose();
	UGuLiSceneUISourceRegistry::Notify(this, EGuLiSceneUIChange::Pose);
}

void UGuLiOutpostPresentationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	SetComponentTickEnabled(false);
	if (GetNetMode() != NM_DedicatedServer)
	{
		if (Halo) Halo->DestroyComponent();
		if (VisualMesh) VisualMesh->DestroyComponent();
	}
	if (auto* Registry = GetWorld()->GetSubsystem<UGuLiSceneUISourceRegistry>()) Registry->UnregisterSource(this);
	Super::EndPlay(Reason);
}
