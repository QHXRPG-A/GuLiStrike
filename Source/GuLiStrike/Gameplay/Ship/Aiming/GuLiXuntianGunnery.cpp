#include "Gameplay/Ship/Aiming/GuLiXuntianGunnery.h"
#include "Gameplay/Ship/Aiming/GuLiXuntianBarrelAnim.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Gameplay/Ship/GuLiStrikeWeaponPart.h"
#include "Gameplay/CombatEffects/GuLiProjectilePoolSubsystem.h"
#include "Gameplay/Data/Generated/GuLiVfxIds.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Network/GuLiHybridManifest.h"
#include "Network/GuLiHybridAdapterRegistry.h"
#include "Network/Private/GuLiHybridComponentState.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Network/Private/GuLiHybridStateArchive.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/World.h"

namespace
{
struct FBatteryProfile { FName Skill; float Damage, Interval, Speed, TurnRate; };
const FBatteryProfile Air{TEXT("Xuntian.AirGuns"), 10, .105f, 30500, 43};
const FBatteryProfile Ground{TEXT("Xuntian.BellyGuns"), 30, .34f, 20000, 26};

FTransform RefBone(const FReferenceSkeleton& Ref, int32 Index)
{
    FTransform Result;
    for (; Index != INDEX_NONE; Index = Ref.GetParentIndex(Index))
        Result *= Ref.GetRefBonePose()[Index];
    return Result;
}
double ArcMargin(const FVector& Local,float MinDeg,float MaxDeg)
{
    if (Local.IsNearlyZero()) return -180.;
    const double Angle=FMath::RadiansToDegrees(FMath::Atan2(-Local.Z,Local.Size2D()));
    // The nadir is inside an unrestricted 90 degree upper stop.
    return (MaxDeg>=90.f ? Angle-MinDeg : FMath::Min(Angle-MinDeg,MaxDeg-Angle))+.0001;
}
}

UGuLiXuntianGunnery::UGuLiXuntianGunnery() { Cards.SetNum(4); }
AGuLiStrikeShip* UGuLiXuntianGunnery::Ship() const { return CastChecked<AGuLiStrikeShip>(GetOwner()); }

void UGuLiXuntianGunnery::BeginPlay()
{
    Super::BeginPlay();
    if (!ValidLimits(GroundMinDepression, GroundMaxDepression))
    {
        GroundMinDepression = 45;
        GroundMaxDepression = 90;
    }
    if (auto* H = GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>())
        H->OnCombatKill.AddUObject(this, &ThisClass::OnDamage);
}
void UGuLiXuntianGunnery::EndPlay(const EEndPlayReason::Type Reason)
{
    if (auto* H = GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()) H->OnCombatKill.RemoveAll(this);
    Super::EndPlay(Reason);
}
bool UGuLiXuntianGunnery::ValidLimits(float MinDeg, float MaxDeg)
{
    return FMath::IsFinite(MinDeg) && FMath::IsFinite(MaxDeg) && MinDeg >= 0 && MinDeg <= MaxDeg && MaxDeg <= 90;
}
bool UGuLiXuntianGunnery::Covers(const FVector& V, float MinDeg, float MaxDeg)
{
    return ArcMargin(V,MinDeg,MaxDeg)>=0;
}
void UGuLiXuntianGunnery::GetGroundGunLimits(float& MinDeg, float& MaxDeg) const
{
    MinDeg = GroundMinDepression;
    MaxDeg = GroundMaxDepression;
}
bool UGuLiXuntianGunnery::SetGroundGunLimits(float MinDeg, float MaxDeg)
{
    const auto* Binding = UGuLiHybridActorComponent::Find(Ship());
    if (!ValidLimits(MinDeg,MaxDeg) || !Ship()->HasAuthority() || (Binding && !UGuLiHybridActorComponent::HasSimulation(Ship()))) return false;
    GroundMinDepression = MinDeg;
    GroundMaxDepression = MaxDeg;
    NextHint = 0;
    return true;
}
bool UGuLiXuntianGunnery::BindRangeHull(FGuLiHybridManifestData& Data,FString& Error)
{
    const auto* Path=Data.Strings.Find(TEXT("startup/ship_class"));
    auto* Class=Path?LoadClass<AGuLiStrikeShip>(nullptr,**Path):nullptr;
    return Class && FGuLiHybridAdapterRegistry::BindDefinition(Data,TEXT("range/xuntian_target"),TEXT("XuntianRangeHull"),Error)
        && FGuLiHybridComponentState::BindDefinitionLayout(Data,TEXT("range/xuntian_target"),*Class,Error);
}
void UGuLiXuntianGunnery::AddManifestRules(FGuLiHybridManifestData& Data)
{
    for (const auto& Profile : {Air, Ground})
    {
        auto& Rule = Data.Rules.AddDefaulted_GetRef();
        Rule.Kind = EGuLiHybridRuleKind::Damage;
        Rule.SourceDefinition = TEXT("player/ship");
        Rule.Id = FGuLiHybridManifest::MakeDamageRuleId(Rule.SourceDefinition, Profile.Skill, Profile.Skill);
        Rule.Damage = Profile.Damage;
        Rule.MaximumTargets = 1;
        Rule.EffectTicks = 900;
        Rule.CooldownTicks = 0; // Each independently admitted pooled round settles once.
    }
}
void UGuLiXuntianGunnery::SetTrigger(bool Down) { bTrigger = Down; }
void UGuLiXuntianGunnery::ResetControls()
{
    bTrigger = bHasAim = bViewStarted = bGroundHint = bGround = bAimingAtSurface = false;
    ViewPitch = InitialViewPitch;
    Focus = {};
    Feed.Reset();
    NextContacts = NextHint = 0;
}
void UGuLiXuntianGunnery::ApplyRemoteAim(FVector Point, bool GroundBank, float MinDeg, float MaxDeg)
{
    AimPoint = Point;
    bGround = GroundBank;
    bHasAim = true;
    GroundMinDepression = MinDeg;
    GroundMaxDepression = MaxDeg;
}

