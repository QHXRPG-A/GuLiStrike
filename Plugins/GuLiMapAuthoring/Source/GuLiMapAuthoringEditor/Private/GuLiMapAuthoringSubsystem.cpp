#include "GuLiMapAuthoringSubsystem.h"
#include "GuLiMapAuthoring.h"
#include "GuLiMapAuthoringSettings.h"
#include "GuLiMapMarker.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/Level.h"
#include "FileHelpers.h"
#include "Framework/Docking/TabManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionHandle.h"

UWorld* UGuLiMapAuthoringSubsystem::EditorWorld() const
{
    if (!GEditor || GEditor->PlayWorld || GEditor->bIsSimulatingInEditor) return nullptr;
    UWorld* W=GEditor->GetEditorWorldContext().World();
    return W&&W->WorldType==EWorldType::Editor?W:nullptr;
}
TArray<AGuLiMapMarker*> UGuLiMapAuthoringSubsystem::LoadedMarkers() const
{
    TArray<AGuLiMapMarker*> Out;
    if (UWorld* W=EditorWorld()) for (TActorIterator<AGuLiMapMarker> It(W);It;++It) if (IsValid(*It)) Out.Add(*It);
    Out.Sort([](const auto& A,const auto& B){return A.Record.MarkerKey.LexicalLess(B.Record.MarkerKey);});
    return Out;
}
TArray<UGuLiMapTypeDefinition*> UGuLiMapAuthoringSubsystem::ListTypes()
{
    auto& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    FARFilter Filter; Filter.ClassPaths.Add(UGuLiMapTypeDefinition::StaticClass()->GetClassPathName()); Filter.bRecursiveClasses=true;
    Filter.PackagePaths.Add(FName(*GetDefault<UGuLiMapAuthoringSettings>()->TypeAssetPath)); Filter.bRecursivePaths=true;
    TArray<FAssetData> Assets; Registry.GetAssets(Filter,Assets);
    TArray<UGuLiMapTypeDefinition*> Out;
    for (const auto& A:Assets) if (auto* Type=Cast<UGuLiMapTypeDefinition>(A.GetAsset())) Out.Add(Type);
    Out.Sort([](const auto& A,const auto& B){return A.TypeId.LexicalLess(B.TypeId);}); return Out;
}
UGuLiMapTypeDefinition* UGuLiMapAuthoringSubsystem::CreateType(const FString& ID)
{
    if (!EditorWorld() || !GuLiMap::IsKey(ID) || ID.Equals(TEXT("None"),ESearchCase::IgnoreCase)) return nullptr;
    for (auto* Type:ListTypes()) if (Type->TypeId==FName(*ID)) return nullptr;
    const FString PackageName=GetDefault<UGuLiMapAuthoringSettings>()->TypeAssetPath/TEXT("DA_")+ID;
    if (!FPackageName::IsValidLongPackageName(PackageName) || FPackageName::DoesPackageExist(PackageName) || FindPackage(nullptr,*PackageName)) return nullptr;
    UPackage* Package=CreatePackage(*PackageName);
    auto* Type=NewObject<UGuLiMapTypeDefinition>(Package,*FPaths::GetBaseFilename(PackageName),RF_Public|RF_Standalone|RF_Transactional);
    Type->TypeId=FName(*ID); Type->DisplayName=ID;
    FAssetRegistryModule::AssetCreated(Type); Package->MarkPackageDirty(); return Type;
}
FGuLiMapResult UGuLiMapAuthoringSubsystem::EnsurePresets()
{
    FGuLiMapResult Result;
    if (!EditorWorld()) { Result.Issues.Emplace(TEXT("No editable world / PIE active.")); return Result; }
    for (const FString ID:{TEXT("Resource"),TEXT("Outpost"),TEXT("Generic")})
    {
        bool Exists=false; for (auto* Type:ListTypes()) if (Type->TypeId==FName(*ID)) Exists=true;
        if (Exists) continue;
        auto* Type=CreateType(ID);
        if (!Type) { Result.Issues.Emplace(TEXT("Cannot create preset ")+ID); continue; }
        if (ID!=TEXT("Generic"))
        {
            FGuLiMapRegionRecord Area; Area.RegionId=FGuid::NewGuid(); Area.RegionKey=ID==TEXT("Resource")?TEXT("ResourceArea"):TEXT("Capture");
            Area.Geometry=FInstancedStruct::Make<FGuLiMapCylinder>(); Type->DefaultRegions.Add(Area);
        }
        if (ID==TEXT("Resource"))
        {
            Type->DisplayName=TEXT("资源点"); Type->Color=FLinearColor(0.2f,0.9f,0.35f);
            Type->DefaultParameters.AddProperty(TEXT("ResourceKind"),EPropertyBagPropertyType::Name);
            Type->DefaultParameters.SetValueName(TEXT("ResourceKind"),TEXT("Unspecified"));
            Type->DefaultParameters.AddProperty(TEXT("Amount"),EPropertyBagPropertyType::Double);
            Type->DefaultParameters.SetValueDouble(TEXT("Amount"),1);
        }
        if (ID==TEXT("Outpost"))
        {
            Type->DisplayName=TEXT("据点"); Type->Color=FLinearColor(1,0.55f,0.1f);
            Type->DefaultParameters.AddProperty(TEXT("Tier"),EPropertyBagPropertyType::Int32); Type->DefaultParameters.SetValueInt32(TEXT("Tier"),1);
        }
        if (ID==TEXT("Generic")) Type->DisplayName=TEXT("通用标记");
    }
    Result.bSuccess=Result.Issues.IsEmpty(); return Result;
}
AGuLiMapMarker* UGuLiMapAuthoringSubsystem::CreateMarker(UGuLiMapTypeDefinition* Type,FVector Location)
{
    UWorld* W=EditorWorld(); if (!W || !Type || Location.ContainsNaN()) return nullptr;
    TArray<FGuLiMapIssue> Issues; GuLiMap::ValidateType(*Type,Issues); if (!Issues.IsEmpty()) return nullptr;
    const FScopedTransaction Tx(NSLOCTEXT("GuLiMap","Create","创建地图标记"));
    W->PersistentLevel->Modify();
    FActorSpawnParameters P; P.OverrideLevel=W->PersistentLevel; P.ObjectFlags=RF_Transactional;
    auto* A=W->SpawnActor<AGuLiMapMarker>(Location,FRotator::ZeroRotator,P); if (!A) return nullptr;
    A->Modify(); A->Record.Type=Type; A->Record.DisplayName=Type->DisplayName; A->Record.Parameters=Type->DefaultParameters; A->Record.Regions=Type->DefaultRegions;
    A->RegenerateIdentity(); A->EnsureRegionIdentities(); A->SetActorLabel(Type->DisplayName.IsEmpty()?Type->TypeId.ToString():Type->DisplayName); A->SetFolderPath(TEXT("GuLi/MapAuthoring"));
    A->RefreshVisuals(); return A;
}
bool UGuLiMapAuthoringSubsystem::CollectSnapshot(FGuLiMapSnapshot& Out,TArray<FGuLiMapIssue>& Issues,bool RequireSaved)
{
    UWorld* W=EditorWorld(); if (!W) { Issues.Emplace(TEXT("No editable current world / PIE or SIE active.")); return false; }
    Out.MapPackage=W->GetOutermost()->GetName();
    auto Dirty=[&](UPackage* P){if (RequireSaved&&P&&P->IsDirty()) Issues.Emplace(TEXT("Save package before export: ")+P->GetName());};
    if (RequireSaved && (Out.MapPackage.StartsWith(TEXT("/Temp/")) || !FPackageName::DoesPackageExist(Out.MapPackage))) Issues.Emplace(TEXT("Save the current map before export."));
    Dirty(W->GetOutermost()); for (UPackage* P:W->GetOutermost()->GetExternalPackages()) Dirty(P);
    if (!Issues.IsEmpty()) return false;
    // Hold references for the whole collection; unloaded markers are loaded without loading landscape/art.
    TArray<FWorldPartitionReference> References; TSet<FGuid> DescriptorIds;
    if (UWorldPartition* WP=W->GetWorldPartition())
    {
        WP->ForEachActorDescContainerInstance([&](UActorDescContainerInstance* Container)
        {
            for (UActorDescContainerInstance::TIterator<AGuLiMapMarker> It(Container);It;++It)
            {
                auto* Desc=*It; DescriptorIds.Add(Desc->GetGuid());
                References.Emplace(Desc);
                auto* A=Cast<AGuLiMapMarker>(Desc->GetActor());
                if (!A) Issues.Emplace(TEXT("Incomplete World Partition snapshot: could not load descriptor ")+GuLiMap::Guid(Desc->GetGuid()));
                else if (A->GetWorld()!=W || A->GetLevel()!=W->PersistentLevel || Container!=WP->GetActorDescContainerInstance())
                    Issues.Emplace(TEXT("Marker in a nested container/Level Instance is unsupported."),A->Record.MarkerId);
                if (Desc->GetIsSpatiallyLoaded() || !Desc->GetDataLayers().IsEmpty()) Issues.Emplace(TEXT("Marker must be non-spatial and outside Data Layers: ")+GuLiMap::Guid(Desc->GetGuid()));
            }
        },true);
    }
    TSet<FGuid> LoadedActorIds;
    for (auto* A:LoadedMarkers())
    {
        if (A->GetLevel()!=W->PersistentLevel || A->GetAttachParentActor()) Issues.Emplace(TEXT("Marker must be unattached in the persistent level."),A->Record.MarkerId);
        LoadedActorIds.Add(A->GetActorGuid()); Dirty(A->GetPackage());
        if (RequireSaved&&W->GetWorldPartition()&&!DescriptorIds.Contains(A->GetActorGuid()))
            Issues.Emplace(TEXT("Saved World Partition marker has no Actor Descriptor; completeness cannot be guaranteed."),A->Record.MarkerId);
        if (auto* T=A->Record.Type.LoadSynchronous()) Dirty(T->GetOutermost());
        Out.Markers.Add({A->Record,A->GetActorTransform()});
    }
    for (const FGuid& ID:DescriptorIds) if (!LoadedActorIds.Contains(ID)) Issues.Emplace(TEXT("Incomplete marker collection: missing ActorGuid ")+GuLiMap::Guid(ID));
    // Duplicate IDs are a type-catalog error, even if only one of the colliding assets is used.
    TSet<FName> TypeIds;
    for (auto* Type:ListTypes()) { if (TypeIds.Contains(Type->TypeId)) Issues.Emplace(TEXT("Duplicate configured TypeId: ")+Type->TypeId.ToString()); TypeIds.Add(Type->TypeId); }
    return Issues.IsEmpty();
}
FGuLiMapResult UGuLiMapAuthoringSubsystem::GetSnapshot()
{
    FGuLiMapResult R; FGuLiMapSnapshot Snapshot; TMap<FString,FString> Files;
    if (CollectSnapshot(Snapshot,R.Issues,false) && GuLiMap::BuildFiles(Snapshot,Files,R.Issues)) { R.bSuccess=true; R.Json=Files[TEXT("layout.json")]; }
    return R;
}
FGuLiMapResult UGuLiMapAuthoringSubsystem::ValidateMap()
{
    FGuLiMapResult R; FGuLiMapSnapshot Snapshot;
    if (CollectSnapshot(Snapshot,R.Issues,false)) GuLiMap::Validate(Snapshot,R.Issues);
    R.bSuccess=R.Issues.IsEmpty(); return R;
}
FGuLiMapResult UGuLiMapAuthoringSubsystem::ExportMap()
{
    FGuLiMapResult R; FGuLiMapSnapshot Snapshot; TMap<FString,FString> Files;
    if (!CollectSnapshot(Snapshot,R.Issues,true) || !GuLiMap::BuildFiles(Snapshot,Files,R.Issues)) return R;
    const FString Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("Data/MapAuthoring")/Snapshot.MapPackage.RightChop(1));
    FString Error; if (!GuLiMap::Publish(Directory,Files,Error)) { R.Issues.Emplace(Error); return R; }
    R.bSuccess=true; R.Json=Files[TEXT("layout.json")];
    TArray<FString> Names; Files.GetKeys(Names); Names.Sort(); for (const auto& Name:Names) R.Files.Add(Directory/Name);
    return R;
}
bool UGuLiMapAuthoringSubsystem::SaveAuthoringPackages()
{
    UWorld* W=EditorWorld(); if (!W) return false;
    if (W->GetOutermost()->GetName().StartsWith(TEXT("/Temp/")) && !FEditorFileUtils::SaveCurrentLevel()) return false;
    TArray<UPackage*> Packages={W->GetOutermost()}; Packages.Append(W->GetOutermost()->GetExternalPackages());
    for (auto* A:LoadedMarkers()) { Packages.AddUnique(A->GetPackage()); if (auto* T=A->Record.Type.LoadSynchronous()) Packages.AddUnique(T->GetOutermost()); }
    for (auto* T:ListTypes()) Packages.AddUnique(T->GetOutermost());
    if (FApp::IsUnattended()) return UEditorLoadingAndSavingUtils::SavePackages(Packages,true);
    return FEditorFileUtils::PromptForCheckoutAndSave(Packages,true,false)==FEditorFileUtils::PR_Success;
}
void UGuLiMapAuthoringSubsystem::OpenPanel() { FGlobalTabmanager::Get()->TryInvokeTab(FName(TEXT("GuLiMapAuthoring"))); }
void UGuLiMapAuthoringSubsystem::ClosePanel()
{
    if (auto Tab=FGlobalTabmanager::Get()->FindExistingLiveTab(FName(TEXT("GuLiMapAuthoring")))) Tab->RequestCloseTab();
}

