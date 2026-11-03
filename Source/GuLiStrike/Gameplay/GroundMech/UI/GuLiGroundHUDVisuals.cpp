#include "Gameplay/GroundMech/UI/GuLiGroundHUDVisuals.h"
#include "Gameplay/Data/Generated/GuLiStrikeGroundHUDTableRows.h"
#include "Components/BorderSlot.h"
#include "Components/SizeBox.h"
#include "Engine/Font.h"
#include "Gameplay/Presentation/GuLiLocalTeamColors.h"
#include "GeomTools.h"
#include "Algo/Reverse.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SInvalidationPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"

namespace GuLiGroundUI
{
void FitDesignToViewport(USizeBox* Design,FVector2D ViewportSize)
{
    if(!Design||ViewportSize.X<1||ViewportSize.Y<1)return;
    const double Scale=FMath::Min(ViewportSize.X/1920.,ViewportSize.Y/1080.);
    const FVector2D Extent=ViewportSize/Scale;
    if(!FMath::IsNearlyEqual(double(Design->GetWidthOverride()),Extent.X,.1))Design->SetWidthOverride(Extent.X);
    if(!FMath::IsNearlyEqual(double(Design->GetHeightOverride()),Extent.Y,.1))Design->SetHeightOverride(Extent.Y);
}
FLinearColor Color(const UDataTable* Table,FName Name,FLinearColor Fallback)
{
    const auto* R=Table?Table->FindRow<FGuLiStrikeGroundHUDElementsRow>(Name,TEXT("Ground HUD theme"),false):nullptr;
    return R&&!R->ColorSRGB.IsEmpty()?FLinearColor::FromSRGBColor(FColor::FromHex(R->ColorSRGB)):Fallback;
}
float Number(const UDataTable* Table,FName Name,float Fallback)
{const auto* R=Table?Table->FindRow<FGuLiStrikeGroundHUDElementsRow>(Name,TEXT("Ground HUD theme"),false):nullptr;return R?R->Value:Fallback;}
FSlateBrush Brush(const UDataTable* Table,FName Name)
{
    FSlateBrush B;B.DrawAs=ESlateBrushDrawType::Image;
    if(const auto* R=Table?Table->FindRow<FGuLiStrikeGroundHUDElementsRow>(Name,TEXT("Ground HUD art"),false):nullptr)
    {
        B.SetResourceObject(R->Texture.LoadSynchronous());
        if(R->Sprite>=0){const FVector2f Min((R->Sprite%4)/4.f,(R->Sprite/4)/2.f);B.SetUVRegion(FBox2f(Min,Min+FVector2f(.25f,.5f)));}
    }
    return B;
}
FSlateFontInfo Font(const UDataTable* Table,float Size,bool Bold)
{
    const auto* Row=Table?Table->FindRow<FGuLiStrikeGroundHUDElementsRow>(TEXT("UIFont"),TEXT("Ground HUD font"),false):nullptr;
    if(const auto* Face=Row?Cast<UFont>(Row->Texture.LoadSynchronous()):nullptr)return FSlateFontInfo(Face,Size,Bold?TEXT("Bold"):TEXT("Medium"));
    return FCoreStyle::GetDefaultFontStyle(Bold?"Bold":"Regular",Size);
}
FLinearColor Light(FLinearColor Base,float Gain)
{
    // Saturating each RGB component independently turned the palette's orange
    // into yellow. Keep its hue while raising luminance within the UI gamut.
    const float A=Base.A;const float Peak=FMath::Max3(Base.R,Base.G,Base.B);
    Base*=FMath::Min(Gain,1.f/FMath::Max(.001f,Peak));Base.A=A;return Base;
}
}

