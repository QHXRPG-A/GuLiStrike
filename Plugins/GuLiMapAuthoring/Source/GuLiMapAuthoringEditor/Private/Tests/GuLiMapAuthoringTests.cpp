#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "GuLiMapAuthoring.h"
#include "GuLiMapAuthoringSubsystem.h"
#include "GuLiMapMarker.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "FileHelpers.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonSerializer.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
constexpr EAutomationTestFlags Flags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
TStrongObjectPtr<UGuLiMapTypeDefinition> MakeType()
{
    TStrongObjectPtr<UGuLiMapTypeDefinition> T(NewObject<UGuLiMapTypeDefinition>());
    T->TypeId=TEXT("SupplyPoint"); T->DisplayName=TEXT("补给点");
    T->DefaultParameters.AddProperty(TEXT("Amount"),EPropertyBagPropertyType::Double); T->DefaultParameters.SetValueDouble(TEXT("Amount"),7.25);
    T->DefaultParameters.AddProperty(TEXT("Description"),EPropertyBagPropertyType::String); return T;
}
FGuLiMapSnapshot MakeSnapshot(UGuLiMapTypeDefinition* T)
{
    FGuLiMapSnapshot S; S.MapPackage=TEXT("/Game/MapAuthoringTest");
    FGuLiMapSnapshotEntry E; E.Record.MarkerId=FGuid::NewGuid(); E.Record.MarkerKey=TEXT("Outpost_A"); E.Record.DisplayName=TEXT("中文, \"据点\"\n第二行");
    E.Record.Note=TEXT("注释\n换行"); E.Record.Type=T; E.Record.Parameters=T->DefaultParameters; E.Record.Tags={TEXT("North"),TEXT("Resource")};
    E.Record.Parameters.SetValueString(TEXT("Description"),TEXT("资源,\"金属\"\n下一行"));
    FGuLiMapRegionRecord R; R.RegionId=FGuid::NewGuid(); R.RegionKey=TEXT("Build"); R.Geometry=FInstancedStruct::Make<FGuLiMapPolygonPrism>();
    R.Geometry.GetMutable<FGuLiMapPolygonPrism>().Vertices={FVector2D(0,0),FVector2D(400,0),FVector2D(400,400),FVector2D(200,200),FVector2D(0,400)};
    R.Translation=FVector(111.125,333.25,20); R.Rotation=FRotator(12,30,5); E.Record.Regions.Add(R);
    R.RegionId=FGuid::NewGuid(); R.RegionKey=TEXT("Capture"); R.Geometry=FInstancedStruct::Make<FGuLiMapCylinder>(); E.Record.Regions.Add(R);
    E.WorldTransform=FTransform(FRotator(5,90,20),FVector(12345678.123456789,-444.123456789,56.5));
    S.Markers.Add(E); return S;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiMapFieldsTest,"GuLi.MapAuthoring.FieldsMigration",Flags)
bool FGuLiMapFieldsTest::RunTest(const FString&)
{
    auto T=MakeType(); FInstancedPropertyBag Values=T->DefaultParameters; Values.SetValueDouble(TEXT("Amount"),42.125);
    const FGuid Id=Values.FindPropertyDescByName(TEXT("Amount"))->ID;
    T->DefaultParameters.RenameProperty(TEXT("Amount"),TEXT("Stock")); T->DefaultParameters.AddProperty(TEXT("Active"),EPropertyBagPropertyType::Bool);
    T->DefaultParameters.SetValueBool(TEXT("Active"),true); FString Error;
    TestTrue(TEXT("GUID rename + added defaults migrate"),GuLiMap::MigrateFields(Values,T->DefaultParameters,false,Error));
    TestEqual(TEXT("Rename keeps field ID"),Values.FindPropertyDescByName(TEXT("Stock"))->ID,Id);
    TestEqual(TEXT("Rename keeps value"),Values.GetValueDouble(TEXT("Stock")).GetValue(),42.125);
    TestTrue(TEXT("New field default"),Values.GetValueBool(TEXT("Active")).GetValue());
    T->DefaultParameters.SetValueDouble(TEXT("Stock"),999);
    GuLiMap::MigrateFields(Values,T->DefaultParameters,false,Error); TestEqual(TEXT("Changed default preserves instance"),Values.GetValueDouble(TEXT("Stock")).GetValue(),42.125);
    T->DefaultParameters.RemovePropertyByName(TEXT("Stock"));
    TestFalse(TEXT("Deletion refuses silent data loss"),GuLiMap::MigrateFields(Values,T->DefaultParameters,false,Error));
    TestEqual(TEXT("Deletion retains old data"),Values.GetValueDouble(TEXT("Stock")).GetValue(),42.125);
    FPropertyBagPropertyDesc Changed(TEXT("Stock"),EPropertyBagPropertyType::String); Changed.ID=Id;
    T->DefaultParameters.AddProperties({Changed}); T->DefaultParameters.SetValueString(TEXT("Stock"),TEXT("reset"));
    TestFalse(TEXT("Type change requires explicit upgrade"),GuLiMap::MigrateFields(Values,T->DefaultParameters,false,Error));
    TestTrue(TEXT("Explicit upgrade"),GuLiMap::MigrateFields(Values,T->DefaultParameters,true,Error));
    TestEqual(TEXT("Incompatible value resets, no conversion"),Values.GetValueString(TEXT("Stock")).GetValue(),FString(TEXT("reset")));
    TestTrue(TEXT("Compatible values survive upgrade"),Values.GetValueBool(TEXT("Active")).GetValue());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiMapGeometryTest,"GuLi.MapAuthoring.GeometryAndTransforms",Flags)
