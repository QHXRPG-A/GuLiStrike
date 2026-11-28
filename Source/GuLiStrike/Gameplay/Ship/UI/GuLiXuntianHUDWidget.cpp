#include "Gameplay/Ship/UI/GuLiXuntianHUDWidget.h"
#include "UI/GuLiSegmentedHealthBar.h"
#include "Commander/UI/GuLiSceneUIWidget.h"
#include "Commander/UI/GuLiHealthBarLayout.h"
#include "Commander/UI/GuLiSceneUITypes.h"
#include "Commander/UI/GuLiSceneUISubsystem.h"
#include "Gameplay/Ship/UI/GuLiXuntianAtlas.h"
#include "Gameplay/Ship/Aiming/GuLiXuntianGunnery.h"
#include "Gameplay/CombatEffects/GuLiGroundWarningSubsystem.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Font.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "SceneView.h"
#include "Styling/CoreStyle.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
namespace Art=GuLiXuntianAtlas;
using V2=FVector2D;
const FLinearColor Ivory(FColor::FromHex(TEXT("FEE4D9")));
const FLinearColor Aqua(FColor::FromHex(TEXT("6AA4BE")));
const FLinearColor Amber(FColor::FromHex(TEXT("EE9D58")));
const FLinearColor Red(FColor::FromHex(TEXT("A34053")));
const FLinearColor Pink(FColor::FromHex(TEXT("EABCBA")));
const FLinearColor Muted(FColor::FromHex(TEXT("D5C09C")));
const FLinearColor Dark(FColor::FromHex(TEXT("2C3735")));
const FLinearColor Ink(FColor::FromHex(TEXT("021716")));
const FLinearColor Glass(FColor::FromHex(TEXT("142A2C")));

FLinearColor Alpha(FLinearColor Color,float Value) { Color.A=Value; return Color; }
enum class EAlign { Left, Center, Right };

struct FPainter
{
    const FGeometry& Geo;
    FSlateWindowElementList& Draw;
    const FSlateBrush& Atlas;
    const UFont* PanelFont;
    const UFont* NameFont;
    const FSlateFontMeasure& Measure;
    V2 Size;
    float Scale;
    int32 Layer;
    GuLiHealthGrid::FMeshBatch& Bars;
    GuLiHealthGrid::FCanvas BarCanvas;
#if WITH_EDITOR
    FGuLiXuntianHUDPaint* Inspection = nullptr;
#endif