namespace
{
/** All geometry is UI-local; no scene lighting, exposure or post-process dependency. */
struct FInstrumentPaint
{
    const FGeometry& G;FSlateWindowElementList& Out;int32 Layer;
    FVector2D Scale=FVector2D(1);
    FVector2D Extension=FVector2D::ZeroVector,Offset=FVector2D::ZeroVector;
    FVector2D Map(FVector2D V) const
    {
        return (V+Offset+FVector2D(Extension.X*FMath::Clamp((V.X-640.)/640.,0.,1.),
            Extension.Y*FMath::Clamp((V.Y-120.)/840.,0.,1.)))*Scale;
    }
    void Line(const TArray<FVector2D>& Points,FLinearColor C,float Width=1)
    {
        TArray<FVector2D> P;P.Reserve(Points.Num());for(const auto& V:Points)P.Add(Map(V));
        FSlateDrawElement::MakeLines(Out,Layer,G.ToPaintGeometry(),P,ESlateDrawEffect::None,C,true,Width*FMath::Min(Scale.X,Scale.Y));
    }
    void Glow(const TArray<FVector2D>& P,FLinearColor C,float Width=2)
    {
        Line(P,C.CopyWithNewOpacity(C.A*.045f),Width+15);Line(P,C.CopyWithNewOpacity(C.A*.10f),Width+8);
        Line(P,C.CopyWithNewOpacity(C.A*.23f),Width+3);Line(P,GuLiGroundUI::Light(C),Width);
    }
    void Fill(const TArray<FVector2D>& Points,FLinearColor C,TOptional<FLinearColor> Bottom={})
    {
        if(Points.Num()<3)return;
        TArray<FVector2D> Polygon=Points;
        if(Polygon.Last().Equals(Polygon[0]))Polygon.Pop();
        double Area=0;for(int32 N=0;N<Polygon.Num();++N)Area+=Polygon[N]^Polygon[(N+1)%Polygon.Num()];
        if(Area<0)Algo::Reverse(Polygon);
        TArray<FVector2D> Triangles;
        if(!FGeomTools2D::TriangulatePoly(Triangles,Polygon))return;
        TArray<FSlateVertex> V;TArray<SlateIndex> I;
        const auto Transform=G.GetAccumulatedRenderTransform();
        for(const auto& P:Triangles)
        {
            const auto Tint=Bottom.IsSet()?FMath::Lerp(C,Bottom.GetValue(),float(FMath::Clamp(Map(P).Y/FMath::Max(1.,G.GetLocalSize().Y),0.,1.))):C;
            I.Add(static_cast<SlateIndex>(V.Num()));V.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform,FVector2f(Map(P)),FVector2f::ZeroVector,Tint.ToFColor(true)));
        }
        const auto Handle=FSlateApplication::Get().GetRenderer()->GetResourceHandle(*FCoreStyle::Get().GetBrush("WhiteBrush"));
        FSlateDrawElement::MakeCustomVerts(Out,Layer,Handle,V,I,nullptr,0,0);
    }
    void Box(FVector2D P,FVector2D S,FLinearColor C)
    {FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(Map(P+S)-Map(P),FSlateLayoutTransform(Map(P))),FCoreStyle::Get().GetBrush("WhiteBrush"),ESlateDrawEffect::None,C);}
    void Image(FVector2D P,FVector2D S,const FSlateBrush& B,FLinearColor C=FLinearColor::White)
    {if(B.GetResourceObject())FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(Map(P+S)-Map(P),FSlateLayoutTransform(Map(P))),&B,ESlateDrawEffect::None,C);}
};
TArray<FVector2D> Bevel(FVector2D S,float Inset,float Cut)
{
    const double X=S.X-Inset,Y=S.Y-Inset;
    return {{Inset+Cut,Inset},{X-Cut,Inset},{X,Inset+Cut},{X,Y-Cut},{X-Cut,Y},{Inset+Cut,Y},{Inset,Y-Cut},{Inset,Inset+Cut},{Inset+Cut,Inset}};
}
TArray<FVector2D> InstrumentOutline(FVector2D S,float Inset,float Cut,EGuLiGroundInstrumentShape Shape)
{
    // Reference-specific silhouettes, scaled inside the existing safe layout.
    // Small controls and the world-space card frame keep their own contour.
    TArray<FVector2D> Points;
    switch(Shape)
    {
    case EGuLiGroundInstrumentShape::Health:
        Points={{.055,0},{.235,0},{.265,.10},{.51,.10},{.54,.17},{.94,.17},{1,.32},{1,.84},{.94,1},{.055,1},{0,.85},{0,.17}};break;
    case EGuLiGroundInstrumentShape::Jet:
        Points={{.13,0},{.86,0},{1,.09},{1,.91},{.87,1},{.16,1},{0,.83},{0,.12}};break;
    case EGuLiGroundInstrumentShape::LocalMap:
        Points={{.07,.02},{.55,0},{.92,0},{1,.085},{1,.925},{.93,1},{.065,1},{0,.92},{0,.12}};break;
    case EGuLiGroundInstrumentShape::Rogue:
        Points={{.10,0},{.85,.015},{1,.075},{1,.94},{.90,1},{.10,1},{0,.93},{0,.07}};break;
    default:return Bevel(S,Inset,Cut);
    }
    for(auto& P:Points)P=FVector2D(Inset)+P*(S-FVector2D(Inset*2));
    const auto First=Points[0];Points.Add(First);return Points;
}