bool FGuLiMapGeometryTest::RunTest(const FString&)
{
    for (FName Shape:GuLiMap::ListShapes())
    {
        auto H=GuLiMap::FindGeometry(Shape); FInstancedStruct S; S.InitializeAs(H->GetStruct()); FString Error;
        TestTrue(*Shape.ToString(),H->Validate(S,Error)); TestTrue(TEXT("Filled geometry"),H->BuildMesh(S).Triangles.Num()>0);
    }
    auto T=MakeType(); auto S=MakeSnapshot(T.Get()); const auto& R=S.Markers[0].Record.Regions[0];
    auto H=GuLiMap::FindGeometry(R.Geometry); FString Error;
    TestTrue(TEXT("Concave polygon"),H->Validate(R.Geometry,Error));
    TestEqual(TEXT("Concave prism triangles: sides + 2*(n-2)"),H->BuildMesh(R.Geometry).Triangles.Num(),48);
    const FVector P(12,35,90); const FVector World=(R.GetTransform()*S.Markers[0].WorldTransform).TransformPosition(P);
    TestTrue(TEXT("Region composition"),World.Equals(S.Markers[0].WorldTransform.TransformPosition(R.GetTransform().TransformPosition(P)),1.e-7));
    auto Bad=R.Geometry; Bad.GetMutable<FGuLiMapPolygonPrism>().Vertices={FVector2D(0,0),FVector2D(100,100),FVector2D(0,100),FVector2D(100,0)};
    TestFalse(TEXT("Self intersection"),H->Validate(Bad,Error));
    TestEqual(TEXT("Invalid preview has no fill"),H->BuildMesh(Bad).Triangles.Num(),0);
    Bad.GetMutable<FGuLiMapPolygonPrism>().Vertices={FVector2D(0,0),FVector2D(100,0),FVector2D(100,0)};
    TestFalse(TEXT("Zero edge"),H->Validate(Bad,Error));
    TArray<FGuLiMapIssue> Issues; GuLiMap::Validate(S,Issues); TestEqual(TEXT("Multiple named areas valid"),Issues.Num(),0);
    S.Markers[0].Record.Regions[1].RegionId=R.RegionId; Issues.Reset(); GuLiMap::Validate(S,Issues);
    TestTrue(TEXT("Duplicate region identity rejected"),Issues.Num()>0); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiMapExportTest,"GuLi.MapAuthoring.ExportDeterminismAndAtomicity",Flags)