    FPaintGeometry Box(V2 P,V2 Extent) const
    {
        return Geo.ToPaintGeometry(FVector2f(Extent),FSlateLayoutTransform(Scale,FVector2f(P*Scale)));
    }
    void Rect(V2 P,V2 Extent,FLinearColor C) const
    {
        FSlateDrawElement::MakeBox(Draw,Layer,Box(P,Extent),FCoreStyle::Get().GetBrush("WhiteBrush"),ESlateDrawEffect::None,C);
    }
    void Sprite(const Art::FTile& T,V2 Center,float Opacity=1) const
    {
        if (Opacity<=0) return;
        const V2 Extent(T.W,T.H);
        FSlateBrush Brush=Atlas;
        Brush.SetUVRegion(FBox2f(FVector2f(T.X,T.Y)/Art::Size,FVector2f(T.X+T.W,T.Y+T.H)/Art::Size));
        FSlateDrawElement::MakeBox(Draw,Layer,Box(Center-Extent*.5,Extent),&Brush,ESlateDrawEffect::None,Alpha(FLinearColor::White,Opacity));
    }
    void Path(std::initializer_list<V2> Points,FLinearColor C,float Width=1) const
    {
        TArray<FVector2f> P; P.Reserve(int32(Points.size()));
        for (const V2& V:Points) P.Add(FVector2f(V*Scale));
        FSlateDrawElement::MakeLines(Draw,Layer,Geo.ToPaintGeometry(),P,ESlateDrawEffect::None,C,true,Width*Scale);
    }
    void Line(V2 A,V2 B,FLinearColor C,float Width=1) const { Path({A,B},C,Width); }
    void Arc(V2 P,double R,double From,double To,FLinearColor C,float Width) const
    {
        if (To<=From || C.A<=0) return;
        TArray<FVector2f> Points;
        const int32 Steps=FMath::Max(2,FMath::CeilToInt((To-From)/2.));
        Points.Reserve(Steps+1);
        for (int32 I=0;I<=Steps;++I)
        {
            const double A=FMath::DegreesToRadians(FMath::Lerp(From,To,double(I)/Steps));
            Points.Add(FVector2f((P+R*V2(FMath::Cos(A),FMath::Sin(A)))*Scale));
        }
        FSlateDrawElement::MakeLines(Draw,Layer,Geo.ToPaintGeometry(),Points,ESlateDrawEffect::None,C,true,Width*Scale);
    }
    void Label(const FString& Text,V2 P,float Px,FLinearColor C,EAlign Align=EAlign::Left,float Spacing=0) const
    {
        if (Text.IsEmpty() || C.A<=0) return;
        bool CJK=false;
        for (const TCHAR Ch:Text) if (Ch>=0x2e80) { CJK=true; break; }
        FSlateFontInfo Font(CJK?NameFont:PanelFont,Px*.75f,TEXT("Medium"));
        Font.LetterSpacing=FMath::RoundToInt(1000*Spacing/Px);
        const V2 Extent=Measure.Measure(Text,Font);
        if (Align==EAlign::Right) P.X-=Extent.X;
        if (Align==EAlign::Center) P.X-=Extent.X*.5;
        P.Y-=Extent.Y*.5;
        const ESlateDrawEffect Effect=ESlateDrawEffect::None;
        FSlateDrawElement::MakeText(Draw,Layer,Box(P+V2(0,1),Extent+V2(5)),Text,Font,Effect,Alpha(Ink,C.A*.78f));
        FSlateDrawElement::MakeText(Draw,Layer,Box(P,Extent+V2(5)),Text,Font,Effect,C);
    }
    void Corners(V2 P,V2 Extent,double Length,FLinearColor C,float W) const
    {
        for (int32 X:{-1,1}) for (int32 Y:{-1,1})
        {
            const V2 K=P+V2(X*Extent.X,Y*Extent.Y);
            Path({K-V2(X*Length,0),K,K-V2(0,Y*Length)},C,W);
        }
    }
    void HeightCue(V2 P,double Height,FLinearColor C,double R=4) const
    {
        if (FMath::Abs(Height)<=500) { Line(P-V2(R,0),P+V2(R,0),C,1.6f); return; }
        const double Y=FMath::Sign(Height)*R*.48;
        Path({P+V2(-R,Y),P-V2(0,Y),P+V2(R,Y)},C,1.7f);
    }
    void GunTicks(V2 P,int32 Total,int32 Available,float Opacity=1) const
    {
        for (int32 I=0;I<Total;++I)
        {
            const V2 Start=P+V2((I-(Total-1)*.5)*8-3,0);
            Rect(Start-V2(1,2),V2(8,5),Alpha(Ink,.84f*Opacity));
            if (I<Available) Rect(Start-V2(0,1),V2(6,3),Alpha(Amber,Opacity));
            else Line(Start+V2(0,1),Start+V2(6,1),Alpha(Muted,Opacity),1.1f);
        }
    }
    void Gradient(V2 P,V2 Extent,bool Down,TArray<FSlateGradientStop> Stops) const
    {
        FSlateDrawElement::MakeGradient(Draw,Layer,Box(P,Extent),MoveTemp(Stops),Down?Orient_Horizontal:Orient_Vertical);
    }
};

