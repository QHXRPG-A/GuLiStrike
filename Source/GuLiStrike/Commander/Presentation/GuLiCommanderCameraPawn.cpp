// Copyright Epic Games, Inc. All Rights Reserved.
#include "Commander/Presentation/GuLiCommanderCameraPawn.h"
#include "Commander/Presentation/GuLiCommanderCameraGeometry.h"
#include "Commander/Presentation/GuLiCommanderLandscapeQuerySubsystem.h"
#include "Commander/Presentation/GuLiCommanderHUD.h"
#include "Commander/UI/GuLiCommanderHUDWidget.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "SceneView.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogGuLiCommanderCamera, Log, All);

namespace
{
    float FollowHeight(float Current, float Target, float Dt, float HalfLife, float Speed)
    {
        const float Delta = (Target - Current) * (1.f - FMath::Pow(.5f, Dt / HalfLife));
        return Current + FMath::Clamp(Delta, -Speed * Dt, Speed * Dt);
    }
}

AGuLiCommanderCameraPawn::AGuLiCommanderCameraPawn()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
    bOnlyRelevantToOwner = true;
    SetReplicateMovement(false);
    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);
    SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CommanderSpringArm"));
    SpringArm->SetupAttachment(SceneRoot);
    SpringArm->bDoCollisionTest = false;
    SpringArm->bUsePawnControlRotation = false;
    SpringArm->bEnableCameraLag = false;
    PerspectiveCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("PerspectiveCamera"));
    PerspectiveCamera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
    PerspectiveCamera->ProjectionMode = ECameraProjectionMode::Perspective;
    PerspectiveCamera->bUsePawnControlRotation = false;
}

bool AGuLiCommanderCameraPawn::LoadConfig()
{
    if (bConfigLoaded) return true;
    const auto* Data = GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>();
    const auto* Row = Data ? Data->GetCameraConfig() : nullptr;
    if (!Row)
    {
        if (!bConfigErrorLogged)
        {
            UE_LOG(LogGuLiCommanderCamera, Error, TEXT("Commander camera requires imported Camera / Default."));
            bConfigErrorLogged = true;
        }
        return false;
    }
    Config = *Row;
    TargetHeightMeters = SmoothedHeightMeters = Config.InitialHeightMeters;
    PerspectiveCamera->FieldOfView = Config.FieldOfViewDegrees;
    bConfigLoaded = true;
    return true;
}

float AGuLiCommanderCameraPawn::PitchForHeight(float H) const
{
    const float T = FMath::Clamp((H - Config.MinimumHeightMeters) /
        (Config.TacticalStartHeightMeters - Config.MinimumHeightMeters), 0.f, 1.f);
    return -FMath::Lerp(Config.NearPitchDegrees, Config.TacticalPitchDegrees, T * T * (3 - 2 * T));
}

void AGuLiCommanderCameraPawn::SetTier(EGuLiCommanderCameraTier Tier)
{
    if (CameraTier == Tier) return;
    CameraTier = Tier;
    OnCameraTierChanged.Broadcast(Tier);
}

void AGuLiCommanderCameraPawn::InitializeSolver()
{
    if (!LoadConfig()) return;
    SpringPitchDegrees = PitchForHeight(SmoothedHeightMeters);
    FVector Pivot;
    float Arm;
    if (!SolveNormalPose(GetActorLocation(), GetActorRotation().Yaw, SmoothedHeightMeters, 0, true, Pivot, Arm)) return;
    SetActorLocationAndRotation(Pivot, FRotator(0, GetActorRotation().Yaw, 0));
    SpringArm->SetRelativeRotation(FRotator(SpringPitchDegrees, 0, 0));
    SpringArm->TargetArmLength = Arm;
    HeldCruisePivotZ = Pivot.Z;
    bSolverInitialized = true;
    SetTier(SmoothedHeightMeters < Config.TacticalStartHeightMeters ? EGuLiCommanderCameraTier::Near : EGuLiCommanderCameraTier::Tactical);
}