void FGuLiXuntianBatteryState::SerializeAim(FGuLiHybridStateArchive& Ar)
{
    Ar.Value(bHasAim); Ar.Value(bGround); Ar.Value(Point); Ar.Value(MinAngle); Ar.Value(MaxAngle);
    if (!UGuLiXuntianGunnery::ValidLimits(MinAngle,MaxAngle)) Ar.Reject();
}
void FGuLiXuntianBatteryState::Serialize(FGuLiHybridStateArchive& Ar)
{
    SerializeAim(Ar);
    Ar.Value(Cooldown); Ar.Value(NextAir); Ar.Value(NextGround);
    Ar.Array(Bores,128,[](auto& A, auto& B)
    {
        A.Value(B.Socket); A.Value(B.Aim); A.Value(B.Barrel);
        if (B.Socket.IsNone() || B.Barrel<0 || B.Barrel>=16) A.Reject();
    });
    if (Cooldown<0 || Cooldown>1 || NextAir<0 || NextAir>=128 || NextGround<0 || NextGround>=128) Ar.Reject();
}
void UGuLiXuntianGunnery::ReadState(FGuLiXuntianBatteryState& S, bool Full) const
{
    S.Point=AimPoint; S.MinAngle=GroundMinDepression; S.MaxAngle=GroundMaxDepression;
    S.bGround=bGround; S.bHasAim=bHasAim;
    if (!Full) return;
    S.Cooldown=Cooldown; S.NextAir=NextAir; S.NextGround=NextGround;
    S.Bores.Reset(Mounts.Num());
    for (const auto& M:Mounts) S.Bores.Add({M.Socket,M.Aim,M.Barrel});
}
void UGuLiXuntianGunnery::RestoreState(const FGuLiXuntianBatteryState& S, bool Full)
{
    ApplyRemoteAim(S.Point,S.bGround,S.MinAngle,S.MaxAngle); bHasAim=S.bHasAim;
    if (!Full) return;
    bTrigger=false; Cooldown=S.Cooldown; NextAir=S.NextAir; NextGround=S.NextGround;
    RestartMountCache(); RefreshMounts();
    for (auto& M:Mounts)
        if (const auto* B=S.Bores.FindByPredicate([&](const auto& E){return E.Socket==M.Socket;}))
        { M.Aim=B->Aim; M.Barrel=B->Barrel%M.Bores.Num(); }
}