struct FProjection
{
    FMatrix Matrix;
    V2 Center;
    bool Project(const FVector& Position,V2& Screen,bool Pin=false) const
    {
        const FVector4 Clip=Matrix.TransformFVector4(FVector4(Position,1));
        V2 Offset(Clip.X*Center.X,-Clip.Y*Center.Y);
        if (Clip.W>0) Offset/=Clip.W;
        const V2 Limit=Center-V2(12);
        const bool Onscreen=Clip.W>0 && FMath::Abs(Offset.X)<=Limit.X && FMath::Abs(Offset.Y)<=Limit.Y;
        if (!Onscreen && Pin)
        {
            if (Offset.IsNearlyZero()) Offset=V2(0,1);
            Offset/=FMath::Max(FMath::Abs(Offset.X)/Limit.X,FMath::Abs(Offset.Y)/Limit.Y);
        }
        Screen=Center+Offset;
        return Onscreen;
    }
};

void ShadeEdges(const FPainter& H,float Ground)
{
    const float Height=H.Size.Y*.23f, Width=H.Size.X*.2f;
    const FLinearColor Edge(FColor::FromHex(TEXT("07171C")));
    const FLinearColor Mid(FColor::FromHex(TEXT("0B1C1E")));
    H.Gradient(V2::ZeroVector,V2(H.Size.X,Height),true,{
        {FVector2f(0,0),Alpha(Edge,.76f)},{FVector2f(0,Height*.58f),Alpha(Mid,.28f)},{FVector2f(0,Height),Alpha(Mid,0)}});
    H.Gradient(V2(0,H.Size.Y-Height),V2(H.Size.X,Height),true,{
        {FVector2f(0,0),Alpha(Edge,0)},{FVector2f(0,Height),Alpha(Edge,.77f)}});
    if (Ground<=0) return;
    H.Gradient(V2::ZeroVector,V2(Width,H.Size.Y),false,{{FVector2f(0,0),Alpha(Ink,.43f*Ground)},{FVector2f(Width,0),Alpha(Ink,0)}});
    H.Gradient(V2(H.Size.X-Width,0),V2(Width,H.Size.Y),false,{{FVector2f(0,0),Alpha(Ink,0)},{FVector2f(Width,0),Alpha(Ink,.43f*Ground)}});
}

void DrawCompass(const FPainter& H,float Heading)
{
    const double C=H.Size.X*.5;
    const TCHAR* const Cardinal[]={TEXT("N"),TEXT("E"),TEXT("S"),TEXT("W")};
    for (int32 I=FMath::FloorToInt((Heading-41)/5)*5;I<=Heading+41;I+=5)
    {
        const double D=(I-Heading)*6.2;
        if (FMath::Abs(D)>255) continue;
        const int32 Value=(I+720)%360;
        const bool Major=Value%15==0;
        const float Fade=FMath::Clamp(1.-FMath::Abs(D)/287.,.14,.92);
        H.Line(V2(C+D,105),V2(C+D,Major?116:111),Alpha(Ivory,Fade),Major?1.8f:1.f);
        if (Value%30==0 && FMath::Abs(D)>33)
        {
            const bool IsCardinal=Value%90==0;
            H.Label(IsCardinal?FString(Cardinal[Value/90]):FString::Printf(TEXT("%03d"),Value),V2(C+D,94),15,IsCardinal?Aqua:Muted,EAlign::Center,.6);
        }
    }
    H.Label(FString::Printf(TEXT("%03d"),FMath::RoundToInt(Heading)%360),V2(C,96),29,Ivory,EAlign::Center,1);
    H.Line(V2(C-24,115),V2(C+24,115),Amber,1.5);
    H.Sprite(Art::CompassPointer,V2(C,124));
}

bool Flying(const FGuLiCombatTargetSnapshot& C) { return C.Handle.Kind==EGuLiTargetKind::Ship || C.Handle.Kind==EGuLiTargetKind::Wingman; }
FVector ContactPoint(const FGuLiCombatTargetSnapshot& C)
{
    if (const auto* Ship=Cast<AGuLiStrikeShip>(C.CollisionActor.Get())) return Ship->GetHullMeshComponent()->Bounds.Origin;
    return C.GetHitCenter();
}

