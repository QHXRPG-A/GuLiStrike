#include "GuLiMapAuthoringSubsystem.h"
#include "GuLiMapAuthoring.h"
#include "GuLiMapAuthoringSettings.h"
#include "GuLiMapDensityMap.h"
#include "GuLiMapMarker.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace GuLiStandalone
{
struct FCandidate
{
    FGuLiMapSnapshot Snapshot;
    TArray<TStrongObjectPtr<UGuLiMapTypeDefinition>> Types;
    TMap<FName,FString> AssetPaths;
    TMap<FGuid,bool> Follow;
};
bool Fail(FString& Error,const FString& Message) { Error=Message; return false; }
bool String(const FJsonObject& J,const TCHAR* Key,FString& Out,FString& Error)
{ return J.TryGetStringField(Key,Out)||Fail(Error,FString(TEXT("Missing/string expected: "))+Key); }
bool Array(const FJsonObject& J,const TCHAR* Key,const TArray<TSharedPtr<FJsonValue>>*& Out,FString& Error)
{ return J.TryGetArrayField(Key,Out)||Fail(Error,FString(TEXT("Missing/array expected: "))+Key); }
bool Object(const FJsonObject& J,const TCHAR* Key,const TSharedPtr<FJsonObject>*& Out,FString& Error)
{ return J.TryGetObjectField(Key,Out)||Fail(Error,FString(TEXT("Missing/object expected: "))+Key); }
bool Boolean(const FJsonObject& J,const TCHAR* Key,bool& Out,FString& Error)
{ return J.TryGetBoolField(Key,Out)||Fail(Error,FString(TEXT("Missing/boolean expected: "))+Key); }
bool Guid(const FJsonObject& J,const TCHAR* Key,FGuid& Out,FString& Error)
{ FString S; return String(J,Key,S,Error) && ((FGuid::Parse(S,Out)&&Out.IsValid())||Fail(Error,TEXT("Invalid GUID: ")+S)); }
bool Number(const FJsonObject& J,const TCHAR* Key,double& Out,FString& Error)
{ return (J.TryGetNumberField(Key,Out)&&FMath::IsFinite(Out))||Fail(Error,FString(TEXT("Finite number expected: "))+Key); }
bool Integer(const FJsonObject& J,const TCHAR* Key,int32& Out,FString& Error)
{
    double V=0; if (!Number(J,Key,V,Error)) return false;
    if (V<MIN_int32||V>MAX_int32||FMath::FloorToDouble(V)!=V) return Fail(Error,TEXT("Integer out of range"));
    Out=int32(V); return true;
}
bool JsonFile(const FString& Directory,const TCHAR* Name,TSharedPtr<FJsonObject>& Out,FString& Error)
{
    FString Text;
    if (!FFileHelper::LoadFileToString(Text,*(Directory/Name))||Text.Len()>128*1024*1024
        ||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Out)||!Out) return Fail(Error,TEXT("Invalid JSON file: ")+FString(Name));
    return true;
}
bool Transform(const FJsonObject& J,FTransform& Out,FString& Error)
{
    FVector P=FVector::ZeroVector,R=FVector::ZeroVector,S=FVector::OneVector;
    if (!GuLiMap::CheckKeys(J,{TEXT("position"),TEXT("rotation_pitch_yaw_roll"),TEXT("scale")},Error)
        ||!GuLiMap::ReadVector(J.TryGetField(TEXT("position")),P)||!GuLiMap::ReadVector(J.TryGetField(TEXT("rotation_pitch_yaw_roll")),R)
        ||!GuLiMap::ReadVector(J.TryGetField(TEXT("scale")),S)||S!=FVector::OneVector) return Fail(Error,TEXT("Invalid finite unit-scale transform"));
    Out=FTransform(FRotator(R.X,R.Y,R.Z),P,S);return true;
}
bool Region(const FJsonObject& J,FGuLiMapRegionRecord& Out,FString& Error)
{
    const TSharedPtr<FJsonObject>* Relative=nullptr;const TSharedPtr<FJsonObject>* Geometry=nullptr;FString Key,Shape;FTransform T;
    if (!GuLiMap::CheckKeys(J,{TEXT("region_id"),TEXT("region_key"),TEXT("display_name"),TEXT("enabled"),TEXT("relative_transform"),TEXT("world_transform"),TEXT("shape_type"),TEXT("geometry")},Error)
        ||!Guid(J,TEXT("region_id"),Out.RegionId,Error)||!String(J,TEXT("region_key"),Key,Error)||!String(J,TEXT("display_name"),Out.DisplayName,Error)
        ||!Boolean(J,TEXT("enabled"),Out.bEnabled,Error)||!Object(J,TEXT("relative_transform"),Relative,Error)||!Transform(**Relative,T,Error)
        ||!String(J,TEXT("shape_type"),Shape,Error)||!Object(J,TEXT("geometry"),Geometry,Error)) return false;
    auto Handler=GuLiMap::FindGeometry(FName(*Shape));
    if (!Handler||!Handler->FromJson(**Geometry,Out.Geometry,Error)||!Handler->Validate(Out.Geometry,Error)) return Fail(Error,TEXT("Invalid shape: ")+Shape+TEXT(" ")+Error);
    Out.RegionKey=FName(*Key);Out.Translation=T.GetLocation();Out.Rotation=T.Rotator();return true;
}
bool ReadType(const FJsonObject& J,UGuLiMapTypeDefinition& Out,FString& AssetPath,FString& Error)
{
    FString ID,Icon;FVector RGB=FVector::ZeroVector;double Alpha=0;const TArray<TSharedPtr<FJsonValue>>* Shapes=nullptr;const TArray<TSharedPtr<FJsonValue>>* Fields=nullptr;const TArray<TSharedPtr<FJsonValue>>* Defaults=nullptr;
    if (!GuLiMap::CheckKeys(J,{TEXT("type_id"),TEXT("asset_path"),TEXT("display_name"),TEXT("editor_icon"),TEXT("color_rgb"),TEXT("color_alpha"),TEXT("allowed_shapes"),TEXT("fields"),TEXT("default_regions")},Error)
        ||!String(J,TEXT("type_id"),ID,Error)||!String(J,TEXT("asset_path"),AssetPath,Error)||!String(J,TEXT("display_name"),Out.DisplayName,Error)
        ||!String(J,TEXT("editor_icon"),Icon,Error)||!GuLiMap::ReadVector(J.TryGetField(TEXT("color_rgb")),RGB)||!Number(J,TEXT("color_alpha"),Alpha,Error)
        ||!Array(J,TEXT("allowed_shapes"),Shapes,Error)||!Array(J,TEXT("fields"),Fields,Error)||!Array(J,TEXT("default_regions"),Defaults,Error)) return false;
    if (!GuLiMap::IsKey(ID)||Fields->Num()>1024||Defaults->Num()>4096||Alpha<0||Alpha>1||RGB.GetMin()<0) return Fail(Error,TEXT("Invalid type definition"));
    Out.TypeId=FName(*ID);Out.EditorIcon=FName(*Icon);Out.Color=FLinearColor(RGB.X,RGB.Y,RGB.Z,Alpha);Out.AllowedShapes.Reset();
    for (const auto& V:*Shapes) { FString S;if (!V||!V->TryGetString(S)||!GuLiMap::FindGeometry(FName(*S))) return Fail(Error,TEXT("Unknown allowed shape"));Out.AllowedShapes.Add(FName(*S)); }
    TArray<FPropertyBagPropertyDesc> Descs;TArray<const FJsonObject*> FieldObjects;TSet<FGuid> Ids;TSet<FName> Names;
    for (const auto& V:*Fields)
    {
        const auto F=V&&V->Type==EJson::Object?V->AsObject():nullptr;FString Name,Kind,TypeObject;FGuid FieldId;
        if (!F||!GuLiMap::CheckKeys(*F,{TEXT("field_id"),TEXT("field_key"),TEXT("display_name"),TEXT("field_type"),TEXT("value_type_object"),TEXT("default"),TEXT("required"),TEXT("minimum"),TEXT("maximum"),TEXT("enum_values")},Error)
            ||!Guid(*F,TEXT("field_id"),FieldId,Error)||!String(*F,TEXT("field_key"),Name,Error)||!String(*F,TEXT("field_type"),Kind,Error)) return false;
        F->TryGetStringField(TEXT("value_type_object"),TypeObject);
        if (Ids.Contains(FieldId)||Names.Contains(FName(*Name))||!GuLiMap::IsKey(Name)) return Fail(Error,TEXT("Duplicate/invalid field identity"));
        Ids.Add(FieldId);Names.Add(FName(*Name));
        EPropertyBagPropertyType ValueType=EPropertyBagPropertyType::None;const UObject* ObjectType=nullptr;
        if (Kind==TEXT("bool")) ValueType=EPropertyBagPropertyType::Bool;
        else if (Kind==TEXT("int32")) ValueType=EPropertyBagPropertyType::Int32;
        else if (Kind==TEXT("double")) ValueType=EPropertyBagPropertyType::Double;
        else if (Kind==TEXT("name")) ValueType=EPropertyBagPropertyType::Name;
        else if (Kind==TEXT("string")) ValueType=EPropertyBagPropertyType::String;
        else if (Kind==TEXT("vector")) { ValueType=EPropertyBagPropertyType::Struct;ObjectType=TBaseStructure<FVector>::Get(); }
        else if (Kind==TEXT("enum")) { ValueType=EPropertyBagPropertyType::Enum;ObjectType=LoadObject<UEnum>(nullptr,*TypeObject);if (!ObjectType)return Fail(Error,TEXT("Enum type cannot be resolved: ")+TypeObject); }
        else if (Kind==TEXT("soft_object")||Kind==TEXT("soft_class"))
        { ValueType=Kind==TEXT("soft_object")?EPropertyBagPropertyType::SoftObject:EPropertyBagPropertyType::SoftClass;ObjectType=TypeObject.IsEmpty()?UObject::StaticClass():LoadObject<UClass>(nullptr,*TypeObject);if(!ObjectType)return Fail(Error,TEXT("Object/class constraint cannot be resolved")); }
        else return Fail(Error,TEXT("Unsupported field type: ")+Kind);
        FPropertyBagPropertyDesc D(FName(*Name),ValueType,ObjectType);D.ID=FieldId;Descs.Add(D);FieldObjects.Add(F.Get());
    }
    Out.DefaultParameters.Reset();Out.DefaultParameters.AddProperties(Descs);Out.FieldRules.Reset();
    for (int32 I=0;I<Descs.Num();++I)
    {
        const auto& F=*FieldObjects[I];const auto* D=Out.DefaultParameters.FindPropertyDescByID(Descs[I].ID);
        if (!D||!GuLiMap::SetField(Out.DefaultParameters,*D,F.TryGetField(TEXT("default")),Error)) return false;
        if (F.HasField(TEXT("display_name"))||F.HasField(TEXT("required"))||F.HasField(TEXT("minimum"))||F.HasField(TEXT("maximum")))
        {
            auto& Rule=Out.FieldRules.AddDefaulted_GetRef();Rule.FieldId=D->ID;F.TryGetStringField(TEXT("display_name"),Rule.DisplayName);F.TryGetBoolField(TEXT("required"),Rule.bRequired);
            Rule.bUseMinimum=F.HasField(TEXT("minimum"));Rule.bUseMaximum=F.HasField(TEXT("maximum"));
            if (Rule.bUseMinimum&&!Number(F,TEXT("minimum"),Rule.Minimum,Error)) return false;
            if (Rule.bUseMaximum&&!Number(F,TEXT("maximum"),Rule.Maximum,Error)) return false;
        }
    }
    Out.DefaultRegions.Reset();for(const auto& V:*Defaults) { if (!V||V->Type!=EJson::Object)return Fail(Error,TEXT("Invalid default region"));auto& R=Out.DefaultRegions.AddDefaulted_GetRef();if(!Region(*V->AsObject(),R,Error))return false; }
    TArray<FGuLiMapIssue> Issues;GuLiMap::ValidateType(Out,Issues);if(GuLiMap::HasErrors(Issues))return Fail(Error,Issues[0].Message);return true;
}
bool ReadDensity(const FJsonObject& J,FGuLiMapDensityMapRecord& Out,FString& Error)
{
    int32 Version=0;FString TerritoryType,TerritoryKey;const TArray<TSharedPtr<FJsonValue>>* Layers=nullptr;
    if (!Integer(J,TEXT("schema_version"),Version,Error)||Version!=1||!Guid(J,TEXT("density_map_id"),Out.DensityMapId,Error)
        ||!Number(J,TEXT("cell_size_cm"),Out.CellSizeCm,Error)||Out.CellSizeCm<=0||!String(J,TEXT("territory_type_id"),TerritoryType,Error)
        ||!String(J,TEXT("territory_region_key"),TerritoryKey,Error)||!Array(J,TEXT("layers"),Layers,Error)||Layers->Num()>64)return false;
    Out.TerritoryTypeId=FName(*TerritoryType);Out.TerritoryRegionKey=FName(*TerritoryKey);Out.Layers.Reset();
    for (const auto& V:*Layers)
    {
        if (!V||V->Type!=EJson::Object)return Fail(Error,TEXT("Invalid density layer"));const auto L=V->AsObject();auto& Layer=Out.Layers.AddDefaulted_GetRef();FString Key;FVector RGB=FVector::ZeroVector;double Alpha=0;const TArray<TSharedPtr<FJsonValue>>* Cells=nullptr;
        if (!Guid(*L,TEXT("layer_id"),Layer.LayerId,Error)||!String(*L,TEXT("layer_key"),Key,Error)||!String(*L,TEXT("display_name"),Layer.DisplayName,Error)
            ||!GuLiMap::ReadVector(L->TryGetField(TEXT("color_rgb")),RGB)||!Number(*L,TEXT("color_alpha"),Alpha,Error)||!Array(*L,TEXT("cells"),Cells,Error)||Cells->Num()>4*1024*1024) return false;
        Layer.LayerKey=FName(*Key);Layer.Color=FLinearColor(RGB.X,RGB.Y,RGB.Z,Alpha);TSet<FIntPoint> Seen;
        for (const auto& Cell:*Cells)
        {
            if (!Cell||Cell->Type!=EJson::Object)return Fail(Error,TEXT("Invalid density cell"));const auto C=Cell->AsObject();int32 X=0,Y=0,N=0;
            if (!GuLiMap::CheckKeys(*C,{TEXT("cell_x"),TEXT("cell_y"),TEXT("density_u8")},Error)||!Integer(*C,TEXT("cell_x"),X,Error)||!Integer(*C,TEXT("cell_y"),Y,Error)
                ||!Integer(*C,TEXT("density_u8"),N,Error)||N<0||N>255||Seen.Contains(FIntPoint(X,Y)))return Fail(Error,TEXT("Invalid/duplicate density cell"));
            Seen.Add(FIntPoint(X,Y));if(N&&!GuLiMap::SetDensityCell(Layer,FIntPoint(X,Y),uint8(N)))return Fail(Error,TEXT("Could not set density cell"));
        }
    }
    GuLiMap::NormalizeDensityMap(Out);return true;
}
bool Candidate(UGuLiMapAuthoringSubsystem& Service,const FString& Directory,FCandidate& Out,FString& Error)
{
    UWorld* World=Service.EditorWorld();if(!World)return Fail(Error,TEXT("Open an editor world outside PIE/SIE"));
    TSharedPtr<FJsonObject> Manifest,Layout,Catalog;
    if (!JsonFile(Directory,TEXT("manifest.json"),Manifest,Error)||!JsonFile(Directory,TEXT("layout.json"),Layout,Error)||!JsonFile(Directory,TEXT("type_catalog.json"),Catalog,Error))return false;
    FString Schema,Map;int32 Version=0;
    if (!String(*Manifest,TEXT("schema"),Schema,Error)||Schema!=TEXT("guli-map-design/export-v1")||!String(*Layout,TEXT("map_package"),Map,Error)
        ||Map!=World->GetOutermost()->GetName()||!Integer(*Layout,TEXT("schema_version"),Version,Error)||Version!=1)
        return Fail(Error,TEXT("Unsupported export or target map does not match the current editor world"));
    Out.Snapshot.MapPackage=Map;
    const TArray<TSharedPtr<FJsonValue>>* Types=nullptr;const TArray<TSharedPtr<FJsonValue>>* Markers=nullptr;
    if (!Array(*Catalog,TEXT("types"),Types,Error)||Types->Num()>1024||!Array(*Layout,TEXT("markers"),Markers,Error)||Markers->Num()>100000)return false;
    TMap<FName,UGuLiMapTypeDefinition*> ById;
    const FString AllowedRoot=GetDefault<UGuLiMapAuthoringSettings>()->TypeAssetPath+TEXT("/");
    TSet<FString> Paths;
    for(const auto& V:*Types)
    {
        if(!V||V->Type!=EJson::Object)return Fail(Error,TEXT("Type object expected"));
        TStrongObjectPtr<UGuLiMapTypeDefinition> Type(NewObject<UGuLiMapTypeDefinition>());FString Path;
        if(!ReadType(*V->AsObject(),*Type,Path,Error))return false;
        const FString Package=FPackageName::ObjectPathToPackageName(Path);
        if(!FPackageName::IsValidLongPackageName(Package)||!Package.StartsWith(AllowedRoot)||FPackageName::ObjectPathToObjectName(Path).IsEmpty()
            ||Paths.Contains(Path.ToLower())||ById.Contains(Type->TypeId))return Fail(Error,TEXT("Invalid/duplicate configured type path or TypeId"));
        UObject* Existing=LoadObject<UObject>(nullptr,*Path);
        if(Existing&&!Cast<UGuLiMapTypeDefinition>(Existing))return Fail(Error,TEXT("Type path references an unrelated asset: ")+Path);
        Paths.Add(Path.ToLower());Out.AssetPaths.Add(Type->TypeId,Path);ById.Add(Type->TypeId,Type.Get());Out.Types.Add(MoveTemp(Type));
    }
    for(const auto& V:*Markers)
    {
        if(!V||V->Type!=EJson::Object)return Fail(Error,TEXT("Marker object expected"));const auto M=V->AsObject();auto& Entry=Out.Snapshot.Markers.AddDefaulted_GetRef();auto& R=Entry.Record;
        FString Key,TypeId;const TSharedPtr<FJsonObject>* T=nullptr;const TSharedPtr<FJsonObject>* Params=nullptr;const TArray<TSharedPtr<FJsonValue>>* Tags=nullptr;const TArray<TSharedPtr<FJsonValue>>* Regions=nullptr;
        if(!GuLiMap::CheckKeys(*M,{TEXT("marker_id"),TEXT("marker_key"),TEXT("type_id"),TEXT("display_name"),TEXT("enabled"),TEXT("note"),TEXT("tags"),TEXT("parameters"),TEXT("regions"),TEXT("world_transform")},Error)
            ||!Guid(*M,TEXT("marker_id"),R.MarkerId,Error)||!String(*M,TEXT("marker_key"),Key,Error)||!String(*M,TEXT("type_id"),TypeId,Error)
            ||!String(*M,TEXT("display_name"),R.DisplayName,Error)||!String(*M,TEXT("note"),R.Note,Error)||!Boolean(*M,TEXT("enabled"),R.bEnabled,Error)
            ||!Object(*M,TEXT("world_transform"),T,Error)||!Transform(**T,Entry.WorldTransform,Error)||!Object(*M,TEXT("parameters"),Params,Error)
            ||!Array(*M,TEXT("tags"),Tags,Error)||!Array(*M,TEXT("regions"),Regions,Error))return false;
        auto* Type=ById.FindRef(FName(*TypeId));if(!Type)return Fail(Error,TEXT("Marker references an unknown type: ")+TypeId);
        R.MarkerKey=FName(*Key);R.Type=Type;R.Parameters=Type->DefaultParameters;
        const UPropertyBag* Bag=R.Parameters.GetPropertyBagStruct();const int32 FieldCount=Bag?Bag->GetPropertyDescs().Num():0;
        if((*Params)->Values.Num()!=FieldCount)return Fail(Error,TEXT("Marker parameter keys do not match type schema"));
        if(Bag)for(const auto& Desc:Bag->GetPropertyDescs())if(!GuLiMap::SetField(R.Parameters,Desc,(*Params)->TryGetField(Desc.Name.ToString()),Error))return false;
        for(const auto& Tag:*Tags){FString S;if(!Tag||!Tag->TryGetString(S))return Fail(Error,TEXT("String tag expected"));R.Tags.Add(FName(*S));}
        for(const auto& RegionValue:*Regions){if(!RegionValue||RegionValue->Type!=EJson::Object)return Fail(Error,TEXT("Region object expected"));auto& RegionRecord=R.Regions.AddDefaulted_GetRef();if(!Region(*RegionValue->AsObject(),RegionRecord,Error))return false;}
    }
    const FString DensityFile=Directory/TEXT("density_layers.json");
    if(FPaths::FileExists(DensityFile)){TSharedPtr<FJsonObject> Density;if(!JsonFile(Directory,TEXT("density_layers.json"),Density,Error))return false;FGuLiMapDensityMapRecord Record;if(!ReadDensity(*Density,Record,Error))return false;Out.Snapshot.DensityMap=MoveTemp(Record);}
    if(FPaths::FileExists(Directory/TEXT("editor.json")))
    {
        TSharedPtr<FJsonObject> Settings;const TSharedPtr<FJsonObject>* Follow=nullptr;
        if(!JsonFile(Directory,TEXT("editor.json"),Settings,Error))return false;
        if(Settings->TryGetObjectField(TEXT("ground_follow"),Follow))for(const auto& Pair:(*Follow)->Values)
        {FGuid ID;bool B=false;if(!FGuid::Parse(Pair.Key,ID)||!Pair.Value->TryGetBool(B))return Fail(Error,TEXT("Invalid follow-ground metadata"));Out.Follow.Add(ID,B);}
    }
    return true;
}
bool Baseline(UGuLiMapAuthoringSubsystem& Service,const FString& Directory,FString& Error)
{
    // Compare the exact canonical source snapshots, including their type schemas.
    // The project adapter additionally verifies SHA-256 file/terrain signatures.
    const auto Layout=Service.GetSnapshot(),Density=Service.GetDensitySnapshot(),Types=Service.GetStandaloneTypeCatalog();
    if(!Layout.bSuccess||!Density.bSuccess||!Types.bSuccess)return Fail(Error,TEXT("Could not read complete current authoring state"));
    for(const auto& Pair:TArray<TPair<FString,FString>>{{TEXT("baseline_layout.json"),Layout.Json},{TEXT("baseline_density_layers.json"),Density.Json},{TEXT("baseline_type_catalog.json"),Types.Json}})
    {
        FString Expected;if(!FFileHelper::LoadFileToString(Expected,*(Directory/Pair.Key))||Expected!=Pair.Value)
            return Fail(Error,TEXT("UE source changed or no baseline is attached: ")+Pair.Key+TEXT(". Reconcile the design with a fresh snapshot before applying."));
    }
    return true;
}
void CopyType(UGuLiMapTypeDefinition& Target,const UGuLiMapTypeDefinition& Source)
{
    Target.TypeId=Source.TypeId;Target.DisplayName=Source.DisplayName;Target.Color=Source.Color;Target.EditorIcon=Source.EditorIcon;
    Target.AllowedShapes=Source.AllowedShapes;Target.DefaultRegions=Source.DefaultRegions;Target.DefaultParameters=Source.DefaultParameters;Target.FieldRules=Source.FieldRules;
}
bool Prepare(UGuLiMapAuthoringSubsystem& Service,const FString& Directory,FCandidate& Parsed,FGuLiMapResult& R)
{
    FString Error;
    if(!Baseline(Service,Directory,Error)||!Candidate(Service,Directory,Parsed,Error)){R.Issues.Emplace(Error);return false;}
    GuLiMap::Validate(Parsed.Snapshot,R.Issues);if(GuLiMap::HasErrors(R.Issues))return false;
    FGuLiMapSnapshot Current;if(!Service.CollectSnapshot(Current,R.Issues,false))return false;
    const auto Actors=Service.LoadedMarkers();
    if(Actors.Num()!=Current.Markers.Num()){R.Issues.Emplace(TEXT("Actor set is incomplete after snapshot; refusing a partial import"));return false;}
    TSet<FGuid> Old,Next;for(const auto& E:Current.Markers)Old.Add(E.Record.MarkerId);for(const auto& E:Parsed.Snapshot.Markers)Next.Add(E.Record.MarkerId);
    int32 Create=0,Update=0,Remove=0;for(FGuid ID:Next)Old.Contains(ID)?++Update:++Create;for(FGuid ID:Old)if(!Next.Contains(ID))++Remove;
    auto Report=MakeShared<FJsonObject>();Report->SetNumberField(TEXT("create_markers"),Create);Report->SetNumberField(TEXT("update_markers"),Update);Report->SetNumberField(TEXT("delete_markers"),Remove);Report->SetNumberField(TEXT("configured_types"),Parsed.Types.Num());
    R.Json=GuLiMap::CanonicalJson(MakeShared<FJsonValueObject>(Report))+TEXT("\n");R.bSuccess=true;return true;
}
}