class SGuLiGroundGlyph final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SGuLiGroundGlyph){}SLATE_END_ARGS()
    void Construct(const FArguments&,FName In,FLinearColor InLine,FLinearColor InAccent,FLinearColor InText)
    {Name=In;Cyan=GuLiGroundUI::Light(InLine);Warm=GuLiGroundUI::Light(InAccent,1.3f);Text=InText;SetVisibility(EVisibility::HitTestInvisible);}
    virtual FVector2D ComputeDesiredSize(float) const override{return {100,100};}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& E,int32 L,const FWidgetStyle& Style,bool) const override
    {
        FInstrumentPaint P{G,E,L,G.GetLocalSize()/FVector2D(100)};
        const auto C=Cyan*Style.GetColorAndOpacityTint(),A=Warm*Style.GetColorAndOpacityTint(),T=Text*Style.GetColorAndOpacityTint();
        auto Outline=[&](TArray<FVector2D> Points,FLinearColor Fill,FLinearColor Edge,float Width=1.2f)
        {P.Fill(Points,Fill);const FVector2D First=Points[0];Points.Add(First);P.Line(Points,Edge,Width);};
        auto Arc=[&](FVector2D Center,float Radius,float Start,float End,FLinearColor Color,float Width)
        {TArray<FVector2D> Points;for(int32 I=0;I<=18;++I){const double R=FMath::DegreesToRadians(FMath::Lerp(Start,End,I/18.f));Points.Add(Center+FVector2D(FMath::Cos(R),FMath::Sin(R))*Radius);}P.Line(Points,Color,Width);};
        const FString Key=Name.ToString();
        if(Name==TEXT("WeaponLevel"))
        {
            Outline({{32,18},{44,30},{70,9},{80,12},{80,22},{54,47},{42,35}},C*.45f,C,3);
            P.Line({{23,63},{50,44},{77,63}},A,5);P.Line({{23,83},{50,64},{77,83}},T,4);
        }
        else if(Name==TEXT("FireRate"))
        {
            for(int32 I=0;I<3;++I)
            {
                const float Y=20+I*27;
                Outline({{44,Y},{73,Y},{89,Y+8},{73,Y+16},{44,Y+16}},I==1?A:C,I==1?T:C,2);
                P.Line({{10+I*7.,Y+8},{35,Y+8}},C,4);
            }
        }
        else if(Name==TEXT("Speed"))
        {
            for(int32 I=0;I<3;++I)
            {
                const double X=50+(I-1)*27,Y=I==1?9:29;
                P.Fill({{X,Y},{X-13,Y+23},{X-5,Y+21},{X-5,79},{X+5,79},{X+5,Y+21},{X+13,Y+23}},C.CopyWithNewOpacity(I==1?1.f:.78f));
                P.Line({{X,Y+8},{X,69}},T.CopyWithNewOpacity(.75f),1.3f);
                P.Line({{X,82},{X,96}},C.CopyWithNewOpacity(.38f),3);
            }
        }
        else if(Name==TEXT("MapSelf"))
        {
            P.Fill({{78,13},{19,49},{45,57},{52,83}},T);
            P.Line({{78,13},{19,49},{45,57},{52,83},{78,13}},C,2);
            P.Line({{45,57},{78,13}},C.CopyWithNewOpacity(.8f),1.5f);
        }
        else if(Name==TEXT("MapFriendly"))
        {
            const auto Team=GuLiLocalTeamColors::GetUI(EGuLiTeam::Red,EGuLiTeam::Red);
            Arc({50,50},24,0,360,Team.CopyWithNewOpacity(.12f),16);
            TArray<FVector2D> Disc;for(int32 I=0;I<32;++I){const double R=I*UE_PI/16;Disc.Add(FVector2D(50)+FVector2D(FMath::Cos(R),FMath::Sin(R))*23);}
            P.Fill(Disc,Team);Arc({50,50},24,0,360,T,2.5f);
        }
        else if(Name==TEXT("MapEnemy"))
        {
            const auto Team=GuLiLocalTeamColors::GetUI(EGuLiTeam::Blue,EGuLiTeam::Red);
            P.Fill({{50,19},{18,77},{82,77}},Team);
        }
        else if(Name==TEXT("MapPoint"))
        {
            P.Glow({{50,13},{87,50},{50,87},{13,50},{50,13}},A,4);
            P.Line({{50,20},{80,50},{50,80},{20,50},{50,20}},T,2);
        }
        else if(Name==TEXT("Upgrade"))
        {
            Outline({{50,8},{91,50},{50,92},{9,50}},C*.14f,C,2);
            P.Line({{31,53},{50,34},{69,53}},A,4);P.Line({{31,70},{50,51},{69,70}},C,3);
        }
        else // Front-facing mechanical health silhouette, rather than the old heart badge.
        {
            const auto Dark=C*.14f;
            Outline({{44,6},{56,6},{60,12},{56,22},{44,22},{40,12}},Dark,C);
            Outline({{34,24},{42,22},{58,22},{66,24},{67,43},{59,51},{41,51},{33,43}},Dark,C,1.7f);
            P.Line({{38,26},{43,36},{57,36},{62,26}},C,2);P.Line({{43,39},{57,39},{56,47},{44,47},{43,39}},C,1);
            Outline({{18,22},{30,20},{34,27},{31,43},{17,40},{13,30}},Dark,C);
            Outline({{70,20},{82,22},{87,30},{83,40},{69,43},{66,27}},Dark,C);
            for(bool Right:{false,true})
            {
                auto Mirror=[&](TArray<FVector2D> Points){if(Right)for(auto& V:Points)V.X=100-V.X;return Points;};
                Outline(Mirror({{17,43},{29,45},{26,62},{16,65},{11,59}}),Dark,C,1.5f);
                Outline(Mirror({{35,53},{47,53},{46,70},{34,72},{30,64}}),Dark,C,1.5f);
                Outline(Mirror({{33,74},{45,73},{44,90},{29,93},{26,89}}),Dark,C,1.5f);
                P.Line(Mirror({{19,48},{23,50},{20,60}}),C,1);P.Line(Mirror({{37,77},{38,87},{29,89}}),C,1);
            }
            P.Line({{41,54},{50,59},{59,54}},C,1.5f);P.Line({{45,13},{55,13}},T,2);
        }
        return L;
    }
