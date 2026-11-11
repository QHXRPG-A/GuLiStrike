#include "Gameplay/Navigation/GuLiCircularNavigationComponent.h"
#include "AI/NavigationModifier.h"
#include "AI/Navigation/NavigationRelevantData.h"
#include "NavAreas/NavArea_Null.h"

UGuLiCircularNavigationComponent::UGuLiCircularNavigationComponent()
{
    bAttachToOwnersRoot=false;
    bNavigationRelevant=false;
}

void UGuLiCircularNavigationComponent::SetCircles(TConstArrayView<GuLiCircleAvoidance::FCircle> InCircles)
{
    check(IsInGameThread());
    // Producers call only on a geometry/version change, never in a movement step.
    Circles.Reset(InCircles.Num());
    for (const auto& Circle:InCircles)
        if (ensure(Circle.Id && Circle.Radius>0 && FMath::IsFinite(Circle.MinimumZ)
            && FMath::IsFinite(Circle.MaximumZ) && Circle.MaximumZ>Circle.MinimumZ))
            Circles.Add(Circle);
    bNavigationRelevant=!Circles.IsEmpty();
    UpdateNavigationBounds();
    RefreshNavigationModifiers();
}

void UGuLiCircularNavigationComponent::CalcAndCacheBounds() const
{
    Bounds=FBox(ForceInit);
    for (const auto& Circle:Circles)
        Bounds+=FBox(FVector(Circle.Center.X-Circle.Radius,Circle.Center.Y-Circle.Radius,Circle.MinimumZ),
            FVector(Circle.Center.X+Circle.Radius,Circle.Center.Y+Circle.Radius,Circle.MaximumZ));
}

void UGuLiCircularNavigationComponent::GetNavigationData(FNavigationRelevantData& Data) const
{
    for (const auto& Circle:Circles)
    {
        const FVector Center(Circle.Center.X,Circle.Center.Y,(double(Circle.MinimumZ)+Circle.MaximumZ)*.5);
        FAreaNavModifier Area(Circle.Radius,Circle.MaximumZ-Circle.MinimumZ,
            FTransform(Center),UNavArea_Null::StaticClass());
        Area.SetIncludeAgentHeight(true);
        Data.Modifiers.Add(Area);
    }
}