void UGuLiXuntianGunnery::RefreshMounts()
{
    const uint32 Revision = Ship()->GetLoadoutRevision();
    if (Revision == SeenLoadout) return;
    SeenLoadout = Revision;
    TArray<FGuLiXuntianMount> Previous = MoveTemp(Mounts);
    for (const auto& Entry : Ship()->GetInstalledParts())
    {
        auto* Part = Cast<UGuLiStrikeWeaponPart>(Entry.Part.Get());
        const FString Slot = Entry.SocketName.ToString();
        const bool bBelly = Slot.StartsWith(TEXT("bottom_"));
        if (!Part || (!bBelly && !Slot.StartsWith(TEXT("air_")))) continue;
        auto* Mesh = Part->GetSkeletalVisualComponent();
        const auto* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
        if (!Asset) continue;
        if (auto* Existing = Previous.FindByPredicate([&](const auto& G){return G.Part.Get() == Part && G.Mesh.Get() == Mesh;}))
        {
            Mounts.Add(MoveTemp(*Existing));
            continue;
        }
        const auto& Ref = Asset->GetRefSkeleton();
        const int32 Hinge = Ref.FindBoneIndex(TEXT("BarrelPitch"));
        if (Hinge == INDEX_NONE) continue;
        const FTransform Pivot = RefBone(Ref, Hinge);
        FGuLiXuntianMount Gun;
        Gun.Socket = Entry.SocketName; Gun.Part = Part; Gun.Mesh = Mesh; Gun.bGround = bBelly;
        Gun.RestVisual = Mesh->GetRelativeTransform(); Gun.Hinge = Pivot.GetLocation();
        Gun.HingeAxes = Pivot.GetRotation();
        for (int32 Index = 1; Index <= 16; ++Index)
        {
            const auto* Socket = Asset->FindSocket(FName(*FString::Printf(TEXT("Socket_%d"),Index)));
            if (!Socket || Socket->BoneName != TEXT("BarrelPitch")) break;
            Gun.Bores.Add(FTransform(Socket->RelativeRotation,Socket->RelativeLocation,Socket->RelativeScale) * Pivot);
        }
        if (Gun.Bores.IsEmpty()) continue;
        if (GetWorld()->GetNetMode() != NM_DedicatedServer)
            Mesh->SetAnimInstanceClass(UGuLiXuntianBarrelAnim::StaticClass());
        Mounts.Add(MoveTemp(Gun));
    }
    Mounts.Sort([](const auto& A,const auto& B){return A.Socket.LexicalLess(B.Socket);});
    BellyMounts.Reset();
    for (int32 I=0;I<Mounts.Num();++I) if (Mounts[I].bGround) BellyMounts.Add(I);
}


namespace
{
struct FBarrelPoseWorld
{
    FQuat Yaw,Pitch;
    FVector Hinge,Muzzle;
};
FBarrelPoseWorld PoseFor(const FGuLiXuntianBoreGeometry& G,const FVector& Direction)
{
    const FVector Flat=FVector::VectorPlaneProject(Direction,G.Up).GetSafeNormal(UE_SMALL_NUMBER,G.FlatForward);
    FBarrelPoseWorld Pose;
    Pose.Yaw=FQuat(G.Up,FMath::Atan2(G.Up.Dot(G.FlatForward.Cross(Flat)),G.FlatForward.Dot(Flat)));
    Pose.Pitch=FQuat::FindBetweenNormals(Pose.Yaw.RotateVector(G.RestForward),Direction);
    Pose.Hinge=G.MountOrigin+Pose.Yaw.RotateVector(G.RestHingeOffset);
    Pose.Muzzle=Pose.Hinge+Pose.Pitch.RotateVector(Pose.Yaw.RotateVector(G.RestOffset));
    return Pose;
}
// Solve against the selected physical bore. Its muzzle moves during yaw and elevation.
FVector BoreTo(const FGuLiXuntianBoreGeometry& G,const FVector& Point)
{
    FVector Direction=(Point-G.MountOrigin).GetSafeNormal(UE_SMALL_NUMBER,G.RestForward);
    for (int32 I=0;I<8;++I)
    {
        const FVector Corrected=(Point-PoseFor(G,Direction).Muzzle).GetSafeNormal(UE_SMALL_NUMBER,Direction);
        if (FVector::DistSquared(Corrected,Direction)<1.e-12) return Corrected;
        Direction=Corrected;
    }
    return Direction;
}
double GroundMargin(const FGuLiXuntianBoreGeometry& G,const FVector& Point,const FVector& Direction,const FQuat& Hull,float Min,float Max)
{
    const double Reach=UGuLiXuntianGunnery::Reach;
    const double Range=FVector::Dist(Point,PoseFor(G,Direction).Muzzle);
    return FMath::Min(ArcMargin(Hull.UnrotateVector(Direction),Min,Max),FMath::RadiansToDegrees(FMath::Atan2(Reach+.01-Range,Reach)));
}
}