private:FName Name;FLinearColor Cyan,Warm,Text;
};
}
TSharedRef<SWidget> GuLiGroundUI::Glyph(FName Name,FLinearColor Line,FLinearColor Accent,FLinearColor Text)
{return SNew(SInvalidationPanel)[SNew(SGuLiGroundGlyph,Name,Line,Accent,Text)];}

class SGuLiGroundPanel final : public SBorder
{
public:
    SLATE_BEGIN_ARGS(SGuLiGroundPanel){}SLATE_END_ARGS()
    void Construct(const FArguments&,UGuLiGroundHUDPanel* In){Owner=In;SBorder::Construct(SBorder::FArguments());}
    virtual int32 OnPaint(const FPaintArgs& Args,const FGeometry& G,const FSlateRect& Culling,FSlateWindowElementList& E,int32 L,const FWidgetStyle& Style,bool Enabled) const override
    {
        const auto* O=Owner.Get();if(!O)return L;
        const FVector2D S=G.GetLocalSize();if(S.X<2||S.Y<2)return L;
        const float Cut=FMath::Min(O->bArtworkFrame?70.f:O->bCompact?10.f:24.f,float(FMath::Min(S.X,S.Y)*.15));
        const FLinearColor Edge=(O->IsHighlighted()?O->AccentColor:O->EdgeColor)*Style.GetColorAndOpacityTint();
        FInstrumentPaint P{G,E,L};
        const auto Fill=O->GetBrushColor()*Style.GetColorAndOpacityTint();
        auto Contour=[&](float Inset,float Corner){return InstrumentOutline(S,Inset,Corner,O->Shape);};
        if(O->bKeycap)
        {
            P.Fill(Bevel(S,1,4),Fill);
            P.Line(Bevel(S,1,4),GuLiGroundUI::Light(Edge).CopyWithNewOpacity(.85f),1.1f);
            return SCompoundWidget::OnPaint(Args,G,Culling,E,P.Layer+1,Style,Enabled);
        }
        if(O->bArtworkFrame)
        {
            const auto A=S*O->ArtworkMin,B=S*O->ArtworkMax;
            P.Fill({{Cut,3},{S.X-Cut,3},{S.X-3,Cut},{S.X-3,A.Y},{3,A.Y},{3,Cut}},Fill);
            P.Fill({{3,B.Y},{S.X-3,B.Y},{S.X-3,S.Y-Cut},{S.X-Cut,S.Y-3},{Cut,S.Y-3},{3,S.Y-Cut}},Fill);
            P.Box({3,A.Y},{A.X-3,B.Y-A.Y},Fill);P.Box({B.X,A.Y},{S.X-B.X-3,B.Y-A.Y},Fill);
            P.Line({A,{B.X,A.Y},B,{A.X,B.Y},A},Edge.CopyWithNewOpacity(.9f),2);
        }
        else P.Fill(Contour(3,Cut),Fill,O->bCompact?Fill:FMath::Lerp(Fill,Edge.CopyWithNewOpacity(Fill.A),.045f));
        // Read-only HUDuiPRO grid inside the opaque instrument; the outer contour is sized to its live slot.
        P.Image(FVector2D(Cut,5),FVector2D(FMath::Max(1.,S.X-2*Cut),O->bArtworkFrame?S.Y*O->ArtworkMin.Y-10:FMath::Max(1.,S.Y-10)),O->GridBrush,O->DetailColor.CopyWithNewOpacity(.07f)*Style.GetColorAndOpacityTint());
        // A dark gasket, luminous outer lip and separate inner engraving keep the
        // instruments legible over both bright terrain and the blurred card view.
        ++P.Layer;
        const float Rim=O->bArtworkFrame?14.f:4.f;
        P.Line(Contour(Rim,Cut),Fill.CopyWithNewOpacity(1),O->bArtworkFrame?22.f:6.f);
        if(O->bArtworkFrame)
        {
            const auto Outline=Bevel(S,Rim,Cut);
            P.Line(Outline,Edge.CopyWithNewOpacity(Edge.A*.07f),74.f);
            P.Line(Outline,Edge.CopyWithNewOpacity(Edge.A*.13f),45.f);
            P.Glow(Outline,Edge,13.f);
            P.Line(Outline,FMath::Lerp(GuLiGroundUI::Light(Edge),O->DetailColor,.35f),3.f);
        }
        else P.Glow(Contour(Rim,Cut),Edge,O->IsHighlighted()?3.8f:1.8f);
        P.Line(Contour(O->bArtworkFrame?35.f:12.f,FMath::Max(3.f,Cut-8)),GuLiGroundUI::Light(Edge).CopyWithNewOpacity(.62f),O->bArtworkFrame?2.1f:1.f);
        if(O->bArtworkFrame)
        {
            const double X=44,Y=S.Y-44;
            P.Line({{X,Cut+32},{X,S.Y-Cut-32},{Cut+32,Y},{S.X-Cut-32,Y},{S.X-X,S.Y-Cut-32}},Edge.CopyWithNewOpacity(.55f),1.5f);
            P.Line({{70,S.Y*O->ArtworkMin.Y-13},{S.X-70,S.Y*O->ArtworkMin.Y-13}},GuLiGroundUI::Light(Edge),2.4f);
            P.Line({{70,S.Y*O->ArtworkMax.Y+15},{S.X-70,S.Y*O->ArtworkMax.Y+15}},GuLiGroundUI::Light(Edge),2.4f);
            for(const double X0:{S.X*.12,S.X*.73})P.Box({X0,34},{S.X*.15,2},Edge.CopyWithNewOpacity(.7f));
        }
        else if(!O->bCompact&&O->Shape==EGuLiGroundInstrumentShape::Standard)
        {
            P.Line({{20,Cut+20},{20,S.Y-Cut-12},{Cut+17,S.Y-18},{S.X-Cut-25,S.Y-18}},Edge.CopyWithNewOpacity(.35f),.8f);
            P.Fill({{Cut+7,7},{S.X*.61,7},{S.X*.61-19,19},{Cut+7,19}},Edge.CopyWithNewOpacity(.10f));
        }
        if(O->HeaderHeight>0)
        {
            const double TabEnd=FMath::Min(S.X-35.,S.X*.7);
            P.Fill({{Cut+5,8},{TabEnd-18,8},{TabEnd,O->HeaderHeight-8},{S.X-22,O->HeaderHeight-8},{S.X-15,O->HeaderHeight},{15,O->HeaderHeight},{15,Cut}},Fill.CopyWithNewOpacity(.75f));
            P.Line({{Cut+5,8},{TabEnd-18,8},{TabEnd,O->HeaderHeight-8},{S.X-22,O->HeaderHeight-8}},Edge.CopyWithNewOpacity(.6f),.8f);
            P.Line({{14,O->HeaderHeight},{S.X-14,O->HeaderHeight}},Edge.CopyWithNewOpacity(.7f));
            P.Box({17,O->HeaderHeight-4},{42,2},GuLiGroundUI::Light(Edge));
        }
        if(!O->bCompact)
        {
            if(O->Shape==EGuLiGroundInstrumentShape::Standard)
                P.Line({{Cut+8,0},{S.X*.45,0},{S.X*.45+7,5},{S.X-Cut-8,5}},Edge.CopyWithNewOpacity(.5f),.7f);
            P.Line({{0,48},{0,S.Y-48}},Edge.CopyWithNewOpacity(.65f),.8f);
            P.Box({S.X-7,S.Y*.35},{3,24},Edge);P.Box({Cut+10,S.Y-5},{24,3},Edge);
            if(O->Shape!=EGuLiGroundInstrumentShape::Standard)
            {
                // Secondary engraving stays within the housing, leaving its
                // center transparent/quiet for real data and terrain.
                P.Line(Contour(0,Cut+3),O->DetailColor.CopyWithNewOpacity(.45f),.75f);
                P.Line({{18,S.Y*.43},{18,S.Y*.62}},GuLiGroundUI::Light(Edge),2.5f);
                P.Line({{S.X-18,S.Y*.67},{S.X-18,S.Y*.82}},Edge.CopyWithNewOpacity(.6f),1.f);
                for(int32 J=0;J<3;++J)P.Box({S.X-9,S.Y*.49+J*7},{3,3},Edge.CopyWithNewOpacity(.65f));
            }
            if(O->Shape==EGuLiGroundInstrumentShape::Jet)
            {
                P.Line({{S.X-61,65},{S.X-25,65},{S.X-16,74},{S.X-16,S.Y-23},{S.X-28,S.Y-13},{S.X-61,S.Y-13}},Edge.CopyWithNewOpacity(.8f),1.2f);
                P.Line({{28,111},{28,148}},GuLiGroundUI::Light(Edge),3.f);
            }
            if(O->Shape==EGuLiGroundInstrumentShape::Health)
                P.Line({{140,102},{S.X-26,102},{S.X-20,108},{S.X-20,134},{140,134},{134,128},{134,108},{140,102}},GuLiGroundUI::Light(Edge).CopyWithNewOpacity(.8f),1.2f);
        }
        return SCompoundWidget::OnPaint(Args,G,Culling,E,P.Layer+1,Style,Enabled);
    }
private:TWeakObjectPtr<UGuLiGroundHUDPanel> Owner;
};
void UGuLiGroundHUDPanel::SetTheme(UDataTable* Table,float InHeaderHeight,bool InCompact)
{
    EdgeColor=GuLiGroundUI::Color(Table,TEXT("Line"));AccentColor=GuLiGroundUI::Color(Table,TEXT("Accent"));DetailColor=GuLiGroundUI::Color(Table,TEXT("Secondary"));
    HeaderHeight=InHeaderHeight;bCompact=InCompact;GridBrush=GuLiGroundUI::Brush(Table,TEXT("InstrumentGrid"));GridResource=GridBrush.GetResourceObject();
    SetBrushColor(GuLiGroundUI::Color(Table,TEXT("Panel")).CopyWithNewOpacity(.91f));SetPadding(FMargin(14));
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    if(MyBorder)MyBorder->Invalidate(EInvalidateWidgetReason::Paint);
}
void UGuLiGroundHUDPanel::SetHighlighted(bool In)
{if(bHighlighted!=In){bHighlighted=In;if(MyBorder)MyBorder->Invalidate(EInvalidateWidgetReason::Paint);}}
TSharedRef<SWidget> UGuLiGroundHUDPanel::RebuildWidget()
{
    MyBorder=SNew(SGuLiGroundPanel,this);
    if(GetChildrenCount()>0)CastChecked<UBorderSlot>(GetContentSlot())->BuildSlot(MyBorder.ToSharedRef());
    return MyBorder.ToSharedRef();
}