bool FGuLiMapExportTest::RunTest(const FString&)
{
    auto T=MakeType(); auto S=MakeSnapshot(T.Get()); TMap<FString,FString> A,B; TArray<FGuLiMapIssue> Issues;
    TestTrue(TEXT("Build six files"),GuLiMap::BuildFiles(S,A,Issues)); TestEqual(TEXT("Six standard files"),A.Num(),6);
    TestTrue(TEXT("Repeat export"),GuLiMap::BuildFiles(S,B,Issues));
    for (const auto& P:A) TestEqual(*P.Key,B.FindRef(P.Key),P.Value);
    TestTrue(TEXT("Chinese CSV escaping"),A[TEXT("markers.csv")].Contains(TEXT("\"中文, \"\"据点\"\"\n第二行\"")));
    TSharedPtr<FJsonObject> J; TestTrue(TEXT("JSON parse"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(A[TEXT("layout.json")]),J));
    TestEqual(TEXT("JSON marker count"),J->GetArrayField(TEXT("markers")).Num(),1);
    TestEqual(TEXT("JSON region count"),J->GetArrayField(TEXT("markers"))[0]->AsObject()->GetArrayField(TEXT("regions")).Num(),2);
    const double X=J->GetArrayField(TEXT("markers"))[0]->AsObject()->GetObjectField(TEXT("world_transform"))->GetArrayField(TEXT("position"))[0]->AsNumber();
    TestEqual(TEXT("Double precision roundtrip"),X,S.Markers[0].WorldTransform.GetLocation().X);
    const FString Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("Data/MapAuthoringValidation")/GuLiMap::Guid(FGuid::NewGuid()));
    FString Error; TestTrue(TEXT("Publish initial batch"),GuLiMap::Publish(Directory,A,Error));
    S.Markers[0].Record.Note=TEXT("第二版"); Issues.Reset(); GuLiMap::BuildFiles(S,B,Issues);
    TestTrue(TEXT("Publish replacement"),GuLiMap::Publish(Directory,B,Error));
    FString Prior; FFileHelper::LoadFileToString(Prior,*((Directory+TEXT(".previous"))/TEXT("layout.json"))); TestEqual(TEXT("Previous batch preserved"),Prior,A[TEXT("layout.json")]);
    TMap<FString,FString> Bad=B; Bad.Add(TEXT("../escape.csv"),TEXT("unsafe"));
    TestFalse(TEXT("Unsafe export rejected before publication"),GuLiMap::Publish(Directory,Bad,Error));
    FString Current; FFileHelper::LoadFileToString(Current,*(Directory/TEXT("layout.json"))); TestEqual(TEXT("Rejected publish leaves full batch intact"),Current,B[TEXT("layout.json")]);
    S.Markers[0].Record.MarkerId.Invalidate(); Issues.Reset(); TMap<FString,FString> Untouched=A;
    TestFalse(TEXT("Invalid snapshot rejected"),GuLiMap::BuildFiles(S,Untouched,Issues)); TestEqual(TEXT("Failed build no partial output"),Untouched[TEXT("layout.json")],A[TEXT("layout.json")]);
    AddInfo(TEXT("Export evidence retained at ")+Directory); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiMapPatchTest,"GuLi.MapAuthoring.AtomicPatch",Flags)
bool FGuLiMapPatchTest::RunTest(const FString&)
{
    auto T=MakeType(); auto S=MakeSnapshot(T.Get()); const auto& E=S.Markers[0]; FGuLiMapMarkerRecord Out=E.Record; FTransform X=E.WorldTransform; FString Error;
    const FString Before=Out.Note;
    TestFalse(TEXT("Mixed valid and wrong typed update rejected"),UGuLiMapAuthoringSubsystem::ParsePatch(TEXT("{\"schema_version\":1,\"note\":\"must not apply\",\"parameters\":{\"Amount\":\"wrong\"}}"),E.Record,E.WorldTransform,Out,X,Error));
    TestEqual(TEXT("No partial output on failure"),Out.Note,Before);
    TestFalse(TEXT("Unknown field rejected"),UGuLiMapAuthoringSubsystem::ParsePatch(TEXT("{\"schema_version\":1,\"parameters\":{\"Unknown\":4}}"),E.Record,E.WorldTransform,Out,X,Error));
    TestFalse(TEXT("Identity cannot change"),UGuLiMapAuthoringSubsystem::ParsePatch(TEXT("{\"schema_version\":1,\"marker_id\":\"bad\"}"),E.Record,E.WorldTransform,Out,X,Error));
    TestTrue(TEXT("New named region + field update"),UGuLiMapAuthoringSubsystem::ParsePatch(TEXT("{\"schema_version\":1,\"parameters\":{\"Amount\":12.125},\"regions\":[{\"region_key\":\"Influence\",\"shape_type\":\"Sphere\",\"geometry\":{\"radius\":750}}]}"),E.Record,E.WorldTransform,Out,X,Error));
    TestEqual(TEXT("Third region preserved"),Out.Regions.Num(),3); TestEqual(TEXT("Correct field value"),Out.Parameters.GetValueDouble(TEXT("Amount")).GetValue(),12.125);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiMapLifecycleTest,"GuLi.MapAuthoring.EditorIdentityUndoSaveReload",Flags)