void AGuLiCommanderCameraPawn::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!IsLocallyControlled()) return;
    if (!bSolverInitialized) InitializeSolver();
    if (!bSolverInitialized)
    {
        PendingPlanarMovement = FVector2D::ZeroVector; PendingYawInput = PendingZoomInput = 0;
        return;
    }
    if (FMath::Abs(PendingZoomInput) > UE_SMALL_NUMBER)
    {
        if (bTransitioning)
        {
            if ((PendingZoomInput < 0 && bOverviewTarget) || (PendingZoomInput > 0 && !bOverviewTarget))
                StartOverviewTransition(!bOverviewTarget);
        }
        else if (bOverviewTarget)
        {
            if (PendingZoomInput < 0) StartOverviewTransition(false);
        }
        else
        {
            const float RequestedHeight = TargetHeightMeters * FMath::Pow(Config.ZoomStepMultiplier, PendingZoomInput);
            // Cross the logical boundary immediately; terrain/zoom interpolation must never delay the tier change.
            if (PendingZoomInput > 0 && RequestedHeight > Config.TacticalMaximumHeightMeters)
                StartOverviewTransition(true);
            else
                TargetHeightMeters = FMath::Clamp(RequestedHeight, Config.MinimumHeightMeters, Config.TacticalMaximumHeightMeters);
        }
    }
    const float Simulated = FMath::Clamp(DeltaSeconds, 0.f, 8.f / 60.f);
    if (bTransitioning || bOverviewTarget) TickOverview(FMath::Max(DeltaSeconds, 0.f));
    else if (Simulated > UE_SMALL_NUMBER)
    {
        const int32 Steps = FMath::Clamp(FMath::CeilToInt(Simulated * 60.f), 1, 8);
        const float InputScale = DeltaSeconds > UE_SMALL_NUMBER ? Simulated / DeltaSeconds : 0.f;
        for (int32 I = 0; I < Steps; ++I)
            SimulateCameraStep(Simulated / Steps, PendingPlanarMovement * (InputScale / Steps), PendingYawInput * InputScale / Steps);
    }
    PendingPlanarMovement = FVector2D::ZeroVector; PendingYawInput = PendingZoomInput = 0;
    float Ground;
    if (FindCameraGroundHeight(CurrentCameraLocation(), Ground))
        ActualHeightMeters = (CurrentCameraLocation().Z - Ground) / 100.f;
    RefreshDebugSnapshot(false, true);
#if !UE_BUILD_SHIPPING
    if (bCameraDebugEnabled) DrawCameraDebug();
#endif
}

void AGuLiCommanderCameraPawn::SimulateCameraStep(float Dt, const FVector2D& MovementSeconds, float YawSeconds)
{
    const float PreviousHeight = SmoothedHeightMeters;
    const float PreviousPitch = SpringPitchDegrees;
    const float PreviousTerrainZ = CurrentCameraLocation().Z - PreviousHeight * 100;
    SmoothedHeightMeters = FMath::FInterpTo(SmoothedHeightMeters, TargetHeightMeters, Dt, Config.ZoomInterpolationPerSecond);
    if (FMath::Abs(SmoothedHeightMeters - TargetHeightMeters) < .01f) SmoothedHeightMeters = TargetHeightMeters;
    SpringPitchDegrees = PitchForHeight(SmoothedHeightMeters);
    const float Yaw = FRotator::NormalizeAxis(GetActorRotation().Yaw + YawSeconds * Config.YawDegreesPerSecond);
    const FRotationMatrix Basis(FRotator(0, Yaw, 0));
    const float Speed = FMath::Clamp(SmoothedHeightMeters * Config.MoveHeightMultiplierPerSecond,
        Config.MinimumMoveMetersPerSecond, Config.MaximumMoveMetersPerSecond) * 100;
    const FVector Requested = GetActorLocation() + (Basis.GetUnitAxis(EAxis::X) * MovementSeconds.X +
        Basis.GetUnitAxis(EAxis::Y) * MovementSeconds.Y) * Speed;
    FVector Pivot; float Arm;
    if (!SolveNormalPose(Requested, Yaw, SmoothedHeightMeters, Dt, false, Pivot, Arm, PreviousTerrainZ))
    {
        SmoothedHeightMeters = PreviousHeight; SpringPitchDegrees = PreviousPitch;
        return;
    }
    SetActorLocationAndRotation(Pivot, FRotator(0, Yaw, 0));
    SpringArm->SetRelativeRotation(FRotator(SpringPitchDegrees, 0, 0));
    SpringArm->TargetArmLength = Arm;
    DesiredArmLength = Arm;
    HeldCruisePivotZ = Pivot.Z;
    SetTier(TargetHeightMeters < Config.TacticalStartHeightMeters ? EGuLiCommanderCameraTier::Near : EGuLiCommanderCameraTier::Tactical);
}