class SGuLiGroundCockpit final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SGuLiGroundCockpit){}SLATE_END_ARGS()
    void Construct(const FArguments&,UGuLiGroundHUDChrome* In,bool InCompass){Owner=In;bCompass=InCompass;SetVisibility(EVisibility::HitTestInvisible);}
    virtual FVector2D ComputeDesiredSize(float) const override{return FVector2D(1920,1080);}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& E,int32 L,const FWidgetStyle& Style,bool) const override
    {
        const auto* O=Owner.Get();if(!O)return L;
        FInstrumentPaint P{G,E,L};P.Extension=G.GetLocalSize()-FVector2D(1920,1080);
        const auto Line=O->LineColor*Style.GetColorAndOpacityTint();
        if(bCompass)
        {
            P.Offset=FVector2D(P.Extension.X*.5,0);P.Extension=FVector2D::ZeroVector;
            if(O->Mode!=0)return L;
            const float Yaw=FMath::Fmod(O->Heading+360.f,360.f);
            for(int32 I=-36;I<=36;++I)
            {
                const float D=FMath::RoundToFloat(Yaw/5)*5+I*5;
                const float X=960+FMath::FindDeltaAngleDegrees(Yaw,D)*3.85f;
                if(X<640||X>1280)continue;
                const bool Major=FMath::RoundToInt(D)%45==0;
                P.Line({{X,91},{X,Major?105.:98.}},O->SecondaryColor.CopyWithNewOpacity(.8f),Major?1.4f:.8f);
                if(Major)
                {
                    static const TCHAR* Names[]={TEXT("N"),TEXT("NE"),TEXT("E"),TEXT("SE"),TEXT("S"),TEXT("SW"),TEXT("W"),TEXT("NW")};
                    const int32 Index=((FMath::RoundToInt(D/45)%8)+8)%8;
                    FSlateDrawElement::MakeText(E,L+1,G.ToPaintGeometry(FVector2D(44,24),FSlateLayoutTransform(P.Map(FVector2D(X-13,108)))),Names[Index],FCoreStyle::GetDefaultFontStyle("Bold",Index%2?13:17),ESlateDrawEffect::None,O->TextColor);
                }
            }
            P.Fill({{960,81},{953,92},{967,92}},O->AccentColor);return L+1;
        }
        // Coordinate tracing of the three supplied 1672x941 references, in a 1920x1080 design space.
        P.Fill({{0,0},{1920,0},{1920,32},{1885,57},{1580,66},{1548,88},{1380,91},{1348,70},{1310,45},{1170,45},{1128,76},{790,76},{744,45},{620,45},{590,68},{390,60},{356,93},{150,93},{126,70},{0,48}},O->PanelColor.CopyWithNewOpacity(.95f));
        P.Fill({{0,1080},{1920,1080},{1920,1060},{1855,1068},{1609,1034},{1363,1012},{1333,1016},{1302,1035},{1152,1035},{1119,1020},{799,1020},{766,1035},{624,1035},{601,1012},{559,1013},{294,1036},{67,1068},{0,1060}},O->PanelColor.CopyWithNewOpacity(.95f));
        // Sample only the unlabelled wing glow of Cockpit_2; never display its baked cockpit text/numbers.
        FSlateBrush Wing=O->CockpitBrush;Wing.SetUVRegion(FBox2f(FVector2f(.045f,.012f),FVector2f(.365f,.12f)));
        P.Image({76,-19},{550,76},Wing,FLinearColor(1,1,1,.43f));
        Wing.SetUVRegion(FBox2f(FVector2f(.635f,.012f),FVector2f(.955f,.12f)));
        P.Image({1294,-19},{550,76},Wing,FLinearColor(1,1,1,.43f));
        ++P.Layer;
        const TArray<FVector2D> Top={{96,0},{345,33},{369,54},{590,67},{619,42},{742,42},{780,71},{1140,71},{1177,43},{1308,43},{1331,65},{1547,51},{1571,31},{1818,0}};
        const TArray<FVector2D> Bottom={{67,1080},{294,1048},{559,1025},{601,1024},{624,1047},{766,1047},{799,1032},{1119,1032},{1152,1047},{1302,1047},{1333,1028},{1363,1024},{1609,1046},{1855,1080}};
        P.Glow(Top,Line,6.5f);P.Glow(Bottom,Line,6.5f);
        // Layered glass-and-metal strips are clipped to the frame silhouette;
        // the central battle window stays completely transparent.
        P.Fill({{0,0},{93,0},{343,35},{365,57},{586,70},{585,82},{362,71},{338,49},{87,15},{0,14}},Line.CopyWithNewOpacity(.14f));
        P.Fill({{1920,0},{1825,0},{1570,34},{1546,54},{1334,68},{1330,81},{1553,68},{1578,49},{1830,15},{1920,14}},Line.CopyWithNewOpacity(.14f));
        P.Line({{80,13},{337,47},{359,68},{588,82},{621,56},{738,56},{776,85},{1145,85},{1181,57},{1302,57},{1325,79},{1555,65},{1581,45},{1832,11}},Line.CopyWithNewOpacity(.47f),1.2f);
        P.Line({{92,1065},{295,1035},{554,1013},{605,1011},{631,1035},{761,1035},{794,1020},{1125,1020},{1158,1035},{1298,1035},{1329,1016},{1366,1011},{1613,1034},{1838,1065}},Line.CopyWithNewOpacity(.55f),1.2f);
        P.Line({{110,1078},{305,1058},{543,1037},{594,1037},{618,1061},{777,1061},{810,1045},{858,1045}},Line.CopyWithNewOpacity(.55f),1);
        P.Line({{1062,1045},{1112,1045},{1145,1061},{1308,1061},{1342,1037},{1376,1037},{1616,1058},{1810,1078}},Line.CopyWithNewOpacity(.55f),1);
        for(bool Right:{false,true})
        {
            auto Mirror=[&](TArray<FVector2D> Points){if(Right)for(auto& V:Points)V.X=1920-V.X;return Points;};
            P.Line(Mirror({{0,15},{45,21},{52,41},{105,42},{128,78},{352,92},{383,66},{405,66}}),Line,1);
            P.Line(Mirror({{0,35},{35,39},{46,61},{112,64},{134,89},{355,89}}),Line.CopyWithNewOpacity(.45f),.8f);
            P.Line(Mirror({{30,124},{30,282},{49,307},{49,481},{33,503},{33,714},{59,742},{59,851},{0,912}}),Line.CopyWithNewOpacity(.75f));
            P.Line(Mirror({{0,946},{22,923},{42,905},{129,905},{153,926},{313,937},{339,965},{339,997},{391,997},{407,1011},{527,1011},{546,1042}}),Line);
            P.Line(Mirror({{14,964},{14,1020},{48,1053},{83,1053},{89,1045},{262,1045}}),Line.CopyWithNewOpacity(.7f));
            for(const double Y:{333.,504.,733.}){const double X=Right?1885:27;P.Box({X,Y},{7,7},Line);P.Line({{X-3,Y-3},{X+10,Y-3},{X+10,Y+10}},Line,.7f);}
            P.Line(Mirror({{191,0},{213,14},{428,15},{445,31},{565,44},{574,26},{662,26},{691,0}}),Line.CopyWithNewOpacity(.45f),.8f);
            P.Line(Mirror({{42,0},{51,18},{76,23},{82,48},{116,50},{137,76},{342,76}}),Line.CopyWithNewOpacity(.68f),1.2f);
            P.Line(Mirror({{18,82},{18,106},{9,116},{9,433},{25,449},{25,710},{14,724},{14,845},{35,866}}),Line.CopyWithNewOpacity(.3f),.8f);
            P.Line(Mirror({{7,9},{31,9},{40,30},{65,35},{73,66},{98,69}}),Line.CopyWithNewOpacity(.48f),1);
            for(int32 I=0;I<7;++I)
            {
                const double X=40+I*48,Y=16+I*6;
                P.Line(Mirror({{X,Y},{X+20,Y+3},{X+29,Y+13}}),Line.CopyWithNewOpacity(.28f),.8f);
                P.Fill(Mirror({{X+4,Y+9},{X+13,Y+10},{X+20,Y+21},{X+11,Y+20}}),Line.CopyWithNewOpacity(.14f));
            }
            for(const FVector2D Q:{FVector2D(69,28),FVector2D(375,29),FVector2D(531,22),FVector2D(60,1045)})
            {
                const auto Dot=Right?FVector2D(1920-Q.X,Q.Y):Q;
                P.Box(Dot-FVector2D(2),{4,4},GuLiGroundUI::Light(Line));
                P.Line({Dot+FVector2D(-5,-5),Dot+FVector2D(5,-5),Dot+FVector2D(5,5),Dot+FVector2D(-5,5),Dot+FVector2D(-5,-5)},Line.CopyWithNewOpacity(.6f),.8f);
            }
        }
        P.Line({{732,1047},{872,1047}},Line.CopyWithNewOpacity(.6f));P.Line({{1047,1047},{1186,1047}},Line.CopyWithNewOpacity(.6f));
        return P.Layer;
    }