void DrawRadar(const FPainter& H,const UGuLiXuntianGunnery& G,const FVector& Hull,EGuLiTeam Team,float Opacity)
{
    if (Opacity<=0) return;
    const V2 Top(H.Size.X-254,46), Center=Top+V2(101,165*.57);
    H.Sprite(Art::Radar,Top+V2(101,82.5),Opacity);
    const double Yaw=FMath::DegreesToRadians(G.ViewYaw);
    const V2 Right(-FMath::Sin(Yaw),FMath::Cos(Yaw)), Forward(FMath::Cos(Yaw),FMath::Sin(Yaw));
    for (const auto& C:G.GetContacts())
    {
        if (!Flying(C)) continue;
        const FVector D=ContactPoint(C)-Hull;
        const V2 R(D.X,D.Y);
        const V2 P=Center+V2(R.Dot(Right),-R.Dot(Forward))*.00168;
        if (P.X<Top.X+6 || P.X>Top.X+196 || P.Y<Top.Y+6 || P.Y>Top.Y+159) continue;
        const bool Friend=C.Team==Team;
        H.Sprite(Friend?Art::RadarFriend:Art::RadarFoe,P,Opacity);
        if (FMath::Abs(D.Z)>500)
            H.HeightCue(P-V2(0,FMath::Sign(D.Z)*9),D.Z,Alpha(Friend?Aqua:Red,Opacity));
    }
    H.Label(TEXT("600 m"),Top+V2(193,155),13,Alpha(Muted,Opacity),EAlign::Right,.4);
}

struct FPresentationTarget
{
    const FGuLiCombatTargetSnapshot* Contact=nullptr;
    V2 P=V2::ZeroVector;
    double Radius=0,Range=0,DeltaZ=0;
};

void DrawContacts(const FPainter& H,const UGuLiXuntianGunnery& G,const FGuLiPilotHealthFrame& Frame,
                  EGuLiTeam Team,FGuLiTargetHandle& LastFocus)
{
    FPresentationTarget Best;
    double BestScore=TNumericLimits<double>::Max();
    for (const auto& Item : Frame.Contacts)
    {
        const auto& C=G.GetContacts()[Item.Index];
        const bool Exact=C.Handle==G.Focus;
        const bool Friend=C.Team==Team;
        if (Item.bBadge) H.Sprite(Friend?Art::Friend:Art::Hostile,Item.Flag);
#if WITH_EDITOR
        if (H.Inspection && C.Handle.Kind == EGuLiTargetKind::CommanderSoldier)
            H.Inspection->Soldiers.Add({C.Handle.LocalId,Item.Life.Current,Item.Shield.Maximum>0 ? Item.Shield.Current/Item.Shield.Maximum : 0.f,
                Item.Shield.Maximum>0,Item.bChanged, Item.Bar.bIsValid ? Item.Bar.Min : Item.Center,
                Item.Bar.bIsValid ? Item.Bar.GetSize() : V2::ZeroVector,Item.Depth,
                FVector::DistXY(G.GetOwner()->GetActorLocation(),C.Location)});
#endif
        if (!Item.bBadge || Friend) continue;
        const bool Previous=C.Handle==LastFocus;
        if (!Exact && (G.Focus.IsValid() || Item.Miss>(Previous?98:82))) continue;
        const double Score=Exact?-1.:Item.Miss-(Previous?10:0);
        if (Score<BestScore) { BestScore=Score; Best={&C,Item.Center,Item.Radius,Item.Range,Item.DeltaZ}; }
    }
    Frame.Bars.Submit(H.Draw,H.Layer);
    LastFocus=Best.Contact?Best.Contact->Handle:FGuLiTargetHandle();
    if (!Best.Contact) return;
    const V2 Extent(Best.Radius,Best.Radius*.62);
    const double Corner=FMath::Min(7.,Best.Radius*.4);
    H.Corners(Best.P,Extent,Corner,Pink,2.6);
    H.Corners(Best.P,Extent,Corner,Red,1.4);
    const bool Airborne=Flying(*Best.Contact);
    const int32 Side=Best.P.X+Best.Radius+97<H.Size.X-12?1:-1;
    V2 Text=Best.P+V2(Side*(Best.Radius+11),0);
    if (Airborne) { H.HeightCue(Text,Best.DeltaZ,Ivory,4.5); Text.X+=Side*11; }
    H.Label(FString::Printf(TEXT("%d m"),FMath::RoundToInt(Best.Range/100)),Text,16,Ivory,Side>0?EAlign::Left:EAlign::Right,.3);
}