bool AGuLiCommanderCameraPawn::SolveNormalPose(FVector Focus, float Yaw, float HeightMeters,
    float Dt, bool bImmediate, FVector& OutPivot, float& OutArm, float PreviousTerrainZ)
{
    const float SinPitch = FMath::Abs(FMath::Sin(FMath::DegreesToRadians(SpringPitchDegrees)));
    if (SinPitch <= UE_SMALL_NUMBER || !ConstrainFocusToBattlefield(Focus)) return false;
    float Arm = HeightMeters * 100 / SinPitch;
    float FocusGround = 0, CameraGround = 0;
    // Changing pitch moves the lens across the terrain; solve its vertical height, not boom length.
    for (int32 I = 0; I < 12; ++I)
    {
        if (!FindCameraGroundHeight(Focus, FocusGround) ||
            !FindCameraGroundHeight(Focus + CalculateCameraOffset(Yaw, Arm), CameraGround)) return false;
        const float Desired = FMath::Max(1.f, (CameraGround + HeightMeters * 100 -
            FocusGround - Config.PivotClearanceMeters * 100) / SinPitch);
        if (FMath::Abs(Desired - Arm) < 1.f) break;
        Arm = FMath::Lerp(Arm, Desired, .5f);
    }
    if (!FindCameraGroundHeight(Focus + CalculateCameraOffset(Yaw, Arm), CameraGround)) return false;
    float HardZ;
    if (!CalculateRequiredPivotHeight(FVector2D(Focus), Yaw, Arm, HardZ)) return false;
    float TargetZ = CameraGround + HeightMeters * 100 - CalculateCameraOffset(Yaw, Arm).Z;
    TargetZ = FMath::Max(TargetZ, HardZ);
    if (!bImmediate && Dt > UE_SMALL_NUMBER)
    {
        const FVector2D Velocity = FVector2D(Focus - GetActorLocation()) / Dt;
        TargetZ = FMath::Max(TargetZ, CalculatePredictedRequiredPivotHeight(Focus, Yaw, Arm, Velocity, HardZ));
    }
    // Smooth terrain elevation at the lens, independently from scroll zoom and changing boom/pitch.
    const float OffsetZ = CalculateCameraOffset(Yaw, Arm).Z;
    const float TerrainTarget = TargetZ + OffsetZ - HeightMeters * 100;
    const bool bRising = TerrainTarget > PreviousTerrainZ;
    const float SmoothZ = bImmediate ? TargetZ : FollowHeight(PreviousTerrainZ, TerrainTarget, Dt,
        bRising ? Config.RiseHalfLifeSeconds : Config.DescentHalfLifeSeconds,
        100 * (bRising ? Config.MaximumRiseMetersPerSecond : Config.MaximumDescentMetersPerSecond)) + HeightMeters * 100 - OffsetZ;
    Focus.Z = FMath::Max(SmoothZ, HardZ);
    OutPivot = Focus; OutArm = Arm;
#if !UE_BUILD_SHIPPING
    LastRequestedPlanarDistance = FVector2D(Focus - GetActorLocation()).Size();
    LastAppliedPlanarDistance = LastRequestedPlanarDistance;
    LastHardRequiredPivotZ = HardZ; LastCruiseTargetPivotZ = TargetZ;
    LastEmergencyLiftAmount = FMath::Max(0.f, HardZ - SmoothZ);
    bEmergencyLiftThisFrame = LastEmergencyLiftAmount > .2f;
    if (bEmergencyLiftThisFrame && !bEmergencyLiftActive) ++EmergencyLiftCount;
    bEmergencyLiftActive = bEmergencyLiftThisFrame; bLastRequestedPoseValid = true;
#endif
    return true;
}

