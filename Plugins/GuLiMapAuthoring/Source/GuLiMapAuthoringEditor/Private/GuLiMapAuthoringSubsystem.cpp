#include "GuLiMapAuthoringSubsystem.h"
#include "GuLiMapAuthoring.h"
#include "GuLiMapAuthoringSettings.h"
#include "GuLiMapDensityMap.h"
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
TArray<AGuLiMapDensityMap*> UGuLiMapAuthoringSubsystem::LoadedDensityMaps() const
{
    TArray<AGuLiMapDensityMap*> Out;
    if (UWorld* W=EditorWorld()) for (TActorIterator<AGuLiMapDensityMap> It(W);It;++It) if (IsValid(*It)) Out.Add(*It);
    Out.Sort([](const auto& A,const auto& B){return GuLiMap::Guid(A.Record.DensityMapId)<GuLiMap::Guid(B.Record.DensityMapId);});
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
            if (ID==TEXT("Outpost"))
            {
                FGuLiMapRegionRecord Territory; Territory.RegionId=FGuid::NewGuid(); Territory.RegionKey=TEXT("Territory"); Territory.DisplayName=TEXT("据点辖区");
                Territory.Geometry=FInstancedStruct::Make<FGuLiMapPolygonPrism>(); Type->DefaultRegions.Add(MoveTemp(Territory));
            }
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
    Result.bSuccess=!GuLiMap::HasErrors(Result.Issues); return Result;
}
FGuLiMapResult UGuLiMapAuthoringSubsystem::EnsureDensityMap(const double CellSizeCm)
{
    FGuLiMapResult Result; UWorld* W=EditorWorld();
    if (!W) { Result.Issues.Emplace(TEXT("No editable world / PIE active.")); return Result; }
    if (!FMath::IsFinite(CellSizeCm)||CellSizeCm<=0.0) { Result.Issues.Emplace(TEXT("CellSizeCm must be finite and positive.")); return Result; }
    TArray<AGuLiMapDensityMap*> Maps=LoadedDensityMaps();
    if (Maps.Num()>1) { Result.Issues.Emplace(TEXT("More than one AGuLiMapDensityMap exists; keep exactly one in the persistent level.")); return Result; }
    if (Maps.Num()==1)
    {
        AGuLiMapDensityMap* Actor=Maps[0]; FGuLiMapDensityMapRecord Candidate=Actor->Record;
        const bool bPresetChanged=GuLiMap::EnsureDensityPresets(Candidate);
        bool bCellSizeChanged=false;
        if (!FMath::IsNearlyEqual(Candidate.CellSizeCm,CellSizeCm))
        {
            if (!GuLiMap::IsDensityMapEmpty(Candidate)) { Result.Issues.Emplace(TEXT("Density data already exists. Clear all layers explicitly before changing CellSizeCm.")); return Result; }
            Candidate.CellSizeCm=CellSizeCm; bCellSizeChanged=true;
        }
        if (bPresetChanged||bCellSizeChanged)
        {
            const FScopedTransaction Tx(NSLOCTEXT("GuLiMap","EnsureDensity","创建或补齐资源密度图")); Actor->Modify(); Actor->Record=MoveTemp(Candidate); Actor->RefreshVisuals(bCellSizeChanged);
        }
        Result.bSuccess=true; return Result;
    }
    const FScopedTransaction Tx(NSLOCTEXT("GuLiMap","CreateDensity","创建资源密度图"));
    W->PersistentLevel->Modify(); FActorSpawnParameters P; P.OverrideLevel=W->PersistentLevel; P.ObjectFlags=RF_Transactional;
    AGuLiMapDensityMap* Actor=W->SpawnActor<AGuLiMapDensityMap>(FVector::ZeroVector,FRotator::ZeroRotator,P);
    if (!Actor) { Result.Issues.Emplace(TEXT("Could not create AGuLiMapDensityMap.")); return Result; }
    Actor->Modify(); Actor->Record.CellSizeCm=CellSizeCm; GuLiMap::EnsureDensityPresets(Actor->Record); Actor->SetActorLabel(TEXT("GuLi Map Density")); Actor->SetFolderPath(TEXT("GuLi/MapAuthoring")); Actor->RefreshVisuals(true);
    Result.bSuccess=true; return Result;
}
AGuLiMapMarker* UGuLiMapAuthoringSubsystem::CreateMarker(UGuLiMapTypeDefinition* Type,FVector Location)
{
    UWorld* W=EditorWorld(); if (!W || !Type || Location.ContainsNaN()) return nullptr;
    TArray<FGuLiMapIssue> Issues; GuLiMap::ValidateType(*Type,Issues); if (GuLiMap::HasErrors(Issues)) return nullptr;
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
    if (GuLiMap::HasErrors(Issues)) return false;
    // Hold references for the whole collection; unloaded markers are loaded without loading landscape/art.
    TArray<FWorldPartitionReference> References; TSet<FGuid> MarkerDescriptorIds,DensityDescriptorIds;
    if (UWorldPartition* WP=W->GetWorldPartition())
    {
        WP->ForEachActorDescContainerInstance([&](UActorDescContainerInstance* Container)
        {
            for (UActorDescContainerInstance::TIterator<AGuLiMapMarker> It(Container);It;++It)
            {
                auto* Desc=*It; MarkerDescriptorIds.Add(Desc->GetGuid());
                References.Emplace(Desc);
                auto* A=Cast<AGuLiMapMarker>(Desc->GetActor());
                if (!A) Issues.Emplace(TEXT("Incomplete World Partition snapshot: could not load descriptor ")+GuLiMap::Guid(Desc->GetGuid()));
                else if (A->GetWorld()!=W || A->GetLevel()!=W->PersistentLevel || Container!=WP->GetActorDescContainerInstance())
                    Issues.Emplace(TEXT("Marker in a nested container/Level Instance is unsupported."),A->Record.MarkerId);
                if (Desc->GetIsSpatiallyLoaded() || !Desc->GetDataLayers().IsEmpty()) Issues.Emplace(TEXT("Marker must be non-spatial and outside Data Layers: ")+GuLiMap::Guid(Desc->GetGuid()));
            }
        },true);
        WP->ForEachActorDescContainerInstance([&](UActorDescContainerInstance* Container)
        {
            for (UActorDescContainerInstance::TIterator<AGuLiMapDensityMap> It(Container);It;++It)
            {
                auto* Desc=*It; DensityDescriptorIds.Add(Desc->GetGuid()); References.Emplace(Desc);
                auto* A=Cast<AGuLiMapDensityMap>(Desc->GetActor());
                if (!A) Issues.Emplace(TEXT("Incomplete World Partition density snapshot: could not load descriptor ")+GuLiMap::Guid(Desc->GetGuid()));
                else if (A->GetWorld()!=W||A->GetLevel()!=W->PersistentLevel||Container!=WP->GetActorDescContainerInstance()) Issues.Emplace(TEXT("Density map in a nested container/Level Instance is unsupported."));
                if (Desc->GetIsSpatiallyLoaded()||!Desc->GetDataLayers().IsEmpty()) Issues.Emplace(TEXT("Density map must be non-spatial and outside Data Layers: ")+GuLiMap::Guid(Desc->GetGuid()));
            }
        },true);
    }
    TSet<FGuid> LoadedActorIds;
    for (auto* A:LoadedMarkers())
    {
        if (A->GetLevel()!=W->PersistentLevel || A->GetAttachParentActor()) Issues.Emplace(TEXT("Marker must be unattached in the persistent level."),A->Record.MarkerId);
        if (A->GetIsSpatiallyLoaded()||!A->GetDataLayerInstances().IsEmpty()) Issues.Emplace(TEXT("Marker must be non-spatial and outside Data Layers."),A->Record.MarkerId);
        LoadedActorIds.Add(A->GetActorGuid()); Dirty(A->GetPackage());
        if (RequireSaved&&W->GetWorldPartition()&&!MarkerDescriptorIds.Contains(A->GetActorGuid()))
            Issues.Emplace(TEXT("Saved World Partition marker has no Actor Descriptor; completeness cannot be guaranteed."),A->Record.MarkerId);
        if (auto* T=A->Record.Type.LoadSynchronous()) Dirty(T->GetOutermost());
        Out.Markers.Add({A->Record,A->GetActorTransform()});
    }
    for (const FGuid& ID:MarkerDescriptorIds) if (!LoadedActorIds.Contains(ID)) Issues.Emplace(TEXT("Incomplete marker collection: missing ActorGuid ")+GuLiMap::Guid(ID));
    TArray<AGuLiMapDensityMap*> DensityMaps=LoadedDensityMaps();
    if (DensityMaps.Num()>1) Issues.Emplace(TEXT("More than one AGuLiMapDensityMap exists in the current world."));
    for (AGuLiMapDensityMap* A:DensityMaps)
    {
        if (A->GetLevel()!=W->PersistentLevel||A->GetAttachParentActor()) Issues.Emplace(TEXT("Density map must be unattached in the persistent level."));
        if (A->GetIsSpatiallyLoaded()||!A->GetDataLayerInstances().IsEmpty()) Issues.Emplace(TEXT("Density map must be non-spatial and outside Data Layers."));
        if (!A->GetActorTransform().Equals(FTransform::Identity)) Issues.Emplace(TEXT("Density map Actor transform must remain identity."));
        LoadedActorIds.Add(A->GetActorGuid()); Dirty(A->GetPackage());
        if (RequireSaved&&W->GetWorldPartition()&&!DensityDescriptorIds.Contains(A->GetActorGuid())) Issues.Emplace(TEXT("Saved World Partition density map has no Actor Descriptor; completeness cannot be guaranteed."));
        if (!Out.DensityMap.IsSet()) Out.DensityMap=A->Record;
    }
    for (const FGuid& ID:DensityDescriptorIds) if (!LoadedActorIds.Contains(ID)) Issues.Emplace(TEXT("Incomplete density-map collection: missing ActorGuid ")+GuLiMap::Guid(ID));
    // Duplicate IDs are a type-catalog error, even if only one of the colliding assets is used.
    TSet<FName> TypeIds;
    for (auto* Type:ListTypes()) { if (TypeIds.Contains(Type->TypeId)) Issues.Emplace(TEXT("Duplicate configured TypeId: ")+Type->TypeId.ToString()); TypeIds.Add(Type->TypeId); }
    return !GuLiMap::HasErrors(Issues);
}
FGuLiMapResult UGuLiMapAuthoringSubsystem::GetSnapshot()
{
    FGuLiMapResult R; FGuLiMapSnapshot Snapshot; TMap<FString,FString> Files;
    if (CollectSnapshot(Snapshot,R.Issues,false) && GuLiMap::BuildFiles(Snapshot,Files,R.Issues)) { R.bSuccess=true; R.Json=Files[TEXT("layout.json")]; }
    return R;
}
FGuLiMapResult UGuLiMapAuthoringSubsystem::GetDensitySnapshot()
{
    FGuLiMapResult R; FGuLiMapSnapshot Snapshot; TMap<FString,FString> Files;
    if (!CollectSnapshot(Snapshot,R.Issues,false)) return R;
    if (!Snapshot.DensityMap.IsSet()) { R.Issues.Emplace(TEXT("No AGuLiMapDensityMap exists in the current map.")); return R; }
    if (GuLiMap::BuildFiles(Snapshot,Files,R.Issues)) { R.bSuccess=true; R.Json=Files.FindRef(TEXT("density_layers.json")); }
    return R;
}
FGuLiMapResult UGuLiMapAuthoringSubsystem::ValidateMap()
{
    FGuLiMapResult R; FGuLiMapSnapshot Snapshot;
    if (CollectSnapshot(Snapshot,R.Issues,false)) GuLiMap::Validate(Snapshot,R.Issues);
    R.bSuccess=!GuLiMap::HasErrors(R.Issues); return R;
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
    for (auto* A:LoadedDensityMaps()) Packages.AddUnique(A->GetPackage());
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
    if (GuLiMap::HasErrors(Issues)) { Error=Issues.FindByPredicate([](const FGuLiMapIssue& Issue){return Issue.Severity==EGuLiMapIssueSeverity::Error;})->Message; return false; }
    Out=MoveTemp(R); OutTransform=X; return true;
}
bool UGuLiMapAuthoringSubsystem::ParseDensityPatch(const FString& Json,const FGuLiMapDensityMapRecord& Original,FGuLiMapDensityMapRecord& Out,FString& Error)
{
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root) { Error=TEXT("Malformed JSON object."); return false; }
    if (!GuLiMap::CheckKeys(*Root,{TEXT("schema_version"),TEXT("layer_key"),TEXT("cells")},Error)) return false;
    double Version=0;
    if (!Root->HasTypedField<EJson::Number>(TEXT("schema_version"))||!Root->TryGetNumberField(TEXT("schema_version"),Version)||Version!=1.0) { Error=TEXT("schema_version must be number 1."); return false; }
    FString LayerKey;
    if (!Root->HasTypedField<EJson::String>(TEXT("layer_key"))||!Root->TryGetStringField(TEXT("layer_key"),LayerKey)||!GuLiMap::IsKey(LayerKey)) { Error=TEXT("layer_key must be a valid ASCII identifier."); return false; }
    const FName LayerName(*LayerKey);
    if (!Original.Layers.ContainsByPredicate([&](const FGuLiMapDensityLayer& Layer){return Layer.LayerKey==LayerName;})) { Error=TEXT("Unknown density layer: ")+LayerKey; return false; }
    const TArray<TSharedPtr<FJsonValue>>* Cells=nullptr;
    if (!Root->TryGetArrayField(TEXT("cells"),Cells)) { Error=TEXT("cells must be an array."); return false; }
    struct FPatchCell { FIntPoint Cell; uint8 Density; };
    TArray<FPatchCell> Parsed; TSet<FIntPoint> Seen;
    auto Integer=[&](const FJsonObject& Object,const FString& Key,const double Minimum,const double Maximum,int64& Value)
    {
        double Number=0;
        if (!Object.HasTypedField<EJson::Number>(Key)||!Object.TryGetNumberField(Key,Number)||!FMath::IsFinite(Number)||FMath::TruncToDouble(Number)!=Number||Number<Minimum||Number>Maximum)
        { Error=Key+TEXT(" must be an in-range integer."); return false; }
        Value=static_cast<int64>(Number); return true;
    };
    for (const TSharedPtr<FJsonValue>& Value:*Cells)
    {
        const TSharedPtr<FJsonObject>* Object=nullptr;
        if (!Value||!Value->TryGetObject(Object)||!Object||!GuLiMap::CheckKeys(**Object,{TEXT("cell_x"),TEXT("cell_y"),TEXT("density_u8")},Error))
        { if (Error.IsEmpty()) Error=TEXT("Each density cell must be an object."); return false; }
        int64 X=0,Y=0,Density=0;
        if (!Integer(**Object,TEXT("cell_x"),MIN_int32,MAX_int32,X)||!Integer(**Object,TEXT("cell_y"),MIN_int32,MAX_int32,Y)||!Integer(**Object,TEXT("density_u8"),0,255,Density)) return false;
        const FIntPoint Cell(static_cast<int32>(X),static_cast<int32>(Y));
        if (Seen.Contains(Cell)) { Error=TEXT("Duplicate density cell in patch."); return false; }
        Seen.Add(Cell); Parsed.Add({Cell,static_cast<uint8>(Density)});
    }
    FGuLiMapDensityMapRecord Candidate=Original;
    FGuLiMapDensityLayer* Layer=Candidate.Layers.FindByPredicate([&](const FGuLiMapDensityLayer& Item){return Item.LayerKey==LayerName;});
    for (const FPatchCell& Cell:Parsed) GuLiMap::SetDensityCell(*Layer,Cell.Cell,Cell.Density);
    GuLiMap::NormalizeDensityMap(Candidate); Out=MoveTemp(Candidate); return true;
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
    GuLiMap::Validate(Snapshot,Result.Issues); if (GuLiMap::HasErrors(Result.Issues)) return Result;
    const FScopedTransaction Tx(NSLOCTEXT("GuLiMap","Update","修改地图标记")); Actor->Modify();
    Actor->Record=MoveTemp(Record); Actor->SetActorTransform(Transform);
    FPropertyChangedEvent Changed(FindFProperty<FProperty>(AGuLiMapMarker::StaticClass(),GET_MEMBER_NAME_CHECKED(AGuLiMapMarker,Record)),EPropertyChangeType::ValueSet);
    Actor->PostEditChangeProperty(Changed);
    Result.bSuccess=true; return Result;
}
FGuLiMapResult UGuLiMapAuthoringSubsystem::UpdateDensityCells(const FString& PatchJson)
{
    FGuLiMapResult Result; TArray<AGuLiMapDensityMap*> Maps=LoadedDensityMaps();
    if (Maps.Num()!=1) { Result.Issues.Emplace(Maps.IsEmpty()?TEXT("No AGuLiMapDensityMap exists in the current editable world."):TEXT("Density map identity is ambiguous; keep exactly one actor.")); return Result; }
    AGuLiMapDensityMap* Actor=Maps[0]; FGuLiMapDensityMapRecord Candidate; FString Error;
    if (!ParseDensityPatch(PatchJson,Actor->Record,Candidate,Error)) { Result.Issues.Emplace(Error); return Result; }
    FGuLiMapSnapshot Snapshot;
    if (!CollectSnapshot(Snapshot,Result.Issues,false)) return Result;
    Snapshot.DensityMap=Candidate; GuLiMap::Validate(Snapshot,Result.Issues);
    if (GuLiMap::HasErrors(Result.Issues)) return Result;
    const FScopedTransaction Tx(NSLOCTEXT("GuLiMap","UpdateDensityCells","批量修改资源密度格")); Actor->Modify(); Actor->Record=MoveTemp(Candidate);
    FPropertyChangedEvent Changed(FindFProperty<FProperty>(AGuLiMapDensityMap::StaticClass(),GET_MEMBER_NAME_CHECKED(AGuLiMapDensityMap,Record)),EPropertyChangeType::ValueSet);
    Actor->PostEditChangeProperty(Changed); Result.bSuccess=true; return Result;
}