void DrawAltitudeTape(const FPainter& H,double Alt,double CenterY,float Ground)
{
    for (int32 N=FMath::FloorToInt((Alt-36)/5)*5;N<=Alt+36;N+=5)
    {
        if (N<0) continue;
        const double Y=CenterY+(Alt-N)*3.5;
        const bool Major=N%10==0;
        H.Line(V2(101,Y),V2(Major?119:110,Y),Alpha(Ivory,.8f*Ground),Major?1.7f:1.f);
        if (N%20==0 && FMath::Abs(Y-CenterY)>21) H.Label(FString::FromInt(N),V2(90,Y),15,Alpha(Muted,Ground),EAlign::Right);
    }
    H.Rect(V2(29,CenterY-12),V2(69,24),Alpha(Glass,.78f*Ground));
    H.Path({V2(98,CenterY-4),V2(102,CenterY),V2(98,CenterY+4)},Alpha(Amber,Ground),2);
}

void DrawGauges(const FPainter& H,const UGuLiXuntianGunnery& G,const FGuLiCombatHealthState& Vitals,double Speed,
                int32 Active,int32 Eligible,int32 BellyCount,float Blend)
{
    const V2 C=H.Size*.5;
    const float Air=1-Blend;
    const float Health=Vitals.MaxHealth>0?FMath::Clamp(Vitals.Health/Vitals.MaxHealth,0.f,1.f):0.f;
    const double Left=C.X-204,Right=C.X+204,PanelLeft=C.X-72,PanelY=H.Size.Y*.885;
    auto Mix=[&](double A,double B){return FMath::Lerp(A,B,double(Blend));};
    if (Air>0)
    {
        H.Sprite(Art::Dials,C,Air);
        H.Arc(C,170,-25,-24+49*FMath::Clamp(Speed/32.4,0.,1.),Alpha(Amber,.98f*Air),3);
        H.Sprite(Art::Altitude,V2(Mix(Left-88,63),C.Y+16),Air);
    }
    if (Blend>0)
    {
        DrawAltitudeTape(H,G.HeightAGL,C.Y,Blend);
    }
    const bool Ground=G.bGround;
    if (Ground)
    {
        const V2 Panel(PanelLeft,PanelY-19);
        H.Rect(Panel,V2(144,38),Alpha(Dark,.64f));
        H.Line(Panel,Panel+V2(144,0),Alpha(Ivory,.48f));
        H.Rect(Panel-V2(0,1),V2(23,2),Aqua);
    }
    const double Y=Ground ? PanelY : C.Y-15;
    const V2 BarOrigin=Ground ? V2(PanelLeft+100,Y-4) : V2(Left-88,Y+15);
#if WITH_EDITOR
    if (H.Inspection) { H.Inspection->bGround=Ground; H.Inspection->HullBarOrigin=BarOrigin; }
#endif
    H.Bars.Reset();
    H.Bars.Add(H.BarCanvas,BarOrigin,V2(Ground ? 40 : 88,8),{Vitals.Health,Vitals.MaxHealth},{Vitals.Shield,Vitals.MaxShield},1.f,1.);
    H.Bars.Submit(H.Draw,H.Layer);
    H.Sprite(Art::Hull,V2(Ground ? PanelLeft+20 : Left-88,Y));
    H.Label(FString::Printf(TEXT("%d%%"),FMath::RoundToInt(Health*100)),V2(Ground ? PanelLeft+90 : Left,Y),Ground ? 23 : 26,Ivory,EAlign::Right,.5);
    H.Label(FString::Printf(TEXT("%d m"),FMath::RoundToInt(G.HeightAGL)),V2(Mix(Left,90),Mix(C.Y+16,C.Y)),Mix(22,20),Ivory,EAlign::Right,.3);
    H.Sprite(Art::Speed,V2(Right,C.Y-15));
    H.Label(FString::Printf(TEXT("%.1f m/s"),Speed),V2(Right+17,C.Y-15),24,Ivory);
    const V2 Weapon(Right,C.Y+16);
    H.Sprite(G.bGround?(Eligible?Art::Belly:Art::BellyBlocked):Art::Air,Weapon);
    H.Label(FString::Printf(TEXT("×%d"),Active),Weapon+V2(17,0),21,Ivory);
    if (G.bGround) H.GunTicks(Weapon+V2(0,15),BellyCount,Eligible);
    if (G.IsTriggerHeld() && Eligible)
    {
        const int32 Sign=G.bGround?1:-1;
        H.Line(Weapon+V2(8,Sign*8),Weapon+V2(11,Sign*11),Ivory,1.8);
    }
}