void UGuLiXuntianGunnery::UpdateGun(FGuLiXuntianMount& G, double Delta)
{
    auto* Part=G.Part.Get(); auto* Mesh=G.Mesh.Get();
    if (!Part || !Mesh || G.Bores.IsEmpty()) { G.bHasGeometry=G.bReachable=false; return; }
    G.Frame=G.RestVisual*Part->GetComponentTransform();
    const auto& Socket=G.Bores[G.Barrel%G.Bores.Num()];
    const FVector LocalRest=Socket.GetRotation().GetForwardVector();
    G.RestForward=G.Frame.TransformVectorNoScale(LocalRest).GetSafeNormal();
    G.Up=Ship()->GetActorUpVector();
    G.FlatForward=FVector::VectorPlaneProject(G.RestForward,G.Up).GetSafeNormal(UE_SMALL_NUMBER,Ship()->GetActorForwardVector());
    G.MountOrigin=G.Frame.GetLocation();
    G.RestHingeOffset=G.Frame.TransformVector(G.Hinge);
    G.RestOffset=G.Frame.TransformVector(Socket.GetLocation()-G.Hinge);
    G.bHasGeometry=true;
    const bool Tracking=bHasAim && G.bGround==bGround;
    const FQuat Hull=Ship()->GetActorQuat();
    FVector Desired=Tracking ? BoreTo(G,AimPoint) : G.RestForward;
    G.bReachable=Tracking && (!G.bGround || GroundMargin(G,AimPoint,Desired,Hull,GroundMinDepression,GroundMaxDepression)>=0);
    auto Limit=[&](FVector D)
    {
        if (!G.bGround) return D;
        FRotator R=Hull.UnrotateVector(D).Rotation();
        R.Pitch=FMath::Clamp(R.Pitch,-double(GroundMaxDepression),-double(GroundMinDepression));
        return Hull.RotateVector(R.Vector());
    };
    Desired=Limit(Desired);
    const FQuat Wanted=FQuat::FindBetweenNormals(LocalRest,G.Frame.InverseTransformVectorNoScale(Desired));
    const double Distance=G.Aim.AngularDistance(Wanted);
    const double Step=FMath::DegreesToRadians((G.bGround?Ground:Air).TurnRate)*Delta;
    G.Aim=FQuat::Slerp(G.Aim,Wanted,Distance>UE_SMALL_NUMBER?FMath::Min(1.,Step/Distance):1.).GetNormalized();
    G.Direction=Limit(G.Frame.TransformVectorNoScale(G.Aim.RotateVector(LocalRest)).GetSafeNormal());
    G.Aim=FQuat::FindBetweenNormals(LocalRest,G.Frame.InverseTransformVectorNoScale(G.Direction));
    const auto Pose=PoseFor(G,G.Direction);
    G.WorldHinge=Pose.Hinge; G.Muzzle=Pose.Muzzle;
    G.BoreDistance=FVector::Distance(AimPoint,G.Muzzle);
    if (auto* Anim=Cast<UGuLiXuntianBarrelAnim>(Mesh->GetAnimInstance()))
    {
        const FQuat WorldBase=Pose.Yaw*G.Frame.GetRotation();
        Mesh->SetWorldRotation(WorldBase);
        const FQuat HingeAxes=WorldBase*G.HingeAxes;
        Anim->HingeRotation=(HingeAxes.Inverse()*Pose.Pitch*HingeAxes).GetNormalized();
    }
}

