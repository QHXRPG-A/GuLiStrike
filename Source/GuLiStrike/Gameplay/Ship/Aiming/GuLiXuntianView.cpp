#include "Gameplay/Ship/Aiming/GuLiXuntianGunnery.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Camera/CameraTypes.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "LandscapeProxy.h"

namespace
{
float InitialYaw(const AGuLiStrikeShip& Ship)
{
    return FRotator::NormalizeAxis(Ship.GetActorRotation().Yaw+FMath::RadiansToDegrees(.41));
}
FCollisionQueryParams OwnShipQuery(const AGuLiStrikeShip& Ship)
{
    FCollisionQueryParams Q(SCENE_QUERY_STAT(XuntianSight),true,&Ship);
    TArray<AActor*> Attached;
    Ship.GetAttachedActors(Attached,true,true);
    Q.AddIgnoredActors(Attached);
    return Q;
}
bool SurfaceReceiver(const FHitResult& Hit)
{
    const auto* A = Hit.GetActor(); const auto* C = Hit.GetComponent();
    if (!A || !C) return false;
    if (A->IsA<ALandscapeProxy>() || A->ActorHasTag(TEXT("ShipHUD.Surface")) || C->ComponentHasTag(TEXT("ShipHUD.Surface"))) return true;
    const auto* Mesh = Cast<UStaticMeshComponent>(C);
    const FString Name = Mesh && Mesh->GetStaticMesh() ? Mesh->GetStaticMesh()->GetName() : C->GetClass()->GetName();
    return Name.Contains(TEXT("Pavement")) || Name.Contains(TEXT("Road")) || Name.Contains(TEXT("Water")) ||
           Name.Contains(TEXT("Terrain")) || Name.Contains(TEXT("Floor")) || Name.Contains(TEXT("GroundPlane"));
}
bool IsFlying(const FGuLiCombatTargetSnapshot& C)
{
    return C.Handle.Kind == EGuLiTargetKind::Ship || C.Handle.Kind == EGuLiTargetKind::Wingman;
}
FVector ContactCenter(const FGuLiCombatTargetSnapshot& C)
{
    if (const auto* S = Cast<AGuLiStrikeShip>(C.CollisionActor.Get()))
        return S->GetHullMeshComponent()->Bounds.Origin;
    return C.GetHitCenter();
}
float Blend(float X, float Low, float High)
{
    const float T = FMath::Clamp((X-Low)/(High-Low),0.f,1.f);
    return T*T*T*(T*(T*6-15)+10);
}
}
bool UGuLiXuntianGunnery::SurfaceAt(FVector XY, FVector& Point) const
{
    auto Query = OwnShipQuery(*Ship());
    const double Top = FMath::Max(XY.Z, Ship()->GetActorLocation().Z) + 5000;
    const FVector A(XY.X,XY.Y,Top), B(XY.X,XY.Y,Top-300000);
    FHitResult Hit;
    for (int32 Attempt=0; Attempt<8; ++Attempt)
    {
        if (!GetWorld()->LineTraceSingleByChannel(Hit,A,B,ECC_Visibility,Query)) return false;
        if (SurfaceReceiver(Hit)) { Point = Hit.ImpactPoint; return true; }
        if (!Hit.GetActor()) return false;
        Query.AddIgnoredActor(Hit.GetActor());
    }
    return false;
}
void UGuLiXuntianGunnery::Orbit(FVector2D Delta)
{
    if (!bViewStarted) { ViewYaw = InitialYaw(*Ship()); bViewStarted = true; }
    constexpr double DegreesPerCount=.0024*180./UE_DOUBLE_PI;
    ViewYaw = FRotator::NormalizeAxis(ViewYaw + Delta.X*DegreesPerCount);
    ViewPitch = FMath::Clamp(float(ViewPitch + Delta.Y*DegreesPerCount),-90.f,90.f);
}
void UGuLiXuntianGunnery::Zoom(float Notches) { Boom = FMath::Clamp(Boom*FMath::Exp(-Notches*.115),6700.,16667.); }
float UGuLiXuntianGunnery::InstrumentBlend() const { return Blend(-ViewPitch,35,72); }
void UGuLiXuntianGunnery::GetCameraView(FMinimalViewInfo& View) const
{
    const float Yaw = bViewStarted ? ViewYaw : InitialYaw(*Ship());
    const double P = FMath::DegreesToRadians(double(ViewPitch)), C = FMath::Cos(P), S = FMath::Sin(P);
    const double Shoulder = Boom * .2446;
    double Distance = Boom*C + Shoulder*S, Height = Shoulder*C - Boom*S;
    if (P<0)
    {
        const double Stern = Boom+Shoulder*S;
        Distance = Stern*1900*C/FMath::Max(1.,FMath::Sqrt(FMath::Square(1900*C)+FMath::Square(Stern*S)));
        Height = FMath::Lerp(Height,-1200.,double(Blend(-ViewPitch,0,19)));
    }
    const FVector Center = Ship()->GetActorLocation(), Heading = FRotator(0,Yaw,0).Vector();
    View.Rotation = FRotator(ViewPitch,Yaw,0);
    View.Location = Center - Distance*Heading + FVector::UpVector*Height;
    // Keep the eye above local terrain as the orbit moves underneath the stern.
    FVector Surface;
    if (SurfaceAt(View.Location,Surface)) View.Location.Z = FMath::Max(View.Location.Z,Surface.Z+400);
    // Also prevent the desired eye from settling inside a neighbouring hull or a ridge.
    const FVector Pivot = Center + FVector::UpVector * FMath::Clamp(Height,-1200.,Shoulder);
    FHitResult Hit;
    if (GetWorld()->SweepSingleByChannel(Hit,Pivot,View.Location,FQuat::Identity,ECC_Camera,
        FCollisionShape::MakeSphere(70.f),OwnShipQuery(*Ship())) && !Hit.bStartPenetrating)
        View.Location = Hit.Location + Hit.Normal*2;
    const double Vfov = FMath::Lerp(54.,FMath::Clamp(54.*Boom/9100,34.,74.),double(InstrumentBlend()));
    View.AspectRatio = 16.f/9;
    View.bConstrainAspectRatio = false;
    View.AspectRatioAxisConstraint = AspectRatio_MaintainYFOV;
    View.FOV = FMath::RadiansToDegrees(2*FMath::Atan(FMath::Tan(FMath::DegreesToRadians(Vfov*.5))*View.AspectRatio));
}