bool AGuLiCommanderCameraPawn::ConstrainFocusToBattlefield(FVector& Pivot) const
{
    FBox Bounds;
    if (!GuLiCommanderCameraGeometry::GetBattleBounds(GetWorld(), Bounds)) return false;
    const double Padding = Config.BoundaryPaddingMeters * 100;
    FBox2D Inner(FVector2D(Bounds.Min) + FVector2D(Padding), FVector2D(Bounds.Max) - FVector2D(Padding));
    if (Inner.GetSize().GetMin() <= 1) return false;
    // Bound the observation focus, not the lens or projected screen corners. Otherwise the
    // backward boom consumes the map margin and traps edge units behind the bottom HUD.
    Pivot.X = FMath::Clamp(Pivot.X, Inner.Min.X, Inner.Max.X);
    Pivot.Y = FMath::Clamp(Pivot.Y, Inner.Min.Y, Inner.Max.Y);
    return true;
}

bool AGuLiCommanderCameraPawn::CalculateRequiredPivotHeight(const FVector2D& XY, float Yaw,
    float Arm, float& OutZ, float* OutGround) const
{
    float Ground;
    if (!FindCameraGroundHeight(FVector(XY, 0), Ground)) return false;
    if (OutGround) *OutGround = Ground;
    OutZ = Ground + Config.PivotClearanceMeters * 100;
    const FVector Offset = CalculateCameraOffset(Yaw, Arm);
    const int32 Segments = FMath::Max(1, FMath::CeilToInt(Arm / (Config.BoomSampleSpacingMeters * 100)));
    for (int32 I = 1; I <= Segments; ++I)
    {
        const FVector Sample = Offset * (double(I) / Segments);
        if (!FindCameraGroundHeight(FVector(XY, 0) + Sample, Ground)) return false;
        const float Clearance = I == Segments ? FMath::Max(Config.MinimumHeightMeters, Config.CameraClearanceMeters)
            : Config.BoomClearanceMeters;
        OutZ = FMath::Max(OutZ, float(Ground + Clearance * 100 - Sample.Z));
    }
    return FMath::IsFinite(OutZ);
}

float AGuLiCommanderCameraPawn::CalculatePredictedRequiredPivotHeight(const FVector& Pivot, float Yaw,
    float Arm, const FVector2D& Velocity, float HardZ) const
{
    if (Velocity.IsNearlyZero()) return HardZ;
    const FVector Offset = CalculateCameraOffset(Yaw, Arm);
    for (int32 I = 1; I <= 3; ++I)
    {
        const FVector Ahead = Pivot + FVector(Velocity * (Config.LookAheadSeconds * I / 3), 0);
        float Ground;
        if (FindCameraGroundHeight(Ahead, Ground))
            HardZ = FMath::Max(HardZ, Ground + Config.PivotClearanceMeters * 100);
        if (FindCameraGroundHeight(Ahead + Offset, Ground))
            HardZ = FMath::Max(HardZ, float(Ground + Config.MinimumHeightMeters * 100 - Offset.Z));
    }
    return HardZ;
}

FVector AGuLiCommanderCameraPawn::CalculateCameraOffset(float Yaw, float Arm) const
{
    return -FRotator(SpringPitchDegrees, Yaw, 0).Vector() * Arm;
}
FVector AGuLiCommanderCameraPawn::CurrentCameraLocation() const
{
    return GetActorLocation() + CalculateCameraOffset(GetActorRotation().Yaw, SpringArm->TargetArmLength);
}
FRotator AGuLiCommanderCameraPawn::CurrentCameraRotation() const
{
    return FRotator(SpringPitchDegrees, GetActorRotation().Yaw, 0);
}
bool AGuLiCommanderCameraPawn::FindCameraGroundHeight(const FVector& At, float& Z) const
{
    const auto* Query = GetWorld()->GetSubsystem<UGuLiCommanderLandscapeQuerySubsystem>();
    if (!Query || At.ContainsNaN()) return false;
    const FVector2D XY(At);
    if (Query->TryGetLandscapeHeight(XY, Z)) return true;
    FBox2D Bounds;
    if (!Query->TryGetBounds(Bounds)) return false;
    // Camera-only continuation beyond the terrain edge lets the lens/boom follow an edge
    // focus. Interior holes still fail; gameplay traces keep using the strict Landscape query.
    constexpr double EdgeInsetCm = 1.0;
    if (Bounds.GetSize().GetMin() <= 2 * EdgeInsetCm) return false;
    const FBox2D Interior(Bounds.Min + FVector2D(EdgeInsetCm), Bounds.Max - FVector2D(EdgeInsetCm));
    if (Interior.IsInsideOrOn(XY)) return false;
    const FVector2D Edge(
        FMath::Clamp(XY.X, Interior.Min.X, Interior.Max.X),
        FMath::Clamp(XY.Y, Interior.Min.Y, Interior.Max.Y));
    return Query->TryGetLandscapeHeight(Edge, Z);
}

