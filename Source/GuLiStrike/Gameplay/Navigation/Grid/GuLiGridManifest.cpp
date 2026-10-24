#include "Network/GuLiHybridManifestRegistry.h"
#include "Network/GuLiHybridManifest.h"
#include "Gameplay/Navigation/Grid/GuLiGridQueries.h"

static bool AppendGroundGrid(UWorld& World,FGuLiHybridManifestData& Data,FString& Error)
{
    if (!World.GetMapName().EndsWith(TEXT("LVL_CommanderMassPrototype"))) return true;
    const auto S=GuLiGrid::AuthoredSnapshot(World);
    if (!S) {Error=TEXT("Commander map requires a valid 300 cm ground grid bake");return false;}
    Data.Strings.Add(TEXT("ground_grid/source_hash"),S->SourceHash);
    Data.Strings.Add(TEXT("ground_grid/traffic_schema"),TEXT("2"));
    Data.Numbers.Add(TEXT("ground_grid/cell_cm"),300.);Data.Numbers.Add(TEXT("ground_grid/blocked_seconds"),3.);
    Data.Numbers.Add(TEXT("ground_grid/work_retry_seconds"),5.);return true;
}
static const FGuLiHybridManifestRegistration GroundGridProvider({TEXT("GroundGrid"),EGuLiHybridManifestPhase::Derived,{},AppendGroundGrid});