bool UGuLiMapAuthoringSubsystem::ParsePatch(const FString& Json,const FGuLiMapMarkerRecord& Original,const FTransform& Transform,FGuLiMapMarkerRecord& Out,FTransform& OutTransform,FString& Error)
{
    TSharedPtr<FJsonObject> J;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),J)||!J) { Error=TEXT("Malformed JSON object."); return false; }
    if (!GuLiMap::CheckKeys(*J,{TEXT("schema_version"),TEXT("marker_id"),TEXT("marker_key"),TEXT("display_name"),TEXT("enabled"),TEXT("note"),TEXT("tags"),TEXT("parameters"),TEXT("position"),TEXT("rotation_pitch_yaw_roll"),TEXT("regions")},Error)) return false;
    double Version=0; if (!J->HasTypedField<EJson::Number>(TEXT("schema_version"))||!J->TryGetNumberField(TEXT("schema_version"),Version)||Version!=1) { Error=TEXT("schema_version must be number 1."); return false; }
    FGuLiMapMarkerRecord R=Original; FTransform X=Transform;
    auto String=[&](const FJsonObject& O,const FString& Key,FString& Target){ if (!O.HasField(Key)) return true; if (!O.HasTypedField<EJson::String>(Key)||!O.TryGetStringField(Key,Target)) { Error=TEXT("Expected string: ")+Key; return false; } return true; };
    auto Bool=[&](const FJsonObject& O,const FString& Key,bool& Target){ if (!O.HasField(Key)) return true; if (!O.HasTypedField<EJson::Boolean>(Key)||!O.TryGetBoolField(Key,Target)) { Error=TEXT("Expected bool: ")+Key; return false; } return true; };
    FString ID=GuLiMap::Guid(R.MarkerId),Key=R.MarkerKey.ToString();
    if (!String(*J,TEXT("marker_id"),ID)||ID!=GuLiMap::Guid(R.MarkerId)) { Error=TEXT("Marker identity is immutable."); return false; }
    if (!String(*J,TEXT("marker_key"),Key)||!String(*J,TEXT("display_name"),R.DisplayName)||!String(*J,TEXT("note"),R.Note)||!Bool(*J,TEXT("enabled"),R.bEnabled)) return false;
    R.MarkerKey=FName(*Key);
    for (const FString K:{TEXT("position"),TEXT("rotation_pitch_yaw_roll")}) if (J->HasField(K))
    {
        FVector V; if (!GuLiMap::ReadVector(J->TryGetField(K),V)) { Error=TEXT("Expected finite [x,y,z]: ")+K; return false; }
        if (K==TEXT("position")) X.SetLocation(V); else X.SetRotation(FRotator(V.X,V.Y,V.Z).Quaternion());
    }
    if (J->HasField(TEXT("tags")))
    {
        const TArray<TSharedPtr<FJsonValue>>* Tags=nullptr;
        if (!J->TryGetArrayField(TEXT("tags"),Tags)) { Error=TEXT("tags must be array."); return false; }
        R.Tags.Reset(); for (const auto& V:*Tags) { FString S; if (V->Type!=EJson::String||!V->TryGetString(S)) { Error=TEXT("Tag must be string."); return false; } R.Tags.Add(FName(*S)); }
    }
    if (J->HasField(TEXT("parameters")))
    {
        const TSharedPtr<FJsonObject>* Params=nullptr;
        if (!J->TryGetObjectField(TEXT("parameters"),Params)) { Error=TEXT("parameters must be object."); return false; }
        for (const auto& P:(*Params)->Values)
        {
            const auto* D=R.Parameters.FindPropertyDescByName(FName(*P.Key));
            if (!D) { Error=TEXT("Unknown field: ")+P.Key; return false; }
            if (!GuLiMap::SetField(R.Parameters,*D,P.Value,Error)) return false;
        }
    }
    if (J->HasField(TEXT("regions")))
    {
        const TArray<TSharedPtr<FJsonValue>>* Regions=nullptr;
        if (!J->TryGetArrayField(TEXT("regions"),Regions)) { Error=TEXT("regions must be an array of patches."); return false; }
        TSet<FGuid> Patched;
        for (const auto& Value:*Regions)
        {
            const TSharedPtr<FJsonObject>* O=nullptr;
            if (!Value->TryGetObject(O)) { Error=TEXT("Region patch must be object."); return false; }
            const auto& A=**O;
            if (!GuLiMap::CheckKeys(A,{TEXT("region_id"),TEXT("region_key"),TEXT("display_name"),TEXT("enabled"),TEXT("position"),TEXT("rotation_pitch_yaw_roll"),TEXT("shape_type"),TEXT("geometry"),TEXT("remove")},Error)) return false;
            FGuLiMapRegionRecord Area; int32 Index=INDEX_NONE; FString RegionID; bool Remove=false;
            if (!String(A,TEXT("region_id"),RegionID)||!Bool(A,TEXT("remove"),Remove)) return false;
            if (!RegionID.IsEmpty())
            {
                FGuid G;
                if (!FGuid::Parse(RegionID,G)||!G.IsValid()||Patched.Contains(G)) { Error=TEXT("Invalid/duplicate region identity."); return false; }
                Patched.Add(G); Index=R.Regions.IndexOfByPredicate([&](const auto& V){return V.RegionId==G;});
                if (Index==INDEX_NONE) { Error=TEXT("Unknown region identity."); return false; }
                Area=R.Regions[Index];
            }
            else Area.RegionId=FGuid::NewGuid();
            if (Remove)
            {
                if (Index==INDEX_NONE || A.Values.Num()!=2) { Error=TEXT("Removal requires only region_id and remove."); return false; }
                R.Regions.RemoveAt(Index); continue;
            }
            FString RegionKey=Area.RegionKey.ToString();
            if (!String(A,TEXT("region_key"),RegionKey)||!String(A,TEXT("display_name"),Area.DisplayName)||!Bool(A,TEXT("enabled"),Area.bEnabled)) return false;
            Area.RegionKey=FName(*RegionKey);
            for (const FString K:{TEXT("position"),TEXT("rotation_pitch_yaw_roll")}) if (A.HasField(K))
            {
                FVector V; if (!GuLiMap::ReadVector(A.TryGetField(K),V)) { Error=TEXT("Invalid region transform."); return false; }
                if (K==TEXT("position")) Area.Translation=V; else Area.Rotation=FRotator(V.X,V.Y,V.Z);
            }
            auto Existing=GuLiMap::FindGeometry(Area.Geometry); FString Shape=Existing?Existing->GetShapeType().ToString():TEXT("Cylinder");
            if (!String(A,TEXT("shape_type"),Shape)) return false;
            auto H=GuLiMap::FindGeometry(FName(*Shape)); if (!H) { Error=TEXT("Unknown shape type."); return false; }
            if (A.HasField(TEXT("geometry")))
            {
                const TSharedPtr<FJsonObject>* G=nullptr;
                if (!A.TryGetObjectField(TEXT("geometry"),G)||!H->FromJson(**G,Area.Geometry,Error)) { if (Error.IsEmpty()) Error=TEXT("Invalid geometry object."); return false; }
            }
            else if (!Existing || Existing->GetShapeType()!=H->GetShapeType()) Area.Geometry.InitializeAs(H->GetStruct());
            if (Index==INDEX_NONE) R.Regions.Add(Area); else R.Regions[Index]=Area;
        }
    }
    FGuLiMapSnapshot Check; Check.Markers.Add({R,X}); TArray<FGuLiMapIssue> Issues; GuLiMap::Validate(Check,Issues);
    if (!Issues.IsEmpty()) { Error=Issues[0].Message; return false; }
    Out=MoveTemp(R); OutTransform=X; return true;
}
FGuLiMapResult UGuLiMapAuthoringSubsystem::UpdateMarker(const FString& MarkerId,const FString& PatchJson)
{
    FGuLiMapResult Result; FGuid ID;
    if (!FGuid::Parse(MarkerId,ID)||!ID.IsValid()) { Result.Issues.Emplace(TEXT("Invalid MarkerId.")); return Result; }
    AGuLiMapMarker* Actor=nullptr; for (auto* A:LoadedMarkers()) if (A->Record.MarkerId==ID) { if (Actor) { Result.Issues.Emplace(TEXT("Ambiguous MarkerId.")); return Result; } Actor=A; }
    if (!Actor) { Result.Issues.Emplace(TEXT("Marker is not loaded in current editable world."),ID); return Result; }
    FGuLiMapMarkerRecord Record; FTransform Transform; FString Error;
    if (!ParsePatch(PatchJson,Actor->Record,Actor->GetActorTransform(),Record,Transform,Error)) { Result.Issues.Emplace(Error,ID); return Result; }
    FGuLiMapSnapshot Snapshot;
    if (!CollectSnapshot(Snapshot,Result.Issues,false)) return Result;
    for (auto& E:Snapshot.Markers) if (E.Record.MarkerId==ID) E={Record,Transform};
    GuLiMap::Validate(Snapshot,Result.Issues); if (!Result.Issues.IsEmpty()) return Result;
    const FScopedTransaction Tx(NSLOCTEXT("GuLiMap","Update","修改地图标记")); Actor->Modify();
    Actor->Record=MoveTemp(Record); Actor->SetActorTransform(Transform);
    FPropertyChangedEvent Changed(FindFProperty<FProperty>(AGuLiMapMarker::StaticClass(),GET_MEMBER_NAME_CHECKED(AGuLiMapMarker,Record)),EPropertyChangeType::ValueSet);
    Actor->PostEditChangeProperty(Changed);
    Result.bSuccess=true; return Result;
}