const Art::FTile& CardIcon(FName Card)
{
    if (Card==TEXT("Hangar")) return Art::CardHangar;
    if (Card==TEXT("Missile")) return Art::CardMissile;
    if (Card==TEXT("Bomb")) return Art::CardBomb;
    if (Card==TEXT("Shield")) return Art::CardShield;
    if (Card==TEXT("Jammer")) return Art::CardJammer;
    return Card==TEXT("Belly")?Art::CardBelly:Art::CardAir;
}

void DrawCards(const FPainter& H,const TArray<FGuLiXuntianCardSlot>& Cards)
{
    const double Left=H.Size.X*.5-142;
    for (int32 I=0;I<Cards.Num();++I)
    {
        const auto& Card=Cards[I];
        const V2 P(Left+I*73,27);
        if (Card.Card.IsNone()) { H.Path({P+V2(0,30),P+V2(0,35),P+V2(65,35),P+V2(65,30)},Alpha(Muted,.34f)); continue; }
        H.Rect(P,V2(65,35),Alpha(Dark,.76f));
        H.Corners(P+V2(32.5,17.5),V2(32.5,17.5),6,Aqua,1.2);
        H.Sprite(CardIcon(Card.Card),P+V2(21,17.5));
        H.Label(FString::FromInt(Card.Level),P+V2(47,17.5),19,Muted,EAlign::Center);
    }
}

void DrawMatch(const FPainter& H,const UGuLiXuntianGunnery& G,EGuLiTeam Team,double Now)
{
    const double Right=H.Size.X-52,Top=H.Size.Y-93;
    H.Rect(V2(Right-236,Top),V2(236,52),Alpha(Dark,.55f));
    H.Label(TEXT("--:--"),V2(Right-163,Top+27),23,Ivory,EAlign::Right,.4);
    H.Line(V2(Right-151,Top+9),V2(Right-151,Top+43),Alpha(Muted,.45f));
    for (int32 I=0;I<2;++I)
    {
        const double Y=Top+17+18*I;
        H.Label(TEXT("–"),V2(Right-122,Y),18,Ivory,EAlign::Right);
        H.Rect(V2(Right-113,Y-3),V2(104,6),Alpha(I?Red:Aqua,.27f));
    }
    const auto& Rows=G.GetFeed();
    for (int32 I=0;I<Rows.Num();++I)
    {
        const auto& K=Rows[I];
        const V2 P(Right-86,FMath::Min(H.Size.Y*.77,Top-30)-(Rows.Num()-1-I)*25);
        const float Fade=FMath::Clamp(7.-(Now-K.Time),0.,1.);
        H.Label(K.Source.Left(12),P-V2(13,0),17,Alpha(K.SourceTeam==Team?Aqua:Pink,Fade),EAlign::Right);
        H.Line(P-V2(4,4),P+V2(4,4),Alpha(Ivory,Fade),1.7f);
        H.Line(P+V2(-4,4),P+V2(4,-4),Alpha(Ivory,Fade),1.7f);
        H.Label(K.Target.Left(12),P+V2(13,0),17,Alpha(K.TargetTeam==Team?Aqua:Pink,Fade));
    }
}