private:TWeakObjectPtr<UGuLiGroundHUDChrome> Owner;bool bCompass=false;
};
void UGuLiGroundHUDChrome::SetTheme(UDataTable* Table)
{
    PanelColor=GuLiGroundUI::Color(Table,TEXT("Panel"));LineColor=GuLiGroundUI::Color(Table,TEXT("Line"));SecondaryColor=GuLiGroundUI::Color(Table,TEXT("Secondary"));
    AccentColor=GuLiGroundUI::Color(Table,TEXT("Accent"));TextColor=GuLiGroundUI::Color(Table,TEXT("Text"));
    CockpitBrush=GuLiGroundUI::Brush(Table,TEXT("CockpitShell"));GridBrush=GuLiGroundUI::Brush(Table,TEXT("InstrumentGrid"));Resources.Reset();
    if(auto* R=CockpitBrush.GetResourceObject())Resources.Add(R);if(auto* R=GridBrush.GetResourceObject())Resources.Add(R);
    if(Frame)Frame->Invalidate(EInvalidateWidgetReason::Paint);if(Compass)Compass->Invalidate(EInvalidateWidgetReason::Paint);
}
void UGuLiGroundHUDChrome::SetMode(uint8 In){if(Mode!=In){Mode=In;if(Compass)Compass->Invalidate(EInvalidateWidgetReason::Paint);}}
void UGuLiGroundHUDChrome::SetHeading(float In){if(!FMath::IsNearlyEqual(In,Heading,.05f)){Heading=In;if(Compass&&Mode==0)Compass->Invalidate(EInvalidateWidgetReason::Paint);}}
TSharedRef<SWidget> UGuLiGroundHUDChrome::RebuildWidget()
{
    Frame=SNew(SGuLiGroundCockpit,this,false);Compass=SNew(SGuLiGroundCockpit,this,true);
    return SNew(SOverlay).Visibility(EVisibility::HitTestInvisible)
        +SOverlay::Slot()[SNew(SInvalidationPanel)[Frame.ToSharedRef()]]
        +SOverlay::Slot()[SNew(SInvalidationPanel)[Compass.ToSharedRef()]];
}
void UGuLiGroundHUDChrome::ReleaseSlateResources(bool ReleaseChildren){Super::ReleaseSlateResources(ReleaseChildren);Frame.Reset();Compass.Reset();}
