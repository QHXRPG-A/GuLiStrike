#include "GuLiMapAuthoring.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace
{
TMap<FName,TSharedPtr<IGuLiMapGeometryHandler>> Geometries;
TMap<FName,GuLiMap::FTypeValidator> Validators;
TMap<FName,TSharedPtr<IGuLiMapExportProvider>> Exporters;
TSharedRef<FJsonValue> Str(const FString& S) { return MakeShared<FJsonValueString>(S); }
TSharedRef<FJsonValue> Obj(TSharedRef<FJsonObject> O) { return MakeShared<FJsonValueObject>(O); }
FString Quote(const FString& S)
{
    FString Out=TEXT("\"");
    for (TCHAR C:S)
    {
        if (C=='"') Out+=TEXT("\\\"");
        else if (C=='\\') Out+=TEXT("\\\\");
        else if (C<32) Out+=FString::Printf(TEXT("\\u%04x"),static_cast<uint32>(C));
        else Out.AppendChar(C);
    }
    return Out+TEXT("\"");
}
void Row(FString& Out,const TArray<FString>& Cells)
{
    for (int32 I=0;I<Cells.Num();++I)
    {
        if (I) Out+=TEXT(",");
        FString S=Cells[I]; S.ReplaceInline(TEXT("\r\n"),TEXT("\n")); S.ReplaceInline(TEXT("\r"),TEXT("\n"));
        if (S.Contains(TEXT(","))||S.Contains(TEXT("\""))||S.Contains(TEXT("\n"))) { S.ReplaceInline(TEXT("\""),TEXT("\"\"")); S=TEXT("\"")+S+TEXT("\""); }
        Out+=S;
    }
    Out+=TEXT("\n");
}
TArray<FString> TransformCells(const FTransform& T)
{
    const FVector P=T.GetLocation(); const FRotator R=T.Rotator();
    return {GuLiMap::Number(P.X),GuLiMap::Number(P.Y),GuLiMap::Number(P.Z),GuLiMap::Number(R.Pitch),GuLiMap::Number(R.Yaw),GuLiMap::Number(R.Roll)};
}
TSharedRef<FJsonObject> TransformJson(const FTransform& T)
{
    auto J=MakeShared<FJsonObject>(); const FRotator R=T.Rotator();
    J->SetField(TEXT("position"),GuLiMap::VectorJson(T.GetLocation()));
    J->SetField(TEXT("rotation_pitch_yaw_roll"),GuLiMap::VectorJson(FVector(R.Pitch,R.Yaw,R.Roll)));
    J->SetField(TEXT("scale"),GuLiMap::VectorJson(FVector::OneVector)); return J;
}
TSharedRef<FJsonObject> RegionJson(const FGuLiMapRegionRecord& R, const FTransform& World)
{
    auto J=MakeShared<FJsonObject>();
    J->SetStringField(TEXT("region_id"),GuLiMap::Guid(R.RegionId)); J->SetStringField(TEXT("region_key"),R.RegionKey.ToString());
    J->SetStringField(TEXT("display_name"),R.DisplayName); J->SetBoolField(TEXT("enabled"),R.bEnabled);
    J->SetObjectField(TEXT("relative_transform"),TransformJson(R.GetTransform()));
    J->SetObjectField(TEXT("world_transform"),TransformJson(R.GetTransform()*World));
    if (auto H=GuLiMap::FindGeometry(R.Geometry)) { J->SetStringField(TEXT("shape_type"),H->GetShapeType().ToString()); J->SetObjectField(TEXT("geometry"),H->ToJson(R.Geometry)); }
    return J;
}
TArray<FPropertyBagPropertyDesc> Fields(const FInstancedPropertyBag& Bag)
{
    TArray<FPropertyBagPropertyDesc> Out;
    if (Bag.GetPropertyBagStruct()) Out=Bag.GetPropertyBagStruct()->GetPropertyDescs();
    Out.Sort([](const auto& A,const auto& B){return A.Name.LexicalLess(B.Name);}); return Out;
}
bool JsonFinite(const TSharedPtr<FJsonValue>& V)
{
    if (!V) return false;
    if (V->Type==EJson::Number) return FMath::IsFinite(V->AsNumber());
    if (V->Type==EJson::Array) for (const auto& A:V->AsArray()) { if (!JsonFinite(A)) return false; }
    if (V->Type==EJson::Object) for (const auto& P:V->AsObject()->Values) { if (!JsonFinite(P.Value)) return false; }
    return true;
}
}
namespace GuLiMap
{
bool RegisterGeometry(TSharedRef<IGuLiMapGeometryHandler> H)
{
    if (Geometries.Contains(H->GetShapeType()) || !H->GetStruct()) return false;
    for (const auto& P:Geometries) if (P.Value->GetStruct()==H->GetStruct()) return false;
    Geometries.Add(H->GetShapeType(),H); return true;
}
void UnregisterGeometry(FName N) { Geometries.Remove(N); }
TSharedPtr<IGuLiMapGeometryHandler> FindGeometry(FName N) { return Geometries.FindRef(N); }
TSharedPtr<IGuLiMapGeometryHandler> FindGeometry(const FInstancedStruct& S)
{
    for (const auto& P:Geometries) if (P.Value->GetStruct()==S.GetScriptStruct()) return P.Value;
    return {};
}
TArray<FName> ListShapes() { TArray<FName> Names; Geometries.GetKeys(Names); Names.Sort(FNameLexicalLess()); return Names; }
bool RegisterValidator(FName N,FTypeValidator V) { if (Validators.Contains(N)) return false; Validators.Add(N,MoveTemp(V)); return true; }
void UnregisterValidator(FName N) { Validators.Remove(N); }
bool RegisterExporter(FName N,TSharedRef<IGuLiMapExportProvider> P) { if (Exporters.Contains(N)) return false; Exporters.Add(N,P); return true; }
void UnregisterExporter(FName N) { Exporters.Remove(N); }
void ShutdownRegistries() { Exporters.Empty(); Validators.Empty(); Geometries.Empty(); }
bool IsKey(const FString& V)
{
    auto Letter=[](TCHAR C) { return C=='_' || C>='a'&&C<='z' || C>='A'&&C<='Z'; };
    if (V.IsEmpty() || !Letter(V[0])) return false;
    for (TCHAR C:V) if (!Letter(C) && !(C>='0'&&C<='9')) return false;
    return true;
}
FString Guid(const FGuid& V) { return V.ToString(EGuidFormats::DigitsWithHyphensLower); }
FString Number(double V) { return V==0?TEXT("0"):FString::Printf(TEXT("%.17g"),V); }
FString CanonicalJson(const TSharedPtr<FJsonValue>& V)
{
    if (!V || V->Type==EJson::Null) return TEXT("null");
    if (V->Type==EJson::Boolean) return V->AsBool()?TEXT("true"):TEXT("false");
    if (V->Type==EJson::Number) return Number(V->AsNumber());
    if (V->Type==EJson::String) return Quote(V->AsString());
    TArray<FString> Pieces;
    if (V->Type==EJson::Array)
    { for (const auto& A:V->AsArray()) Pieces.Add(CanonicalJson(A)); return TEXT("[")+FString::Join(Pieces,TEXT(","))+TEXT("]"); }
    TArray<FString> Keys; V->AsObject()->Values.GetKeys(Keys); Keys.Sort();
    for (const auto& K:Keys) Pieces.Add(Quote(K)+TEXT(":")+CanonicalJson(V->AsObject()->Values[K]));
    return TEXT("{")+FString::Join(Pieces,TEXT(","))+TEXT("}");
}
TSharedRef<FJsonValue> VectorJson(const FVector& V)
{
    return MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)});
}
bool ReadVector(const TSharedPtr<FJsonValue>& V,FVector& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* A=nullptr;
    return V && V->TryGetArray(A) && A->Num()==3 && (*A)[0]->Type==EJson::Number && (*A)[1]->Type==EJson::Number && (*A)[2]->Type==EJson::Number &&
        (*A)[0]->TryGetNumber(Out.X) && (*A)[1]->TryGetNumber(Out.Y) && (*A)[2]->TryGetNumber(Out.Z) && !Out.ContainsNaN();
}
bool CheckKeys(const FJsonObject& O,const TArray<FString>& Allowed,FString& Error)
{
    for (const auto& Pair:O.Values) if (!Allowed.Contains(Pair.Key)) { Error=TEXT("Unknown key: ")+Pair.Key; return false; }
    return true;
}
void Validate(const FGuLiMapSnapshot& S,TArray<FGuLiMapIssue>& Issues)
{
    TSet<FGuid> Ids, RegionIds; TSet<FName> Keys; TMap<FName,const UGuLiMapTypeDefinition*> Types;
    TArray<FName> ValidatorNames; Validators.GetKeys(ValidatorNames); ValidatorNames.Sort(FNameLexicalLess());
    for (const auto& E:S.Markers)
    {
        const auto& R=E.Record;
        auto Fail=[&](const FString& Msg,const FString& Field=TEXT(""),FGuid Region=FGuid()){Issues.Emplace(Msg,R.MarkerId,Region,Field);};
        if (!R.MarkerId.IsValid()||Ids.Contains(R.MarkerId)) Fail(TEXT("Invalid/duplicate MarkerId."));
        if (R.MarkerKey.IsNone()||!IsKey(R.MarkerKey.ToString())||Keys.Contains(R.MarkerKey)) Fail(TEXT("Invalid/duplicate MarkerKey."));
        Ids.Add(R.MarkerId); Keys.Add(R.MarkerKey);
        if (!E.WorldTransform.IsValid()||!E.WorldTransform.GetScale3D().Equals(FVector::OneVector)) Fail(TEXT("Transform must be finite with scale 1."));
        const auto* T=R.Type.LoadSynchronous();
        if (!T) { Fail(TEXT("Missing type definition.")); continue; }
        if (const auto* Existing=Types.Find(T->TypeId); Existing && *Existing!=T) Fail(TEXT("Duplicate TypeId across different assets."));
        if (!Types.Contains(T->TypeId)) { ValidateType(*T,Issues); Types.Add(T->TypeId,T); }
        FInstancedPropertyBag Copy=R.Parameters; FString Error;
        if (!MigrateFields(Copy,T->DefaultParameters,false,Error)) Fail(Error,TEXT("parameters"));
        else if (!R.Parameters.HasSameLayout(T->DefaultParameters)) Fail(TEXT("Type schema changed; synchronize type definition first."),TEXT("parameters"));
        for (const auto& D:Fields(R.Parameters))
        {
            auto V=FieldValue(R.Parameters,D);
            if (!V) { Fail(TEXT("Unsupported/non-finite field value."),D.Name.ToString()); continue; }
            for (const auto& Rule:T->FieldRules) if (Rule.FieldId==D.ID)
            {
                if (Rule.bRequired && (V->Type==EJson::String && (V->AsString().IsEmpty() || V->AsString()==TEXT("None")))) Fail(TEXT("Required field is empty."),D.Name.ToString());
                if (V->Type==EJson::Number && (Rule.bUseMinimum&&V->AsNumber()<Rule.Minimum || Rule.bUseMaximum&&V->AsNumber()>Rule.Maximum)) Fail(TEXT("Value outside allowed range."),D.Name.ToString());
            }
        }
        TSet<FName> RegionKeys,Tags;
        for (FName Tag:R.Tags) { if (Tag.IsNone()||Tags.Contains(Tag)) Fail(TEXT("Empty/duplicate tag."),TEXT("tags")); Tags.Add(Tag); }
        for (const auto& A:R.Regions)
        {
            if (!A.RegionId.IsValid()||RegionIds.Contains(A.RegionId)) Fail(TEXT("Invalid/duplicate RegionId."),TEXT("region_id"),A.RegionId);
            RegionIds.Add(A.RegionId);
            if (A.RegionKey.IsNone()||!IsKey(A.RegionKey.ToString())||RegionKeys.Contains(A.RegionKey)) Fail(TEXT("Invalid/duplicate RegionKey."),TEXT("region_key"),A.RegionId);
            RegionKeys.Add(A.RegionKey);
            if (A.Translation.ContainsNaN()||A.Rotation.ContainsNaN()) Fail(TEXT("Non-finite region transform."),TEXT("transform"),A.RegionId);
            auto H=FindGeometry(A.Geometry);
            if (!H) Fail(TEXT("Unregistered shape."),TEXT("geometry"),A.RegionId);
            else if (!T->AllowedShapes.Contains(H->GetShapeType())) Fail(TEXT("Shape is not allowed by type."),TEXT("geometry"),A.RegionId);
            else if (!H->Validate(A.Geometry,Error)) Fail(Error,TEXT("geometry"),A.RegionId);
        }
        for (FName N:ValidatorNames) Validators[N](E,Issues);
    }
}
bool BuildFiles(const FGuLiMapSnapshot& Input,TMap<FString,FString>& Out,TArray<FGuLiMapIssue>& Issues)
{
    Validate(Input,Issues); if (!Issues.IsEmpty()) return false;
    FGuLiMapSnapshot S=Input;
    S.Markers.Sort([](const auto& A,const auto& B){return A.Record.MarkerKey.LexicalLess(B.Record.MarkerKey);});
    TMap<FString,FString> Files;
    Files.Add(TEXT("markers.csv")); Files.Add(TEXT("regions.csv")); Files.Add(TEXT("vertices.csv"));
    Files.Add(TEXT("properties.csv")); Files.Add(TEXT("tags.csv"));
    // Add every key before retaining references (TMap growth can invalidate value references).
    Row(Files[TEXT("markers.csv")],{TEXT("marker_id"),TEXT("marker_key"),TEXT("type_id"),TEXT("display_name"),TEXT("enabled"),TEXT("note"),TEXT("world_x_cm"),TEXT("world_y_cm"),TEXT("world_z_cm"),TEXT("pitch_deg"),TEXT("yaw_deg"),TEXT("roll_deg")});
    Row(Files[TEXT("regions.csv")],{TEXT("marker_id"),TEXT("region_id"),TEXT("region_key"),TEXT("display_name"),TEXT("enabled"),TEXT("shape_type"),TEXT("local_x_cm"),TEXT("local_y_cm"),TEXT("local_z_cm"),TEXT("local_pitch_deg"),TEXT("local_yaw_deg"),TEXT("local_roll_deg"),TEXT("world_x_cm"),TEXT("world_y_cm"),TEXT("world_z_cm"),TEXT("world_pitch_deg"),TEXT("world_yaw_deg"),TEXT("world_roll_deg"),TEXT("radius_cm"),TEXT("half_x_cm"),TEXT("half_y_cm"),TEXT("half_z_cm"),TEXT("min_z_cm"),TEXT("max_z_cm")});
    Row(Files[TEXT("vertices.csv")],{TEXT("marker_id"),TEXT("region_id"),TEXT("vertex_index"),TEXT("local_x_cm"),TEXT("local_y_cm"),TEXT("world_x_cm"),TEXT("world_y_cm"),TEXT("world_z_cm")});
    Row(Files[TEXT("properties.csv")],{TEXT("marker_id"),TEXT("region_id"),TEXT("field_id"),TEXT("field_key"),TEXT("field_type"),TEXT("value"),TEXT("x"),TEXT("y"),TEXT("z")});
    Row(Files[TEXT("tags.csv")],{TEXT("marker_id"),TEXT("tag")});
    auto Root=MakeShared<FJsonObject>(); Root->SetNumberField(TEXT("schema_version"),1); Root->SetStringField(TEXT("map_package"),S.MapPackage);
    Root->SetStringField(TEXT("length_unit"),TEXT("cm")); Root->SetStringField(TEXT("angle_unit"),TEXT("degree"));
    Root->SetStringField(TEXT("coordinate_system"),TEXT("Unreal left-handed Z-up; local polygon XY at z=0; implicit closure"));
    TArray<TSharedPtr<FJsonValue>> Markers,TypeArray; TMap<FString,const UGuLiMapTypeDefinition*> Types;
    for (auto& E:S.Markers)
    {
        auto& R=E.Record; const auto* T=R.Type.LoadSynchronous(); const FString ID=Guid(R.MarkerId);
        Types.Add(T->TypeId.ToString(),T);
        auto J=MakeShared<FJsonObject>(); J->SetStringField(TEXT("marker_id"),ID); J->SetStringField(TEXT("marker_key"),R.MarkerKey.ToString());
        J->SetStringField(TEXT("type_id"),T->TypeId.ToString()); J->SetStringField(TEXT("display_name"),R.DisplayName);
        J->SetBoolField(TEXT("enabled"),R.bEnabled); J->SetStringField(TEXT("note"),R.Note); J->SetObjectField(TEXT("world_transform"),TransformJson(E.WorldTransform));
        TArray<FString> Cells={ID,R.MarkerKey.ToString(),T->TypeId.ToString(),R.DisplayName,R.bEnabled?TEXT("true"):TEXT("false"),R.Note}; Cells.Append(TransformCells(E.WorldTransform)); Row(Files[TEXT("markers.csv")],Cells);
        auto Params=MakeShared<FJsonObject>();
        for (const auto& D:Fields(R.Parameters))
        {
            auto V=FieldValue(R.Parameters,D); Params->SetField(D.Name.ToString(),V);
            TArray<FString> P={ID,TEXT(""),Guid(D.ID),D.Name.ToString(),FieldType(D),TEXT(""),TEXT(""),TEXT(""),TEXT("")};
            if (FieldType(D)==TEXT("vector")) { const auto& A=V->AsArray(); for (int I=0;I<3;++I) P[6+I]=Number(A[I]->AsNumber()); }
            else P[5]=V->Type==EJson::String?V->AsString():CanonicalJson(V);
            Row(Files[TEXT("properties.csv")],P);
        }
        J->SetObjectField(TEXT("parameters"),Params);
        R.Tags.Sort(FNameLexicalLess()); TArray<TSharedPtr<FJsonValue>> Tags;
        for (FName Tag:R.Tags) { Tags.Add(Str(Tag.ToString())); Row(Files[TEXT("tags.csv")],{ID,Tag.ToString()}); }
        J->SetArrayField(TEXT("tags"),Tags);
        R.Regions.Sort([](const auto& A,const auto& B){return A.RegionKey.LexicalLess(B.RegionKey);});
        TArray<TSharedPtr<FJsonValue>> Regions;
        for (const auto& A:R.Regions)
        {
            const auto H=FindGeometry(A.Geometry); auto G=H->ToJson(A.Geometry); const FString RID=Guid(A.RegionId); const FTransform W=A.GetTransform()*E.WorldTransform;
            Regions.Add(Obj(RegionJson(A,E.WorldTransform)));
            TArray<FString> C={ID,RID,A.RegionKey.ToString(),A.DisplayName,A.bEnabled?TEXT("true"):TEXT("false"),H->GetShapeType().ToString()};
            C.Append(TransformCells(A.GetTransform())); C.Append(TransformCells(W));
            double N=0; C.Add(G->TryGetNumberField(TEXT("radius"),N)?Number(N):TEXT(""));
            FVector Half; const bool HasHalf=ReadVector(G->TryGetField(TEXT("half_extents")),Half);
            for (int I=0;I<3;++I) C.Add(HasHalf?Number(Half[I]):TEXT(""));
            C.Add(G->TryGetNumberField(TEXT("min_z"),N)?Number(N):TEXT("")); C.Add(G->TryGetNumberField(TEXT("max_z"),N)?Number(N):TEXT(""));
            Row(Files[TEXT("regions.csv")],C);
            if (const auto* Poly=A.Geometry.GetPtr<FGuLiMapPolygonPrism>()) for (int32 I=0;I<Poly->Vertices.Num();++I)
            {
                const auto V=Poly->Vertices[I]; const FVector P=W.TransformPosition(FVector(V.X,V.Y,0));
                Row(Files[TEXT("vertices.csv")],{ID,RID,FString::FromInt(I),Number(V.X),Number(V.Y),Number(P.X),Number(P.Y),Number(P.Z)});
            }
            // All shape parameters also appear here, so extension fields cannot disappear from CSV.
            TArray<FString> GKeys; G->Values.GetKeys(GKeys); GKeys.Sort();
            for (const auto& K:GKeys) Row(Files[TEXT("properties.csv")],{ID,RID,TEXT(""),K,TEXT("shape_json"),CanonicalJson(G->Values[K]),TEXT(""),TEXT(""),TEXT("")});
        }
        J->SetArrayField(TEXT("regions"),Regions); Markers.Add(Obj(J));
    }
    TArray<FString> TypeKeys; Types.GetKeys(TypeKeys); TypeKeys.Sort();
    for (const auto& K:TypeKeys)
    {
        const auto& T=*Types[K]; auto J=MakeShared<FJsonObject>();
        J->SetStringField(TEXT("type_id"),K); J->SetStringField(TEXT("asset_path"),T.GetPathName()); J->SetStringField(TEXT("display_name"),T.DisplayName);
        J->SetStringField(TEXT("editor_icon"),T.EditorIcon.ToString());
        J->SetField(TEXT("color_rgb"),VectorJson(FVector(T.Color.R,T.Color.G,T.Color.B))); J->SetNumberField(TEXT("color_alpha"),T.Color.A);
        TArray<TSharedPtr<FJsonValue>> Shapes,Defs,Defaults; TArray<FName> ShapeNames=T.AllowedShapes; ShapeNames.Sort(FNameLexicalLess());
        for (FName N:ShapeNames) Shapes.Add(Str(N.ToString())); J->SetArrayField(TEXT("allowed_shapes"),Shapes);
        for (const auto& D:Fields(T.DefaultParameters))
        {
            auto F=MakeShared<FJsonObject>(); F->SetStringField(TEXT("field_id"),Guid(D.ID)); F->SetStringField(TEXT("field_key"),D.Name.ToString());
            F->SetStringField(TEXT("field_type"),FieldType(D)); F->SetStringField(TEXT("value_type_object"),D.ValueTypeObject?D.ValueTypeObject->GetPathName():TEXT(""));
            F->SetField(TEXT("default"),FieldValue(T.DefaultParameters,D));
            for (const auto& Rule:T.FieldRules) if (Rule.FieldId==D.ID)
            {
                F->SetStringField(TEXT("display_name"),Rule.DisplayName); F->SetBoolField(TEXT("required"),Rule.bRequired);
                if (Rule.bUseMinimum) F->SetNumberField(TEXT("minimum"),Rule.Minimum); if (Rule.bUseMaximum) F->SetNumberField(TEXT("maximum"),Rule.Maximum);
            }
            Defs.Add(Obj(F));
        }
        for (const auto& R:T.DefaultRegions) Defaults.Add(Obj(RegionJson(R,FTransform::Identity)));
        J->SetArrayField(TEXT("fields"),Defs); J->SetArrayField(TEXT("default_regions"),Defaults); TypeArray.Add(Obj(J));
    }
    Root->SetArrayField(TEXT("types"),TypeArray); Root->SetArrayField(TEXT("markers"),Markers);
    if (!JsonFinite(Obj(Root))) { Issues.Emplace(TEXT("Serializer produced non-finite or missing data.")); return false; }
    Files.Add(TEXT("layout.json"),CanonicalJson(Obj(Root))+TEXT("\n"));
    TArray<FName> Names; Exporters.GetKeys(Names); Names.Sort(FNameLexicalLess());
    for (FName Name:Names)
    {
        TMap<FString,FString> Extra; FString Error;
        if (!Exporters[Name]->Generate(S,Extra,Error)) { Issues.Emplace(Error); return false; }
        for (const auto& P:Extra) { if (Files.Contains(P.Key)) { Issues.Emplace(TEXT("Exporter filename collision: ")+P.Key); return false; } Files.Add(P); }
    }
    Out=MoveTemp(Files); return true;
}
bool Publish(const FString& Directory,const TMap<FString,FString>& Files,FString& Error)
{
    FString Final=FPaths::ConvertRelativePathToFull(Directory); FPaths::NormalizeDirectoryName(Final);
    if (Files.IsEmpty() || Final.Len()<8 || FPaths::GetCleanFilename(Final).IsEmpty()) { Error=TEXT("Invalid output directory/batch."); return false; }
    for (const auto& P:Files) if (P.Key!=FPaths::GetCleanFilename(P.Key) || P.Key.Contains(TEXT("..")) || P.Key.IsEmpty())
    { Error=TEXT("Exporter supplied unsafe filename."); return false; }
    IPlatformFile& FS=FPlatformFileManager::Get().GetPlatformFile();
    const FString Stage=Final+TEXT(".staging-")+Guid(FGuid::NewGuid()), Previous=Final+TEXT(".previous"), Old=Final+TEXT(".retired-")+Guid(FGuid::NewGuid());
    if (!FS.CreateDirectoryTree(*Stage)) { Error=TEXT("Cannot create staging directory."); return false; }
    bool Staged=true;
    for (const auto& P:Files) if (!FFileHelper::SaveStringToFile(P.Value,*(Stage/P.Key),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) { Staged=false; break; }
    if (!Staged) { FS.DeleteDirectoryRecursively(*Stage); Error=TEXT("Staging write failed; published batch unchanged."); return false; }
    // On supported Win64, MoveFile renames directories atomically on the same volume.
    const bool HadPrevious=FS.DirectoryExists(*Previous), HadFinal=FS.DirectoryExists(*Final);
    if (HadPrevious&&!FS.MoveFile(*Old,*Previous)) { FS.DeleteDirectoryRecursively(*Stage); Error=TEXT("Previous output is locked."); return false; }
    if (HadFinal&&!FS.MoveFile(*Previous,*Final))
    {
        if (HadPrevious) FS.MoveFile(*Previous,*Old); FS.DeleteDirectoryRecursively(*Stage); Error=TEXT("Current output is locked; unchanged."); return false;
    }
    if (!FS.MoveFile(*Final,*Stage))
    {
        const bool Restored=!HadFinal||FS.MoveFile(*Final,*Previous);
        if (HadPrevious && Restored) FS.MoveFile(*Previous,*Old);
        Error=TEXT("Publish failed. ")+(Restored?FString(TEXT("Previous batch restored.")):FString(TEXT("Restore failed; recover from "))+Previous);
        FS.DeleteDirectoryRecursively(*Stage); return false;
    }
    if (HadPrevious) FS.DeleteDirectoryRecursively(*Old);
    return true;
}
}