FBox2D AGuLiCommanderCameraPawn::GetUsableViewRect(FVector2D& Size) const
{
    const auto* PC = Cast<APlayerController>(GetController());
    int32 W = 0, H = 0;
    if (PC) PC->GetViewportSize(W, H);
    Size = FVector2D(W, H);
    FVector2D Origin = FVector2D::ZeroVector;
    if (const ULocalPlayer* LP = PC ? PC->GetLocalPlayer() : nullptr; LP && LP->ViewportClient && LP->ViewportClient->Viewport)
    {
        FSceneViewProjectionData Projection;
        if (LP->GetProjectionData(LP->ViewportClient->Viewport, Projection))
        { Origin = FVector2D(Projection.GetConstrainedViewRect().Min); Size = FVector2D(Projection.GetConstrainedViewRect().Size()); }
    }
    if (const auto* HUD = PC ? Cast<AGuLiCommanderHUD>(PC->GetHUD()) : nullptr)
        if (const auto* Widget = HUD->GetRuntimeHUDWidget()) return Widget->GetBattlefieldViewRect(Size, Origin);
    return FBox2D(FVector2D::ZeroVector, Size);
}

bool AGuLiCommanderCameraPawn::SolveOverviewPose(FVector& Location, FRotator& Rotation) const
{
    FBox Bounds;
    if (!GuLiCommanderCameraGeometry::GetBattleBounds(GetWorld(), Bounds)) return false;
    FVector2D Size;
    FBox2D Rect = GetUsableViewRect(Size);
    if (Size.GetMin() <= 1 || Rect.GetSize().GetMin() <= 1) return false;
    const FVector2D Pad = Rect.GetSize() * Config.OverviewPaddingFraction;
    Rect.Min += Pad; Rect.Max -= Pad;
    double TX = FMath::Tan(FMath::DegreesToRadians(Config.FieldOfViewDegrees * .5));
    double TY = TX * Size.Y / Size.X;
    const auto* PC = Cast<APlayerController>(GetController());
    if (const ULocalPlayer* LP = PC ? PC->GetLocalPlayer() : nullptr)
    {
        FSceneViewProjectionData Projection;
        if (LP->ViewportClient && LP->ViewportClient->Viewport &&
            LP->GetProjectionData(LP->ViewportClient->Viewport, Projection) &&
            Projection.ProjectionMatrix.M[0][0] > 0 && Projection.ProjectionMatrix.M[1][1] > 0)
        {
            TX = 1. / Projection.ProjectionMatrix.M[0][0];
            TY = 1. / Projection.ProjectionMatrix.M[1][1];
        }
    }
    // Align to the nearest map axis, never force the player back to one compass heading.
    // ReturnYaw stays fixed across reversals and reframing, so the chosen quarter-turn cannot drift.
    const float RelativeYaw = FRotator::NormalizeAxis(ReturnYaw - Config.OverviewYawDegrees);
    const float OverviewYaw = FRotator::NormalizeAxis(Config.OverviewYawDegrees +
        90.f * FMath::RoundToInt(RelativeYaw / 90.f));
    const FRotator OverviewRotation(-Config.OverviewPitchDegrees, OverviewYaw, 0);
    const FRotationMatrix Basis(OverviewRotation);
    const FVector Right = Basis.GetUnitAxis(EAxis::Y), Up = Basis.GetUnitAxis(EAxis::Z);
    const FVector Center = Bounds.GetCenter();
    const FVector2D NdcCenter(2 * Rect.GetCenter().X / Size.X - 1, 1 - 2 * Rect.GetCenter().Y / Size.Y);
    const auto PositionAt = [&](double Height)
    {
        FVector P = Center - Right * (NdcCenter.X * Height * TX) - Up * (NdcCenter.Y * Height * TY);
        P.Z = Center.Z + Height;
        return P;
    };
    const auto Fits = [&](double Height)
    {
        const FVector P = PositionAt(Height);
        for (int32 I = 0; I < 8; ++I)
        {
            const FVector Corner(I & 1 ? Bounds.Max.X : Bounds.Min.X, I & 2 ? Bounds.Max.Y : Bounds.Min.Y,
                I & 4 ? Bounds.Max.Z : Bounds.Min.Z);
            const FVector Delta = Corner - P;
            const double Depth = -Delta.Z;
            if (Depth <= 1) return false;
            const FVector2D Pixel((1 + FVector::DotProduct(Delta, Right) / (Depth * TX)) * Size.X * .5,
                (1 - FVector::DotProduct(Delta, Up) / (Depth * TY)) * Size.Y * .5);
            if (!Rect.IsInsideOrOn(Pixel)) return false;
        }
        return true;
    };
    double Low = FMath::Max(double(Config.TacticalMaximumHeightMeters * 100 + 100), Bounds.GetExtent().Z + 100);
    double High = Low;
    for (int32 I = 0; I < 32 && !Fits(High); ++I) High *= 2;
    if (!Fits(High) || !FMath::IsFinite(High)) return false;
    for (int32 I = 0; I < 32; ++I)
    {
        const double Mid = (Low + High) * .5;
        if (Fits(Mid)) High = Mid; else Low = Mid;
    }
    Location = PositionAt(High);
    Rotation = OverviewRotation;
    return !Location.ContainsNaN();
}

