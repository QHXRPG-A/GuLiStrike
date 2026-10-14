#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UEdGraph;
namespace GuLiHybridAuthoring
{
    // Same canonical representation for inspection and edit preconditions.
    TSharedRef<FJsonObject> ReadGraph(const UEdGraph& Graph);
}