int32 UGuLiXuntianGunnery::CoverageAt(const FVector& Point) const
{
    int32 Count=0;
    const FQuat Hull=Ship()->GetActorQuat();
    for (const int32 I:BellyMounts)
    {
        const auto& G=Mounts[I];
        if (G.bHasGeometry && G.Part.IsValid() && GroundMargin(G,Point,BoreTo(G,Point),Hull,GroundMinDepression,GroundMaxDepression)>=0) ++Count;
    }
    return Count;
}
FGuLiXuntianGroundCoverage UGuLiXuntianGunnery::SnapshotGroundCoverage() const
{
    FGuLiXuntianGroundCoverage Coverage;
    Coverage.Hull=Ship()->GetActorQuat();
    Coverage.MinDepression=GroundMinDepression;
    Coverage.MaxDepression=GroundMaxDepression;
    for (const int32 I : BellyMounts)
    {
        const auto& G=Mounts[I];
        if (G.bHasGeometry && G.Part.IsValid()) Coverage.Bores.Add(G);
    }
    return Coverage;
}

double FGuLiXuntianGroundCoverage::MarginAt(const FVector& Point) const
{
    double Best=-180.;
    for (const auto& Bore : Bores)
        Best=FMath::Max(Best,GroundMargin(Bore,Point,BoreTo(Bore,Point),Hull,MinDepression,MaxDepression));
    return Best;
}

void UGuLiXuntianGunnery::Advance(double Delta, bool bAllowFire)
{
    RefreshMounts();
    for (auto& G : Mounts) UpdateGun(G,Delta);
    Cooldown = FMath::Max(0.,Cooldown-Delta);
    if (bAllowFire && bHasAim && bTrigger && Cooldown <= UE_SMALL_NUMBER && Fire())
        Cooldown = (bGround ? Ground : Air).Interval;
}
bool UGuLiXuntianGunnery::Fire()
{
    auto* Pool = GetWorld()->GetSubsystem<UGuLiProjectilePoolSubsystem>();
    auto* Ledger = GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>();
    auto* Health = Ship()->GetCombatHealthComponent();
    const auto* PS = Ship()->GetPlayerState<AGuLiBattlePlayerState>();
    if (!Pool || !Ledger || !PS || !Health->IsAlive() || !Ship()->CanAcceptServerIntent()) return false;
    const FBatteryProfile& Profile = bGround ? Ground : Air;
    int32& Next = bGround ? NextGround : NextAir;
    for (int32 I=0; I<Mounts.Num(); ++I)
    {
        const int32 Index = (Next + I) % Mounts.Num();
        auto& G = Mounts[Index];
        if (!G.Part.IsValid() || G.bGround != bGround || !G.bReachable) continue;
        FGuLiPooledProjectileLaunch Shot;
        auto& C = Shot.Context;
        C.Source = Health->GetTargetHandle();
        C.Target = Focus; C.MatchEpoch = Ledger->GetMatchEpoch(); C.SkillId = Profile.Skill;
        C.ShotId = C.RootEventId = FGuid::NewGuid();
        C.WeaponBinding.MatchEpoch = C.MatchEpoch; C.WeaponBinding.Team = Health->GetCombatTeam();
        C.WeaponBinding.OwnerPlayerGuid = PS->GetPlayerGuid(); C.WeaponBinding.Domain = EGuLiWeaponDomain::ShipMounted;
        C.WeaponBinding.SubjectId = TEXT("5002"); C.WeaponBinding.SlotId = Profile.Skill;
        C.Damage = Profile.Damage; C.LoadoutRevision = Ship()->GetLoadoutRevision();
        Shot.Position = G.Muzzle; Shot.Direction = G.Direction; Shot.Speed = Profile.Speed;
        Shot.MaximumDistance = Reach; Shot.Lifetime = Reach / Shot.Speed; Shot.ServerTime = GetWorld()->GetTimeSeconds();
        Shot.PlayerBulletVfxId = GuLiVfxIds::PlayerBullet;
        if (!Pool->Launch(Shot).IsValid()) return false;
        G.Barrel = (G.Barrel + 1) % G.Bores.Num();
        Next = (Index+1) % Mounts.Num();
        return true;
    }
    return false;
}