void UGuLiXuntianGunnery::RefreshContacts()
{
    ContactScratch.Reset();
    if (auto* Ledger=GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>())
    {
        const FVector Center = Ship()->GetActorLocation(), Span(Reach+20000);
        Ledger->GetNearbyTargetSnapshots(FBox(Center-Span,Center+Span),ContactScratch);
    }
    const auto Self = Ship()->GetCombatHealthComponent()->GetTargetHandle();
    ContactScratch.RemoveAll([&](const auto& C){return !C.bAlive || C.Handle == Self || C.CollisionActor.Get() == Ship();});
    const FVector Origin=Ship()->GetActorLocation();
    ContactScratch.Sort([&](const auto& A,const auto& B)
    {
        if (IsFlying(A)!=IsFlying(B)) return IsFlying(A);
        return FVector::DistSquared(A.GetHitCenter(),Origin)<FVector::DistSquared(B.GetHitCenter(),Origin);
    });
    bool Changed = ContactScratch.Num() != Contacts.Num();
    for (int32 I = 0; !Changed && I < Contacts.Num(); ++I)
    {
        const auto& A = Contacts[I];
        const auto& B = ContactScratch[I];
        Changed = A.Handle != B.Handle || A.Team != B.Team || A.CollisionActor != B.CollisionActor ||
            A.GetHitCenter() != B.GetHitCenter() || A.CollisionRadius != B.CollisionRadius ||
            A.Health != B.Health || A.MaxHealth != B.MaxHealth || A.Shield != B.Shield || A.MaxShield != B.MaxShield;
    }
    Swap(Contacts, ContactScratch);
    if (Changed)
    {
        ++ContactsRevision;
        ContactActors.Reset();
        ContactSoldiers.Reset();
        for (const auto& Contact : Contacts)
        {
            if (Contact.Handle.Kind==EGuLiTargetKind::CommanderSoldier) ContactSoldiers.Add(Contact.Handle.LocalId);
            else if (const auto* Actor=Contact.CollisionActor.Get()) ContactActors.Add(Actor);
        }
    }
}
void UGuLiXuntianGunnery::UpdateLocalView()
{
    if (!bViewStarted) { ViewYaw = InitialYaw(*Ship()); bViewStarted = true; }
    const double Now = GetWorld()->GetTimeSeconds();
    if (Now>=NextContacts) { RefreshContacts(); NextContacts = Now+.1; }
    Feed.RemoveAll([&](const auto& K){return Now-K.Time>=7.;});
    FMinimalViewInfo View;
    GetCameraView(View);
    const FVector Ray = View.Rotation.Vector(), End = View.Location + Ray*Reach;
    FHitResult Hit;
    const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit,View.Location,End,ECC_Visibility,OwnShipQuery(*Ship()));
    double Nearest = bHit ? Hit.Distance : Reach;
    AimPoint = View.Location + Ray*Nearest;
    const AActor* HitOwner = bHit ? Hit.GetActor() : nullptr;
    const FGuLiCombatTargetSnapshot* Selected = nullptr;
    for (const auto& Contact : Contacts)
    {
        if (HitOwner && Contact.CollisionActor.Get()==HitOwner) { Selected=&Contact; continue; }
        const FVector Center = ContactCenter(Contact);
        const double Along = (Center-View.Location).Dot(Ray);
        if (const auto* Other=Cast<AGuLiStrikeShip>(Contact.CollisionActor.Get()))
        {
            float Time=0;
            if (Other->IntersectHull(View.Location,End,0,Time) && Time*Reach<Nearest)
            { Nearest=Time*Reach; Selected=&Contact; AimPoint=View.Location+Ray*Nearest; HitOwner=nullptr; }
            continue;
        }
        const double R = FMath::Max(30.f,Contact.CollisionRadius);
        if (Along+R<=0 || Along-R>=Nearest) continue;
        const double OffSq = FVector::DistSquared(Center,View.Location+Along*Ray);
        if (OffSq>R*R) continue;
        double D = FMath::Max(0., Along-FMath::Sqrt(R*R-OffSq));
        if (D<Nearest) { Nearest=D; Selected=&Contact; AimPoint=View.Location+Ray*D; HitOwner=nullptr; }
    }
    Focus = Selected ? Selected->Handle : FGuLiTargetHandle();
    bAimingAtSurface = !Selected && bHit && SurfaceReceiver(Hit);
    if (Selected && IsFlying(*Selected)) bGround = ContactCenter(*Selected).Z < Ship()->GetHullMeshComponent()->Bounds.Origin.Z;
    else if (const auto* Other=Cast<AGuLiStrikeShip>(HitOwner))
        bGround = Other->GetHullMeshComponent()->Bounds.Origin.Z < Ship()->GetHullMeshComponent()->Bounds.Origin.Z;
    else if (Selected || bHit) bGround = AimPoint.Z < Ship()->GetHullMeshComponent()->Bounds.Origin.Z;
    else bGround = ViewPitch<0;
    bHasAim = true;
    FVector Under;
    if (SurfaceAt(Ship()->GetActorLocation(),Under)) HeightAGL = FMath::Max(0.,(Ship()->GetActorLocation().Z-Under.Z)/100);
    if (!bGround || !bAimingAtSurface || CoverageAt(AimPoint)) { bGroundHint=false; NextHint=0; }
    else if (Now>=NextHint) { UpdateGroundHint(); NextHint=Now+.18; }
}