void DrawSights(const FPainter& H,const FProjection& View,const UGuLiXuntianGunnery& G,bool EnemyAir,int32 Eligible)
{
    for (const auto& Gun:G.GetMounts())
    {
        if (!G.bHasAim || Gun.bGround!=G.bGround || !Gun.bHasGeometry || !Gun.Part.IsValid()) continue;
        V2 P;
        const bool Visible=View.Project(Gun.Muzzle+Gun.Direction*Gun.BoreDistance,P,true);
        if (Visible && (P-View.Center).SizeSquared()<=400) continue;
        H.Sprite(Gun.bGround?Art::BellyPip:Art::AirPip,P,.5f);
        if (!Visible)
        {
            const V2 D=(P-View.Center).GetSafeNormal();
            const double R=Gun.bGround?5.:3.4;
            H.Line(P+D*R,P+D*(R+5),Alpha(Gun.bGround?Amber:Aqua,.5f),1.5f);
        }
    }
    if (G.bGround && G.bGroundHint && !Eligible)
    {
        V2 P; View.Project(G.GroundHint,P,true);
        const V2 D=(P-View.Center).GetSafeNormal(), Side(-D.Y,D.X);
        const V2 Tip=View.Center+D*48;
        H.Path({Tip-D*7+Side*5,Tip,Tip-D*7-Side*5},Ink,4.4f);
        H.Path({Tip-D*7+Side*5,Tip,Tip-D*7-Side*5},Amber,2.4f);
    }
    if (G.bGround) H.Sprite(G.bHasAim && !Eligible?Art::BellySightBlocked:Art::BellySight,View.Center);
    else H.Sprite(EnemyAir?Art::AirSightTracking:Art::AirSight,View.Center);
}
}

UGuLiXuntianHUDWidget::UGuLiXuntianHUDWidget(const FObjectInitializer& Init) : Super(Init)
{
    static ConstructorHelpers::FObjectFinder<UTexture2D> Atlas(TEXT("/Game/GuLiStrike/Ship/UI/Xuntian/T_XuntianInstruments"));
    static ConstructorHelpers::FObjectFinder<UFont> Digits(TEXT("/Game/GuLiStrike/Ship/UI/Xuntian/F_XuntianPanel"));
    static ConstructorHelpers::FObjectFinder<UFont> Names(TEXT("/Game/GuLiStrike/GroundMech/UI/Fonts/F_GroundHUD"));
    Instruments=Atlas.Object; PanelFont=Digits.Object; NameFont=Names.Object;
    InstrumentBrush.SetResourceObject(Instruments); InstrumentBrush.DrawAs=ESlateBrushDrawType::Image;
    SetVisibility(ESlateVisibility::HitTestInvisible); ForceVolatile(true);
}