void AGuLiCommanderCameraPawn::StartOverviewTransition(bool bEnter)
{
    const FVector From = CurrentCameraLocation();
    const FRotator FromRotation = CurrentCameraRotation();
    if (bEnter && !IsOverviewPresentation())
    {
        ReturnFocus = GetActorLocation(); ReturnYaw = GetActorRotation().Yaw;
    }
    FVector Destination; FRotator DestinationRotation;
    if (bEnter)
    {
        if (!SolveOverviewPose(Destination, DestinationRotation)) return;
        float Ground = 0;
        if (FindCameraGroundHeight(Destination, Ground)) OverviewTargetHeightMeters = (Destination.Z - Ground) / 100;
    }
    else
    {
        const float PreviousPitch = SpringPitchDegrees;
        SpringPitchDegrees = -Config.TacticalPitchDegrees;
        const bool bSolved = SolveNormalPose(ReturnFocus, ReturnYaw, Config.TacticalMaximumHeightMeters, 0, true, ReturnPivot, ReturnArm);
        Destination = ReturnPivot + CalculateCameraOffset(ReturnYaw, ReturnArm);
        SpringPitchDegrees = PreviousPitch;
        if (!bSolved) return;
        DestinationRotation = FRotator(-Config.TacticalPitchDegrees, ReturnYaw, 0);
    }
    TransitionFromLocation = From; TransitionFromRotation = FromRotation;
    TransitionToLocation = Destination; TransitionToRotation = DestinationRotation;
    TransitionElapsed = 0; bTransitioning = true; bOverviewTarget = bEnter;
    // Overview is one fixed zoom slot. The inward edge always lands at the tactical ceiling.
    TargetHeightMeters = Config.TacticalMaximumHeightMeters;
    SetTier(EGuLiCommanderCameraTier::Overview);
    ApplyCameraPose(From, FromRotation);
    LastUsableViewRect = GetUsableViewRect(LastViewSize);
    LastLandscapeRevision = GetWorld()->GetSubsystem<UGuLiCommanderLandscapeQuerySubsystem>()->GetCacheRevision();
}

void AGuLiCommanderCameraPawn::ApplyCameraPose(const FVector& Location, const FRotator& Rotation)
{
    SetActorLocationAndRotation(Location, FRotator(0, Rotation.Yaw, 0));
    SpringPitchDegrees = Rotation.Pitch;
    SpringArm->TargetArmLength = 0;
    SpringArm->SetRelativeRotation(FRotator(SpringPitchDegrees, 0, 0));
}

