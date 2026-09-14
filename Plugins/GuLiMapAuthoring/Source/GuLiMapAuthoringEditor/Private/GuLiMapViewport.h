#pragma once
#include "CoreMinimal.h"
class UGuLiMapTypeDefinition;
class AGuLiMapDensityMap;
class FComponentVisualizer;
struct FGuLiMapDensityBrushSettings
{
    FName LayerKey = TEXT("BlueOre");
    double RadiusCm = 20000.0;
    double Strength01 = 0.25;
    double Falloff01 = 0.5;
    bool bErase = false;
};
namespace GuLiMapEditor
{
    void RegisterViewport();
    void UnregisterViewport();
    void BeginPlacement(UGuLiMapTypeDefinition* Type);
    void BeginDensityPaint(AGuLiMapDensityMap* DensityMap,const FGuLiMapDensityBrushSettings& Settings);
    void UpdateDensityBrush(const FGuLiMapDensityBrushSettings& Settings);
    bool IsDensityPainting();
    void EndInteraction();
    TSharedRef<FComponentVisualizer> CreateVisualizer();
}