int32 UGuLiXuntianHUDWidget::NativePaint(const FPaintArgs& Args,const FGeometry& Geo,const FSlateRect& Culling,
    FSlateWindowElementList& Draw,int32 Layer,const FWidgetStyle& Style,bool Enabled) const
{
    Layer=Super::NativePaint(Args,Geo,Culling,Draw,Layer,Style,Enabled);
    const auto* Ship=Cast<AGuLiStrikeShip>(GetOwningPlayerPawn());
    const auto* G=Ship?Ship->GetXuntianGunnery():nullptr;
    const auto* LP=GetOwningLocalPlayer();
    if (!G || !Ship->IsXuntian() || Ship->IsCarrierDormant() || Ship->GetCombatHealthComponent()->GetHealthState().bDead ||
        !LP || !LP->ViewportClient || !LP->ViewportClient->Viewport) return Layer;
    FSceneViewProjectionData Projection;
    if (!LP->GetProjectionData(LP->ViewportClient->Viewport,Projection)) return Layer;
    const V2 Size=Geo.GetLocalSize();
    if (Size.X<1 || Size.Y<1) return Layer;
    const float Scale=FMath::Min(Size.X/1920.,Size.Y/1080.);
    const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    FPainter H{Geo,Draw,InstrumentBrush,PanelFont,NameFont,*Measure,Size/Scale,Scale,Layer+1,VitalsMesh,{Geo,V2(Scale)}};
#if WITH_EDITOR
    if (bInspectNextPaint)
    {
        bInspectNextPaint=false;
        H.Inspection=&LastInspectedPaint;
        LastInspectedPaint.Frame=GFrameCounter;
        LastInspectedPaint.WarningStrokes=0;
        LastInspectedPaint.DesignSize=H.Size;
        LastInspectedPaint.Soldiers.Reset();
    }
#endif
    const FGuLiHealthBarView BarView(Projection,H.Size);
    const FProjection View{BarView.Matrix,H.Size*.5};
    const auto& Health=Ship->GetCombatHealthComponent()->GetHealthState();
    const auto Team=Ship->GetCombatHealthComponent()->GetCombatTeam();
    const FVector Hull=Ship->GetHullMeshComponent()->Bounds.Origin;
    int32 Active=0,Eligible=0,BellyCount=0;
    for (const auto& Gun : G->GetMounts())
    {
        if (!Gun.Part.IsValid()) continue;
        if (Gun.bGround) ++BellyCount;
        if (Gun.bGround==G->bGround) {++Active; if (Gun.bReachable) ++Eligible;}
    }
    const float Blend=G->InstrumentBlend();
    const bool HasEnemyAir=G->Focus.IsValid() && G->GetContacts().ContainsByPredicate([&](const auto& C)
        { return C.Handle==G->Focus && C.Team!=Team && Flying(C); });
    ShadeEdges(H,Blend); ++H.Layer;
    if (const auto* Warnings=Ship->GetWorld()->GetSubsystem<UGuLiGroundWarningSubsystem>())
    {
        // The translucent ground-coverage layer is above decals. Composite the same
        // ground footprint inside the pilot's HUD, below contacts and instruments.
        TArray<FVector2f> Stroke;
        Warnings->VisitPilotRims([&](TConstArrayView<FVector> Rim,const FLinearColor& Color)
        {
            FBox2D Bounds(ForceInit);
            auto Submit=[&]
            {
                if (Stroke.Num()>=2 && Bounds.Intersect(BarView.Viewport))
                {
                    FSlateDrawElement::MakeLines(Draw,H.Layer,Geo.ToPaintGeometry(),Stroke,ESlateDrawEffect::None,Color,true,1.8f*Scale);
#if WITH_EDITOR
                    if (H.Inspection) ++H.Inspection->WarningStrokes;
#endif
                }
                Stroke.Reset();
                Bounds.Init();
            };
            Stroke.Reserve(Rim.Num());
            for (const FVector& Vertex : Rim)
            {
                FGuLiHealthBarView::FPoint Point;
                if (!BarView.Project(Vertex,Point)) { Submit(); continue; }
                Bounds+=Point.Screen;
                Stroke.Add(FVector2f(Point.Screen*Scale));
            }
            Submit();
        });
    }
    DrawCards(H,G->GetCards());
    DrawCompass(H,FRotator::ClampAxis(G->ViewYaw));
    const auto* Scene=LP->GetSubsystem<UGuLiSceneUISubsystem>();
    if (const auto* Widget=Scene ? Scene->GetWidget() : nullptr)
    {
        H.BarCanvas.Clip = BarView.Viewport;
        DrawContacts(H,*G,Widget->GetPilotHealthFrame(*G,BarView,H.BarCanvas,Ship->GetActorLocation()),Team,LastLabelTarget);
        H.BarCanvas.Clip.Init();
    }
    DrawRadar(H,*G,Hull,Team,1-Blend);
    DrawGauges(H,*G,Health,Ship->GetVelocity().Size()/100,Active,Eligible,BellyCount,Blend);
    DrawMatch(H,*G,Team,Ship->GetWorld()->GetTimeSeconds());
    ++H.Layer;
    DrawSights(H,View,*G,HasEnemyAir,Eligible);
    return H.Layer;
}