FGuLiMapResult UGuLiMapAuthoringSubsystem::GetStandaloneTypeCatalog()
{
    FGuLiMapResult R;auto Root=MakeShared<FJsonObject>();Root->SetNumberField(TEXT("schema_version"),1);TArray<TSharedPtr<FJsonValue>> Types;
    for(const auto* Type:ListTypes())
    {
        GuLiMap::ValidateType(*Type,R.Issues);if(GuLiMap::HasErrors(R.Issues))return R;
        auto Json=GuLiMap::TypeJson(*Type);const TArray<TSharedPtr<FJsonValue>>* Fields=nullptr;
        if(Json->TryGetArrayField(TEXT("fields"),Fields))for(const auto& F:*Fields)
        {
            const auto Object=F->AsObject();FString Key;if(!Object->TryGetStringField(TEXT("field_key"),Key))continue;
            const auto* Desc=Type->DefaultParameters.FindPropertyDescByName(FName(*Key));const auto* Enum=Desc?Cast<UEnum>(Desc->ValueTypeObject):nullptr;
            if(!Enum)continue;TArray<TSharedPtr<FJsonValue>> Choices;
            for(int32 I=0;I<Enum->NumEnums();++I)if(!Enum->HasMetaData(TEXT("Hidden"),I)&&Enum->GetValueByIndex(I)>=0&&Enum->GetValueByIndex(I)<=255)Choices.Add(MakeShared<FJsonValueString>(Enum->GetNameStringByIndex(I)));
            Object->SetArrayField(TEXT("enum_values"),Choices);
        }
        Types.Add(MakeShared<FJsonValueObject>(Json));
    }
    Root->SetArrayField(TEXT("types"),Types);R.Json=GuLiMap::CanonicalJson(MakeShared<FJsonValueObject>(Root))+TEXT("\n");R.bSuccess=true;return R;
}
FGuLiMapResult UGuLiMapAuthoringSubsystem::PreviewStandaloneSnapshot(const FString& Directory)
{
    FGuLiMapResult R;GuLiStandalone::FCandidate Parsed;GuLiStandalone::Prepare(*this,Directory,Parsed,R);return R;
}
FGuLiMapResult UGuLiMapAuthoringSubsystem::ApplyStandaloneSnapshot(const FString& Directory)
{
    FGuLiMapResult R;GuLiStandalone::FCandidate Parsed;if(!GuLiStandalone::Prepare(*this,Directory,Parsed,R))return R;
    UWorld* World=EditorWorld();if(!World){R.bSuccess=false;return R;}
    FScopedTransaction Transaction(NSLOCTEXT("GuLiMap","StandaloneImport","Import standalone map authoring snapshot"));
    TMap<FGuid,AGuLiMapMarker*> Existing;for(auto* A:LoadedMarkers())Existing.Add(A->Record.MarkerId,A);
    TArray<AGuLiMapMarker*> Created;TArray<UGuLiMapTypeDefinition*> NewTypes;TMap<FName,UGuLiMapTypeDefinition*> Targets;
    // Stage missing actors before changing existing records or schemas.
    FActorSpawnParameters Params;Params.OverrideLevel=World->PersistentLevel;Params.ObjectFlags|=RF_Transactional;
    for(const auto& E:Parsed.Snapshot.Markers)if(!Existing.Contains(E.Record.MarkerId))
    {
        auto* A=World->SpawnActor<AGuLiMapMarker>(AGuLiMapMarker::StaticClass(),E.WorldTransform,Params);
        if(!A){for(auto* C:Created)World->EditorDestroyActor(C,true);Transaction.Cancel();R.bSuccess=false;R.Issues.Emplace(TEXT("Could not stage new marker actors; existing data unchanged"));return R;}
        Created.Add(A);Existing.Add(E.Record.MarkerId,A);
    }
    auto DensityActors=LoadedDensityMaps();AGuLiMapDensityMap* Density=DensityActors.IsEmpty()?nullptr:DensityActors[0];
    if(Parsed.Snapshot.DensityMap.IsSet()&&!Density)
    {
        Density=World->SpawnActor<AGuLiMapDensityMap>(AGuLiMapDensityMap::StaticClass(),FTransform::Identity,Params);
        if(!Density){for(auto* C:Created)World->EditorDestroyActor(C,true);Transaction.Cancel();R.bSuccess=false;R.Issues.Emplace(TEXT("Could not stage density actor; existing data unchanged"));return R;}
    }
    for(const auto& Type:Parsed.Types)
    {
        const FString& Path=Parsed.AssetPaths.FindChecked(Type->TypeId);auto* Target=LoadObject<UGuLiMapTypeDefinition>(nullptr,*Path);
        if(!Target){const FString Package=FPackageName::ObjectPathToPackageName(Path);Target=NewObject<UGuLiMapTypeDefinition>(CreatePackage(*Package),*FPackageName::ObjectPathToObjectName(Path),RF_Public|RF_Standalone|RF_Transactional);NewTypes.Add(Target);}
        auto Before=GuLiMap::TypeJson(*Target),After=GuLiMap::TypeJson(*Type);After->SetStringField(TEXT("asset_path"),Path);
        if(GuLiMap::CanonicalJson(MakeShared<FJsonValueObject>(Before))!=GuLiMap::CanonicalJson(MakeShared<FJsonValueObject>(After)))
        {Target->Modify();GuLiStandalone::CopyType(*Target,*Type);Target->MarkPackageDirty();R.Files.AddUnique(Target->GetOutermost()->GetName());}
        Targets.Add(Type->TypeId,Target);
    }
    TSet<FGuid> Retained;
    for(const auto& E:Parsed.Snapshot.Markers)
    {
        auto* A=Existing.FindChecked(E.Record.MarkerId);A->Modify();A->Record=E.Record;A->Record.Type=Targets.FindChecked(E.Record.Type.LoadSynchronous()->TypeId);
        A->bStandaloneFollowTerrain=Parsed.Follow.FindRef(E.Record.MarkerId)||!Parsed.Follow.Contains(E.Record.MarkerId);
        A->SetActorTransform(E.WorldTransform,false,nullptr,ETeleportType::TeleportPhysics);A->SetActorLabel(E.Record.DisplayName.IsEmpty()?E.Record.MarkerKey.ToString():E.Record.DisplayName);
        A->SetFolderPath(TEXT("GuLi/MapAuthoring"));A->RefreshVisuals();A->MarkPackageDirty();Retained.Add(E.Record.MarkerId);
    }
    if(Density&&Parsed.Snapshot.DensityMap.IsSet()){Density->Modify();Density->Record=Parsed.Snapshot.DensityMap.GetValue();Density->RefreshVisuals(true);Density->MarkPackageDirty();}
    for(const auto& Pair:Existing)if(!Retained.Contains(Pair.Key))World->EditorDestroyActor(Pair.Value,true);
    if(Density&&!Parsed.Snapshot.DensityMap.IsSet())World->EditorDestroyActor(Density,true);
    for(auto* Type:NewTypes)FAssetRegistryModule::AssetCreated(Type);
    World->MarkPackageDirty();R.bSuccess=true;return R;
}