void AGuLiCommanderCameraPawn::TickOverview(float Dt)
{
    FVector2D Size;
    const FBox2D Rect = GetUsableViewRect(Size);
    const uint32 Revision = GetWorld()->GetSubsystem<UGuLiCommanderLandscapeQuerySubsystem>()->GetCacheRevision();
    if (bOverviewTarget && (!Size.Equals(LastViewSize) || !Rect.Min.Equals(LastUsableViewRect.Min) ||
        !Rect.Max.Equals(LastUsableViewRect.Max) || Revision != LastLandscapeRevision))
        StartOverviewTransition(true);
    if (!bTransitioning) return;
    TransitionElapsed += Dt;
    const float T = FMath::Clamp(TransitionElapsed / Config.OverviewTransitionSeconds, 0.f, 1.f);
    const float Ease = T * T * (3 - 2 * T);
    FVector Position = FMath::Lerp(TransitionFromLocation, TransitionToLocation, Ease);
    // Interpolate the rig's two axes directly. Quaternion-to-Euler conversion at -90 pitch
    // can exchange yaw and roll, but this rig has no roll and must keep its heading continuous.
    const FRotator Rotation(
        FMath::Lerp(TransitionFromRotation.Pitch, TransitionToRotation.Pitch, Ease),
        TransitionFromRotation.Yaw +
            FMath::FindDeltaAngleDegrees(TransitionFromRotation.Yaw, TransitionToRotation.Yaw) * Ease,
        0);
    float Ground;
    if (FindCameraGroundHeight(Position, Ground))
        Position.Z = FMath::Max(Position.Z, double(Ground + Config.MinimumHeightMeters * 100));
    ApplyCameraPose(Position, Rotation);
    if (T >= 1)
    {
        bTransitioning = false;
        // Commit the exact destination orientation, including the nearest aligned overview heading.
        ApplyCameraPose(TransitionToLocation, TransitionToRotation);
        if (!bOverviewTarget)
        {
            SetActorLocationAndRotation(ReturnPivot, FRotator(0, ReturnYaw, 0));
            SpringPitchDegrees = -Config.TacticalPitchDegrees;
            SpringArm->SetRelativeRotation(FRotator(SpringPitchDegrees, 0, 0));
            SpringArm->TargetArmLength = ReturnArm;
            TargetHeightMeters = SmoothedHeightMeters = Config.TacticalMaximumHeightMeters;
            SetTier(EGuLiCommanderCameraTier::Tactical);
        }
    }
}

void AGuLiCommanderCameraPawn::AddPlanarMovement(FVector2D Movement)
{
    if (!IsOverviewPresentation() && !Movement.ContainsNaN()) PendingPlanarMovement += Movement;
}
void AGuLiCommanderCameraPawn::AddYawInput(float Value)
{
    if (!IsOverviewPresentation() && FMath::IsFinite(Value)) PendingYawInput += Value;
}
void AGuLiCommanderCameraPawn::AddZoomInput(float Value)
{
    if (FMath::IsFinite(Value)) PendingZoomInput += Value;
}
void AGuLiCommanderCameraPawn::SetRequestedHeightMeters(float HeightMeters)
{
    if (!IsLocallyControlled() || !FMath::IsFinite(HeightMeters)) return;
    if (!bSolverInitialized) InitializeSolver();
    if (!bSolverInitialized) return;
    if (IsOverviewPresentation()) JumpToWorldLocation(ReturnFocus);
    if (IsOverviewPresentation()) return;
    TargetHeightMeters = FMath::Clamp(HeightMeters, Config.MinimumHeightMeters, Config.TacticalMaximumHeightMeters);
}
void AGuLiCommanderCameraPawn::JumpToWorldLocation(FVector Location)
{
    if (!IsLocallyControlled() || Location.ContainsNaN()) return;
    if (!bSolverInitialized) InitializeSolver();
    if (!bSolverInitialized) return;
    const bool bWasOverview = IsOverviewPresentation();
    const float Yaw = bWasOverview ? ReturnYaw : GetActorRotation().Yaw;
    const float H = bWasOverview ? Config.TacticalMaximumHeightMeters : TargetHeightMeters;
    const float PreviousPitch = SpringPitchDegrees;
    SpringPitchDegrees = PitchForHeight(H);
    FVector Pivot; float Arm;
    if (!SolveNormalPose(Location, Yaw, H, 0, true, Pivot, Arm)) { SpringPitchDegrees = PreviousPitch; return; }
    bOverviewTarget = bTransitioning = false;
    SetActorLocationAndRotation(Pivot, FRotator(0, Yaw, 0));
    SpringArm->SetRelativeRotation(FRotator(SpringPitchDegrees, 0, 0)); SpringArm->TargetArmLength = Arm;
    TargetHeightMeters = SmoothedHeightMeters = H;
    PendingPlanarMovement = FVector2D::ZeroVector; PendingYawInput = PendingZoomInput = 0;
    SetTier(H < Config.TacticalStartHeightMeters ? EGuLiCommanderCameraTier::Near : EGuLiCommanderCameraTier::Tactical);
}

