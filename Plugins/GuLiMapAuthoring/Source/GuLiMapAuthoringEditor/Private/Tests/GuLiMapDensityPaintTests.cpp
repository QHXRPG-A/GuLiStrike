#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "GuLiMapAuthoring.h"
#include "GuLiMapAuthoringSubsystem.h"
#include "GuLiMapDensityMap.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonSerializer.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "WorldPartition/WorldPartition.h"

namespace
{
constexpr EAutomationTestFlags DensityFlags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;

FGuLiMapDensityMapRecord MakeDensityRecord(const double CellSize=100.0)
{
    FGuLiMapDensityMapRecord Record; Record.DensityMapId=FGuid(1,2,3,4); Record.CellSizeCm=CellSize;
    GuLiMap::EnsureDensityPresets(Record);
    if (FGuLiMapDensityLayer* Blue=Record.Layers.FindByPredicate([](const FGuLiMapDensityLayer& Layer){return Layer.LayerKey==TEXT("BlueOre");})) Blue->LayerId=FGuid(10,11,12,13);
    if (FGuLiMapDensityLayer* Red=Record.Layers.FindByPredicate([](const FGuLiMapDensityLayer& Layer){return Layer.LayerKey==TEXT("RedOre");})) Red->LayerId=FGuid(20,21,22,23);
    GuLiMap::NormalizeDensityMap(Record); return Record;
}

FGuLiMapDensityLayer* Layer(FGuLiMapDensityMapRecord& Record,const FName Key)
{
    return Record.Layers.FindByPredicate([&](const FGuLiMapDensityLayer& Item){return Item.LayerKey==Key;});
}

TStrongObjectPtr<UGuLiMapTypeDefinition> MakeOutpostType()
{
    TStrongObjectPtr<UGuLiMapTypeDefinition> Type(NewObject<UGuLiMapTypeDefinition>());
    Type->TypeId=TEXT("Outpost"); Type->DisplayName=TEXT("据点"); return Type;
}

FGuLiMapSnapshotEntry MakeTerritory(UGuLiMapTypeDefinition* Type,const TCHAR* MarkerKey,const FGuid MarkerId,const FGuid RegionId,const TArray<FVector2D>& Vertices)
{
    FGuLiMapSnapshotEntry Entry; Entry.Record.MarkerId=MarkerId; Entry.Record.MarkerKey=FName(MarkerKey); Entry.Record.DisplayName=MarkerKey; Entry.Record.Type=Type; Entry.Record.Parameters=Type->DefaultParameters;
    FGuLiMapRegionRecord Region; Region.RegionId=RegionId; Region.RegionKey=TEXT("Territory"); Region.DisplayName=TEXT("辖区"); Region.Geometry=FInstancedStruct::Make<FGuLiMapPolygonPrism>(); Region.Geometry.GetMutable<FGuLiMapPolygonPrism>().Vertices=Vertices;
    Entry.Record.Regions.Add(MoveTemp(Region)); return Entry;
}

bool HasSeverity(const TArray<FGuLiMapIssue>& Issues,const EGuLiMapIssueSeverity Severity)
{
    return Issues.ContainsByPredicate([&](const FGuLiMapIssue& Issue){return Issue.Severity==Severity;});
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiDensityGridBrushTest,"GuLi.MapAuthoring.Density.GridBrushAndSparseTiles",DensityFlags)
bool FGuLiDensityGridBrushTest::RunTest(const FString&)
{
    TestEqual(TEXT("Negative fraction floors to -1"),GuLiMap::WorldToDensityCell(-0.001,100.0),-1);
    TestEqual(TEXT("Negative exact boundary remains -1"),GuLiMap::WorldToDensityCell(-100.0,100.0),-1);
    TestEqual(TEXT("Past negative boundary floors to -2"),GuLiMap::WorldToDensityCell(-100.001,100.0),-2);
    FGuLiMapDensityMapRecord Record=MakeDensityRecord(); FGuLiMapDensityLayer* Blue=Layer(Record,TEXT("BlueOre")); FGuLiMapDensityLayer* Red=Layer(Record,TEXT("RedOre"));
    TestNotNull(TEXT("Blue preset"),Blue); TestNotNull(TEXT("Red preset"),Red);
    GuLiMap::SetDensityCell(*Blue,FIntPoint(-1,-1),17); TestEqual(TEXT("Negative tile lookup"),GuLiMap::GetDensityCell(*Blue,FIntPoint(-1,-1)),static_cast<uint8>(17));
    GuLiMap::SetDensityCell(*Blue,FIntPoint(-1,-1),0); TestEqual(TEXT("Zero-only tile reclaimed"),Blue->Tiles.Num(),0);
    FString Error;
    TestTrue(TEXT("Hard add brush"),GuLiMap::ApplyDensityBrush(Record,TEXT("BlueOre"),{FVector2D(50,50),FVector2D(50,50)},60.0,0.25,0.0,false,Error));
    Blue=Layer(Record,TEXT("BlueOre")); Red=Layer(Record,TEXT("RedOre"));
    TestEqual(TEXT("Repeated samples apply one maximum contribution"),GuLiMap::GetDensityCell(*Blue,FIntPoint(0,0)),static_cast<uint8>(64));
    TestEqual(TEXT("Red remains independent"),GuLiMap::GetDensityCell(*Red,FIntPoint(0,0)),static_cast<uint8>(0));
    TestTrue(TEXT("Red can overlap blue"),GuLiMap::ApplyDensityBrush(Record,TEXT("RedOre"),{FVector2D(50,50)},60.0,0.5,0.0,false,Error));
    TestEqual(TEXT("Blue preserved under overlap"),GuLiMap::GetDensityCell(*Layer(Record,TEXT("BlueOre")),FIntPoint(0,0)),static_cast<uint8>(64));
    TestEqual(TEXT("Red overlap value"),GuLiMap::GetDensityCell(*Layer(Record,TEXT("RedOre")),FIntPoint(0,0)),static_cast<uint8>(128));
    TestTrue(TEXT("Clamp add"),GuLiMap::ApplyDensityBrush(Record,TEXT("BlueOre"),{FVector2D(50,50)},60.0,1.0,0.0,false,Error));
    TestEqual(TEXT("Add clamps to 255"),GuLiMap::GetDensityCell(*Layer(Record,TEXT("BlueOre")),FIntPoint(0,0)),static_cast<uint8>(255));
    TestTrue(TEXT("Erase brush"),GuLiMap::ApplyDensityBrush(Record,TEXT("BlueOre"),{FVector2D(50,50)},60.0,1.0,0.0,true,Error));
    TestEqual(TEXT("Erase clamps to zero"),GuLiMap::GetDensityCell(*Layer(Record,TEXT("BlueOre")),FIntPoint(0,0)),static_cast<uint8>(0));
    TestEqual(TEXT("Blue zero tile reclaimed independently"),Layer(Record,TEXT("BlueOre"))->Tiles.Num(),0);
    TestTrue(TEXT("Soft brush"),GuLiMap::ApplyDensityBrush(Record,TEXT("BlueOre"),{FVector2D(0,0)},100.0,1.0,1.0,false,Error));
    const uint8 Soft=GuLiMap::GetDensityCell(*Layer(Record,TEXT("BlueOre")),FIntPoint(0,0));
    TestTrue(TEXT("Falloff produces partial density"),Soft>0&&Soft<255); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiDensityTerritoryTest,"GuLi.MapAuthoring.Density.TerritoryAssignmentAndSeverity",DensityFlags)
bool FGuLiDensityTerritoryTest::RunTest(const FString&)
{
    auto Type=MakeOutpostType(); FGuLiMapSnapshot Snapshot; Snapshot.MapPackage=TEXT("/Game/DensityTerritoryTest"); Snapshot.DensityMap=MakeDensityRecord();
    Snapshot.Markers.Add(MakeTerritory(Type.Get(),TEXT("Outpost_A"),FGuid(101,1,1,1),FGuid(201,1,1,1),{{0,0},{250,0},{250,200},{150,200},{150,100},{100,100},{100,200},{0,200}}));
    Snapshot.Markers.Add(MakeTerritory(Type.Get(),TEXT("Outpost_B"),FGuid(102,1,1,1),FGuid(202,1,1,1),{{250,0},{500,0},{500,200},{250,200}}));
    Snapshot.Markers.Add(MakeTerritory(Type.Get(),TEXT("Outpost_C"),FGuid(103,1,1,1),FGuid(203,1,1,1),{{25,25},{75,25},{75,75},{25,75}}));
    FGuLiMapDensityLayer* Blue=Layer(Snapshot.DensityMap.GetValue(),TEXT("BlueOre"));
    GuLiMap::SetDensityCell(*Blue,FIntPoint(0,0),255);   // Strictly inside A + C => overlap.
    GuLiMap::SetDensityCell(*Blue,FIntPoint(2,0),128);   // Exact A/B seam => stable owner A.
    GuLiMap::SetDensityCell(*Blue,FIntPoint(10,0),64);   // Unassigned.
    TMap<FString,FString> Files; TArray<FGuLiMapIssue> Issues;
    TestTrue(TEXT("Warnings do not block export"),GuLiMap::BuildFiles(Snapshot,Files,Issues));
    TestFalse(TEXT("No territory errors"),HasSeverity(Issues,EGuLiMapIssueSeverity::Error));
    TestTrue(TEXT("Anomalies are warnings"),HasSeverity(Issues,EGuLiMapIssueSeverity::Warning));
    TestEqual(TEXT("Density extension adds three files"),Files.Num(),9);
    TestTrue(TEXT("Overlap summary row"),Files[TEXT("density_territories.csv")].Contains(TEXT("overlap,,,,")));
    TestTrue(TEXT("Unassigned summary row"),Files[TEXT("density_territories.csv")].Contains(TEXT("unassigned,,,,")));
    TSharedPtr<FJsonObject> Json; TestTrue(TEXT("Density JSON parses"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Files[TEXT("density_layers.json")]),Json));
    TestEqual(TEXT("Two anomaly details"),Json->GetArrayField(TEXT("anomalies")).Num(),2);
    bool bBoundaryAssignedToA=false;
    for (const TSharedPtr<FJsonValue>& Value:Json->GetArrayField(TEXT("territory_summary")))
    {
        const TSharedPtr<FJsonObject> Row=Value->AsObject();
        if (Row->GetStringField(TEXT("layer_key"))==TEXT("BlueOre")&&Row->GetStringField(TEXT("marker_key"))==TEXT("Outpost_A")) bBoundaryAssignedToA=Row->GetIntegerField(TEXT("cell_count"))==1;
    }
    TestTrue(TEXT("Exact shared edge has stable Outpost_A owner"),bBoundaryAssignedToA);
    FGuLiMapSnapshot Tilted=Snapshot; Tilted.Markers[0].WorldTransform=FTransform(FRotator(2,0,0)); Issues.Reset(); Files.Reset();
    TestFalse(TEXT("Tilted Territory blocks export"),GuLiMap::BuildFiles(Tilted,Files,Issues)); TestTrue(TEXT("Tilt is an error"),HasSeverity(Issues,EGuLiMapIssueSeverity::Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiDensityPatchTest,"GuLi.MapAuthoring.Density.AtomicPatch",DensityFlags)
bool FGuLiDensityPatchTest::RunTest(const FString&)
{
    FGuLiMapDensityMapRecord Original=MakeDensityRecord(); GuLiMap::SetDensityCell(*Layer(Original,TEXT("BlueOre")),FIntPoint(1,-3),77);
    FGuLiMapDensityMapRecord Out=Original; FString Error;
    TestFalse(TEXT("Out-of-range member rejects whole patch"),UGuLiMapAuthoringSubsystem::ParseDensityPatch(TEXT("{\"schema_version\":1,\"layer_key\":\"BlueOre\",\"cells\":[{\"cell_x\":1,\"cell_y\":-3,\"density_u8\":100},{\"cell_x\":2,\"cell_y\":0,\"density_u8\":256}]}"),Original,Out,Error));
    TestEqual(TEXT("Failed patch leaves output unchanged"),GuLiMap::GetDensityCell(*Layer(Out,TEXT("BlueOre")),FIntPoint(1,-3)),static_cast<uint8>(77));
    TestFalse(TEXT("Duplicate cell rejected"),UGuLiMapAuthoringSubsystem::ParseDensityPatch(TEXT("{\"schema_version\":1,\"layer_key\":\"BlueOre\",\"cells\":[{\"cell_x\":0,\"cell_y\":0,\"density_u8\":1},{\"cell_x\":0,\"cell_y\":0,\"density_u8\":2}]}"),Original,Out,Error));
    TestFalse(TEXT("Non-integer coordinate rejected"),UGuLiMapAuthoringSubsystem::ParseDensityPatch(TEXT("{\"schema_version\":1,\"layer_key\":\"BlueOre\",\"cells\":[{\"cell_x\":0.5,\"cell_y\":0,\"density_u8\":1}]}"),Original,Out,Error));
    TestFalse(TEXT("Unknown root key rejected"),UGuLiMapAuthoringSubsystem::ParseDensityPatch(TEXT("{\"schema_version\":1,\"layer_key\":\"BlueOre\",\"cells\":[],\"extra\":true}"),Original,Out,Error));
    TestFalse(TEXT("Unknown layer rejected"),UGuLiMapAuthoringSubsystem::ParseDensityPatch(TEXT("{\"schema_version\":1,\"layer_key\":\"GreenOre\",\"cells\":[]}"),Original,Out,Error));
    TestTrue(TEXT("Valid exact-set patch"),UGuLiMapAuthoringSubsystem::ParseDensityPatch(TEXT("{\"schema_version\":1,\"layer_key\":\"BlueOre\",\"cells\":[{\"cell_x\":1,\"cell_y\":-3,\"density_u8\":0},{\"cell_x\":4,\"cell_y\":5,\"density_u8\":128}]}"),Original,Out,Error));
    TestEqual(TEXT("Zero clears cell"),GuLiMap::GetDensityCell(*Layer(Out,TEXT("BlueOre")),FIntPoint(1,-3)),static_cast<uint8>(0));
    TestEqual(TEXT("New cell set"),GuLiMap::GetDensityCell(*Layer(Out,TEXT("BlueOre")),FIntPoint(4,5)),static_cast<uint8>(128)); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiDensityExportTest,"GuLi.MapAuthoring.Density.ExtensionDeterminismAndLegacy",DensityFlags)
bool FGuLiDensityExportTest::RunTest(const FString&)
{
    FGuLiMapSnapshot Legacy; Legacy.MapPackage=TEXT("/Game/LegacyMap"); TMap<FString,FString> LegacyFiles; TArray<FGuLiMapIssue> Issues;
    TestTrue(TEXT("Legacy build"),GuLiMap::BuildFiles(Legacy,LegacyFiles,Issues)); TestEqual(TEXT("Legacy map remains exactly six files"),LegacyFiles.Num(),6);
    TestFalse(TEXT("Legacy layout schema has no density embedding"),LegacyFiles[TEXT("layout.json")].Contains(TEXT("density_")));
    FGuLiMapSnapshot Snapshot=Legacy; Snapshot.MapPackage=TEXT("/Game/DensityMap"); Snapshot.DensityMap=MakeDensityRecord();
    FGuLiMapDensityLayer* Blue=Layer(Snapshot.DensityMap.GetValue(),TEXT("BlueOre")); GuLiMap::SetDensityCell(*Blue,FIntPoint(5,-1),1); GuLiMap::SetDensityCell(*Blue,FIntPoint(-2,-1),255); GuLiMap::SetDensityCell(*Blue,FIntPoint(0,3),128);
    TMap<FString,FString> First,Second; Issues.Reset(); TestTrue(TEXT("Density build"),GuLiMap::BuildFiles(Snapshot,First,Issues)); Issues.Reset(); TestTrue(TEXT("Repeat density build"),GuLiMap::BuildFiles(Snapshot,Second,Issues));
    TestEqual(TEXT("Density map publishes nine files"),First.Num(),9);
    for (const TPair<FString,FString>& Pair:First) TestEqual(*FString(TEXT("Byte deterministic "))+Pair.Key,Second.FindRef(Pair.Key),Pair.Value);
    TestTrue(TEXT("All extension files present"),First.Contains(TEXT("density_layers.json"))&&First.Contains(TEXT("density_cells.csv"))&&First.Contains(TEXT("density_territories.csv")));
    const FString& Csv=First[TEXT("density_cells.csv")]; TestTrue(TEXT("Rows sorted y then x"),Csv.Find(TEXT(",-2,-1,"))<Csv.Find(TEXT(",5,-1,"))&&Csv.Find(TEXT(",5,-1,"))<Csv.Find(TEXT(",0,3,")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiDensityLifecycleTest,"GuLi.MapAuthoring.Density.EditorSingletonUndoWorldPartitionSaveReload",DensityFlags)
bool FGuLiDensityLifecycleTest::RunTest(const FString&)
{
    if (!FParse::Param(FCommandLine::Get(),TEXT("GuLiMapAuthoringTestSession")))
    { AddError(TEXT("Run in the dedicated -GuLiMapAuthoringTestSession; this test changes only a generated isolated map.")); return false; }
    GEditor->CreateNewMapForEditing(false,true);
    UWorld* World=GEditor->GetEditorWorldContext().World();
    if (!TestNotNull(TEXT("Isolated editor world"),World)) return false;
    TestNotNull(TEXT("Isolated validation map uses World Partition"),World->GetWorldPartition());
    UGuLiMapAuthoringSubsystem* Service=GEditor->GetEditorSubsystem<UGuLiMapAuthoringSubsystem>(); if (!TestNotNull(TEXT("Editor subsystem"),Service)) return false;
    TestTrue(TEXT("Ensure density singleton"),Service->EnsureDensityMap(2500).bSuccess); TestTrue(TEXT("Ensure is idempotent"),Service->EnsureDensityMap(2500).bSuccess);
    TArray<AGuLiMapDensityMap*> Maps=Service->LoadedDensityMaps(); TestEqual(TEXT("Exactly one density actor"),Maps.Num(),1); if (Maps.Num()!=1) return false;
    AGuLiMapDensityMap* Actor=Maps[0]; TestTrue(TEXT("Editor only"),Actor->IsEditorOnly()); TestFalse(TEXT("Never spatially loaded"),Actor->GetIsSpatiallyLoaded()); TestTrue(TEXT("Persistent level"),Actor->GetLevel()==World->PersistentLevel.Get()); TestTrue(TEXT("Identity transform"),Actor->GetActorTransform().Equals(FTransform::Identity));
    const FGuLiMapResult Patch=Service->UpdateDensityCells(TEXT("{\"schema_version\":1,\"layer_key\":\"BlueOre\",\"cells\":[{\"cell_x\":-4,\"cell_y\":2,\"density_u8\":128}]}")); TestTrue(TEXT("Subsystem patch succeeds with unassigned warning"),Patch.bSuccess);
    TestEqual(TEXT("Patched value"),GuLiMap::GetDensityCell(*Layer(Actor->Record,TEXT("BlueOre")),FIntPoint(-4,2)),static_cast<uint8>(128));
    GEditor->UndoTransaction(); TestEqual(TEXT("Undo restores zero"),GuLiMap::GetDensityCell(*Layer(Actor->Record,TEXT("BlueOre")),FIntPoint(-4,2)),static_cast<uint8>(0));
    GEditor->RedoTransaction(); TestEqual(TEXT("Redo restores density"),GuLiMap::GetDensityCell(*Layer(Actor->Record,TEXT("BlueOre")),FIntPoint(-4,2)),static_cast<uint8>(128));
    const FString Folder=TEXT("/Game/GuLiStrike/Editor/MapAuthoring/Validation/DensityAuto_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString MapFile=FPackageName::LongPackageNameToFilename(Folder/TEXT("Layout"),FPackageName::GetMapPackageExtension());
    TestTrue(TEXT("Save isolated World Partition map"),FEditorFileUtils::SaveMap(World,MapFile)); TestTrue(TEXT("Save authoring packages"),Service->SaveAuthoringPackages());
    FGuLiMapSnapshot Collected; TArray<FGuLiMapIssue> CollectionIssues; TestTrue(TEXT("World Partition descriptor collection"),Service->CollectSnapshot(Collected,CollectionIssues,true)); TestTrue(TEXT("Density present in collected value snapshot"),Collected.DensityMap.IsSet());
    const FGuLiMapResult Before=Service->GetDensitySnapshot(); TestTrue(TEXT("Density snapshot before reload"),Before.bSuccess);
    TestTrue(TEXT("Open neutral map"),FEditorFileUtils::LoadMap(TEXT("/Engine/Maps/Entry"),false,false)); TestTrue(TEXT("Reopen isolated map"),FEditorFileUtils::LoadMap(MapFile,false,false));
    const FGuLiMapResult After=Service->GetDensitySnapshot(); TestTrue(TEXT("Density snapshot after reload"),After.bSuccess); TestEqual(TEXT("Save/reopen density bytes"),After.Json,Before.Json);
    TestTrue(TEXT("Saved density map exports atomically"),Service->ExportMap().bSuccess); AddInfo(TEXT("Isolated density validation map retained: ")+Folder/TEXT("Layout")); return true;
}

#endif