void UGuLiXuntianGunnery::UpdateGroundHint()
{
    bGroundHint = false;
    const FVector Center = Ship()->GetActorLocation();
    double Best = TNumericLimits<double>::Max();
    // Check real surfaces along rays from each mount. A valid candidate is mandatory
    // even on cliffs or where both the inner and outer depression limits are in play.
    for (const auto& G : Mounts)
    {
        if (!G.bGround || !G.Part.IsValid()) continue;
        const FRotator Heading = Ship()->GetActorQuat().UnrotateVector(AimPoint-G.WorldHinge).Rotation();
        for (const float Fraction : {.02f,.5f,.98f})
        {
            const FRotator R(-FMath::Lerp(GroundMinDepression,GroundMaxDepression,Fraction),Heading.Yaw,0);
            const FVector Dir = Ship()->GetActorQuat().RotateVector(R.Vector());
            FHitResult Hit;
            if (!GetWorld()->LineTraceSingleByChannel(Hit,G.WorldHinge,G.WorldHinge+Dir*Reach,ECC_Visibility,OwnShipQuery(*Ship())) ||
                !SurfaceReceiver(Hit) || !CoverageAt(Hit.ImpactPoint)) continue;
            const double D = FVector::DistSquared(Hit.ImpactPoint,AimPoint);
            if (D<Best) { Best=D; GroundHint=Hit.ImpactPoint; bGroundHint=true; }
        }
    }
}
bool UGuLiXuntianGunnery::SetOwnedCardSnapshot(int64 Revision, const TMap<FName,int32>& OwnedLevels)
{
    if (Revision<=CardRevision || !Ship()->IsXuntian()) return false;
    struct FEntry { const TCHAR* Id; int32 Slot, Max; const TCHAR* Symbol; };
    static const FEntry Entries[] = {
        {TEXT("draft-57286bd0-778d-5e1a-8b9a-4b6f8c88c3d8"),0,2,TEXT("Air")},
        {TEXT("draft-b9135de9-b708-5a10-b4a9-3d1e100bfed7"),0,1,TEXT("Hangar")},
        {TEXT("draft-1f47cea7-d171-544b-af2a-ac519572c168"),1,3,TEXT("Belly")},
        {TEXT("draft-f56d587c-30f6-57a1-a836-1efeee8efb1d"),1,2,TEXT("Missile")},
        {TEXT("draft-7330c15e-f8df-5331-bcb4-838c7593ab8f"),1,3,TEXT("Bomb")},
        {TEXT("draft-c95c764c-501e-5a57-a56d-2773b21bf4d5"),2,1,TEXT("Shield")},
        {TEXT("draft-7f446203-4cd0-50dc-a3b5-abdc97be1589"),3,1,TEXT("Jammer")}
    };
    Cards.Reset(4); Cards.SetNum(4);
    for (const auto& E : Entries)
        if (const int32 L=OwnedLevels.FindRef(FName(E.Id)); L>0)
            Cards[E.Slot]={FName(E.Symbol),FMath::Min(L,E.Max)};
    if (const int32 Level=GetBellyLevel(); Level>0 && Cards[1].Card!=TEXT("Missile") && Cards[1].Card!=TEXT("Bomb"))
        Cards[1]={TEXT("Belly"),Level};
    CardRevision=Revision;
    return true;
}
void UGuLiXuntianGunnery::OnDamage(const FGuLiDamageFeedback& F)
{
    if (!F.bKilled || !F.Event.IsValid() || !Ship()->IsXuntian() || !Ship()->IsLocallyControlled() ||
        Feed.ContainsByPredicate([&](const auto& K){return K.Event==F.Event;})) return;
    auto Name = [&](const FGuLiTargetHandle& Target, EGuLiTeam& Team)
    {
        FGuLiCombatTargetSnapshot C;
        if (auto* L=GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>(); L && L->TryGetSourceSnapshot(Target,C)) Team=C.Team;
        if (const auto* P=Cast<APawn>(C.CollisionActor.Get()))
            if (const auto* PS=P->GetPlayerState(); PS && !PS->GetPlayerName().IsEmpty()) return PS->GetPlayerName();
        switch(Target.Kind)
        {
        case EGuLiTargetKind::Ship: return FString(TEXT("巡天"));
        case EGuLiTargetKind::Wingman: return FString(TEXT("僚机"));
        case EGuLiTargetKind::GroundActor: return FString(TEXT("游骑兵"));
        default: return FString::Printf(TEXT("#%u"),Target.LocalId);
        }
    };
    FGuLiXuntianKill K; K.Event=F.Event; K.Time=GetWorld()->GetTimeSeconds();
    K.Source=Name(F.Source,K.SourceTeam); K.Target=Name(F.Target,K.TargetTeam);
    if (Feed.Num()==3) Feed.RemoveAt(0);
    Feed.Add(MoveTemp(K));
}