bool FGuLiMapLifecycleTest::RunTest(const FString&)
{
    if (!FParse::Param(FCommandLine::Get(),TEXT("GuLiMapAuthoringTestSession")))
    { AddError(TEXT("Run in dedicated editor with -GuLiMapAuthoringTestSession; this test intentionally changes its isolated current map.")); return false; }
    UWorld* W=FAutomationEditorCommonUtils::CreateNewMap();
    auto* S=GEditor->GetEditorSubsystem<UGuLiMapAuthoringSubsystem>(); if (!TestNotNull(TEXT("Editor subsystem"),S)) return false;
    const FString Folder=TEXT("/Game/GuLiStrike/Editor/MapAuthoring/Validation/Auto_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    auto* Type=NewObject<UGuLiMapTypeDefinition>(CreatePackage(*(Folder/TEXT("Type"))),TEXT("Type"),RF_Public|RF_Standalone|RF_Transactional);
    Type->TypeId=TEXT("LifecycleType"); Type->DefaultParameters.AddProperty(TEXT("Count"),EPropertyBagPropertyType::Int32);
    FGuLiMapRegionRecord R; R.RegionKey=TEXT("Capture"); R.Geometry=FInstancedStruct::Make<FGuLiMapPolygonPrism>(); Type->DefaultRegions.Add(R);
    auto* A=S->CreateMarker(Type,FVector(10,20,30)); if (!TestNotNull(TEXT("Created configurable type marker"),A)) return false;
    const FGuid ID=A->Record.MarkerId, RegionID=A->Record.Regions[0].RegionId;
    TestTrue(TEXT("New identity"),ID.IsValid()&&RegionID.IsValid());
    auto Result=S->UpdateMarker(GuLiMap::Guid(ID),TEXT("{\"schema_version\":1,\"note\":\"transaction\",\"position\":[100,200,300]}"));
    TestTrue(TEXT("Shared service update"),Result.bSuccess); GEditor->UndoTransaction();
    TestEqual(TEXT("Undo retains ID"),A->Record.MarkerId,ID); TestEqual(TEXT("Undo complete transform"),A->GetActorLocation(),FVector(10,20,30));
    GEditor->RedoTransaction(); TestEqual(TEXT("Redo retains ID"),A->Record.MarkerId,ID); TestEqual(TEXT("Redo complete transform"),A->GetActorLocation(),FVector(100,200,300));
    TestFalse(TEXT("Unsaved map cannot export"),S->ExportMap().bSuccess);
    GEditor->SelectNone(false,true); GEditor->SelectActor(A,true,true);
    FString Copy; GEditor->edactCopySelected(W,&Copy);
    { const FScopedTransaction Tx(FText::FromString(TEXT("MapAuthoring test paste"))); GEditor->edactPasteSelected(W,false,false,false,&Copy); }
    auto* Pasted=GEditor->GetSelectedActors()->GetTop<AGuLiMapMarker>();
    if (!TestNotNull(TEXT("Native paste"),Pasted)) return false;
    TestNotEqual(TEXT("Paste new marker ID"),Pasted->Record.MarkerId,ID); TestNotEqual(TEXT("Paste new region ID"),Pasted->Record.Regions[0].RegionId,RegionID);
    TestNotEqual(TEXT("Paste new business key"),Pasted->Record.MarkerKey,A->Record.MarkerKey);
    const FGuid PastedID=Pasted->Record.MarkerId;
    { const FScopedTransaction Tx(FText::FromString(TEXT("MapAuthoring test duplicate"))); GEditor->edactDuplicateSelected(W->PersistentLevel,false); }
    auto* Duplicate=GEditor->GetSelectedActors()->GetTop<AGuLiMapMarker>();
    TestTrue(TEXT("Native duplicate identity"),Duplicate&&Duplicate->Record.MarkerId!=PastedID&&Duplicate->Record.MarkerId!=ID);
    auto Bad=S->UpdateMarker(GuLiMap::Guid(ID),TEXT("{\"schema_version\":1,\"note\":\"partial\",\"parameters\":{\"Bad\":2}}"));
    TestFalse(TEXT("Bad API update"),Bad.bSuccess); TestEqual(TEXT("Failed API leaves record intact"),A->Record.Note,FString(TEXT("transaction")));
    const FString TypeFile=FPackageName::LongPackageNameToFilename(Type->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(TypeFile),true);
    FSavePackageArgs Save; Save.TopLevelFlags=RF_Public|RF_Standalone; Save.SaveFlags=SAVE_NoError;
    TestTrue(TEXT("Save type"),UPackage::SavePackage(Type->GetOutermost(),Type,*TypeFile,Save));
    const FString MapFile=FPackageName::LongPackageNameToFilename(Folder/TEXT("Layout"),FPackageName::GetMapPackageExtension());
    TestTrue(TEXT("Save isolated map"),FEditorFileUtils::SaveMap(W,MapFile));
    TestTrue(TEXT("Shared save service supports unattended automation"),S->SaveAuthoringPackages());
    auto Before=S->GetSnapshot(); TestTrue(TEXT("Before reload snapshot"),Before.bSuccess);
    TestTrue(TEXT("Open another map"),FEditorFileUtils::LoadMap(TEXT("/Engine/Maps/Entry"),false,false));
    TestTrue(TEXT("Reopen saved map"),FEditorFileUtils::LoadMap(MapFile,false,false));
    auto After=S->GetSnapshot(); TestTrue(TEXT("After reload snapshot"),After.bSuccess); TestEqual(TEXT("Full save/reopen data equality"),After.Json,Before.Json);
    TestTrue(TEXT("Saved map export"),S->ExportMap().bSuccess);
    AddInfo(TEXT("Isolated lifecycle map retained: ")+Folder/TEXT("Layout")); return true;
}
#endif
