#pragma once
#include "CoreMinimal.h"
class UGuLiMapTypeDefinition;
class FComponentVisualizer;
namespace GuLiMapEditor
{
    void RegisterViewport();
    void UnregisterViewport();
    void BeginPlacement(UGuLiMapTypeDefinition* Type);
    void EndInteraction();
    TSharedRef<FComponentVisualizer> CreateVisualizer();
}