void AGuLiCommanderCameraPawn::RefreshDebugSnapshot(bool bClamped, bool bTerrainValid)
{
#if !UE_BUILD_SHIPPING
    DebugSnapshot = {};
    DebugSnapshot.PivotLocation = GetActorLocation(); DebugSnapshot.CameraLocation = CurrentCameraLocation();
    DebugSnapshot.DesiredArmLength = DesiredArmLength; DebugSnapshot.EffectiveArmLength = SpringArm->TargetArmLength;
    DebugSnapshot.CameraClearance = ActualHeightMeters * 100; DebugSnapshot.bTerrainValid = bTerrainValid;
    DebugSnapshot.bFootprintClamped = bClamped; DebugSnapshot.bRequestedPoseValid = bLastRequestedPoseValid;
    DebugSnapshot.HardRequiredPivotZ = LastHardRequiredPivotZ; DebugSnapshot.CruiseTargetPivotZ = LastCruiseTargetPivotZ;
    DebugSnapshot.HeldCruisePivotZ = GetActorLocation().Z; DebugSnapshot.EmergencyLiftCount = EmergencyLiftCount;
    DebugSnapshot.EmergencyLiftAmount = LastEmergencyLiftAmount; DebugSnapshot.bEmergencyLift = bEmergencyLiftThisFrame;
    DebugSnapshot.RequestedPlanarDistance = LastRequestedPlanarDistance; DebugSnapshot.AppliedPlanarDistance = LastAppliedPlanarDistance;
    DebugSnapshot.AppliedPlanarRatio = LastRequestedPlanarDistance > UE_SMALL_NUMBER ? LastAppliedPlanarDistance / LastRequestedPlanarDistance : 1;
    const auto* Landscape = GetWorld()->GetSubsystem<UGuLiCommanderLandscapeQuerySubsystem>();
    DebugSnapshot.bLandscapeValid = Landscape && Landscape->TryGetBounds(DebugSnapshot.LandscapeBounds);
    float Ground;
    if (FindCameraGroundHeight(GetActorLocation(), Ground)) { DebugSnapshot.GroundHeight = Ground; DebugSnapshot.PivotClearance = GetActorLocation().Z - Ground; }
#endif
}
#if !UE_BUILD_SHIPPING
void AGuLiCommanderCameraPawn::DrawCameraDebug() const
{
    if (GEngine) GEngine->AddOnScreenDebugMessage(-int32(GetUniqueID()), 0, FColor::Cyan, FString::Printf(
        TEXT("CommanderCamera tier=%d target=%.1fm actual=%.1fm pitch=%.1f transition=%d source=Camera/Default"),
        int32(CameraTier), GetTargetHeightMeters(), ActualHeightMeters, -SpringPitchDegrees, bTransitioning));
}
namespace
{
    FAutoConsoleCommandWithWorldAndArgs CameraHeightCommand(TEXT("gs.GM.Commander.Camera.Height"),
        TEXT("Request an exact camera height in meters for manual boundary observation; overview still uses the wheel."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
        {
            float Height = 0;
            if (!World || Args.Num() != 1 || !LexTryParseString(Height, *Args[0]) || !FMath::IsFinite(Height)) return;
            for (TActorIterator<AGuLiCommanderCameraPawn> It(World); It; ++It)
                if (It->IsLocallyControlled()) It->SetRequestedHeightMeters(Height);
        }));
    FAutoConsoleCommandWithWorldAndArgs CameraDebugCommand(TEXT("gs.GM.Commander.Camera.Debug"),
        TEXT("Commander camera state from Excel: <0|1>."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
        {
            if (!World || Args.Num() != 1) return;
            for (TActorIterator<AGuLiCommanderCameraPawn> It(World); It; ++It)
                if (It->IsLocallyControlled()) It->SetCameraDebugEnabled(Args[0] == TEXT("1"));
        }));
}
#endif
