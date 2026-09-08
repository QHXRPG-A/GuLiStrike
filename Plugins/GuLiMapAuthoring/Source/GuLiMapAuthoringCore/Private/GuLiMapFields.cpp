#include "GuLiMapAuthoring.h"
#include "Dom/JsonObject.h"
#include "UObject/UnrealType.h"

namespace GuLiMap
{
FString FieldType(const FPropertyBagPropertyDesc& D)
{
    if (D.ContainerTypes.Num()>0) return {};
    switch (D.ValueType)
    {
    case EPropertyBagPropertyType::Bool: return TEXT("bool");
    case EPropertyBagPropertyType::Int32: return TEXT("int32");
    case EPropertyBagPropertyType::Double: return TEXT("double");
    case EPropertyBagPropertyType::Name: return TEXT("name");
    case EPropertyBagPropertyType::String: return TEXT("string");
    case EPropertyBagPropertyType::Enum: return Cast<UEnum>(D.ValueTypeObject)?TEXT("enum"):TEXT("");
    case EPropertyBagPropertyType::Struct: return D.ValueTypeObject==TBaseStructure<FVector>::Get()?TEXT("vector"):TEXT("");
    case EPropertyBagPropertyType::SoftObject: return TEXT("soft_object");
    case EPropertyBagPropertyType::SoftClass: return TEXT("soft_class");
    default: return {};
    }
}
TSharedPtr<FJsonValue> FieldValue(const FInstancedPropertyBag& Bag, const FPropertyBagPropertyDesc& D)
{
    const FString T=FieldType(D);
    if (T==TEXT("bool")) return MakeShared<FJsonValueBoolean>(Bag.GetValueBool(D.Name).GetValue());
    if (T==TEXT("int32")) return MakeShared<FJsonValueNumber>(Bag.GetValueInt32(D.Name).GetValue());
    if (T==TEXT("double")) { const double V=Bag.GetValueDouble(D.Name).GetValue(); return FMath::IsFinite(V)?MakeShared<FJsonValueNumber>(V):TSharedPtr<FJsonValue>(); }
    if (T==TEXT("name")) return MakeShared<FJsonValueString>(Bag.GetValueName(D.Name).GetValue().ToString());
    if (T==TEXT("string")) return MakeShared<FJsonValueString>(Bag.GetValueString(D.Name).GetValue());
    if (T==TEXT("vector"))
    {
        const auto V=Bag.GetValueStruct<FVector>(D.Name);
        return V.IsValid() && !V.GetValue()->ContainsNaN()?VectorJson(*V.GetValue()):TSharedPtr<FJsonValue>();
    }
    if (T==TEXT("enum"))
    {
        const UEnum* E=CastChecked<UEnum>(D.ValueTypeObject);
        const auto V=Bag.GetValueEnum(D.Name,E);
        if (!V.IsValid() || !E->IsValidEnumValue(V.GetValue())) return {};
        return MakeShared<FJsonValueString>(E->GetNameStringByValue(V.GetValue()));
    }
    if (T==TEXT("soft_object") || T==TEXT("soft_class")) return MakeShared<FJsonValueString>(Bag.GetValueSoftPath(D.Name).GetValue().ToString());
    return {};
}
bool SetField(FInstancedPropertyBag& Bag, const FPropertyBagPropertyDesc& D, const TSharedPtr<FJsonValue>& V, FString& Error)
{
    const FString T=FieldType(D); bool B=false; double N=0; FString S; FVector Vec;
    EPropertyBagResult R=EPropertyBagResult::TypeMismatch;
    if (V)
    {
        if (T==TEXT("bool") && V->Type==EJson::Boolean && V->TryGetBool(B)) R=Bag.SetValueBool(D.Name,B);
        if (V->Type==EJson::Number && V->TryGetNumber(N) && FMath::IsFinite(N))
        {
            if (T==TEXT("double")) R=Bag.SetValueDouble(D.Name,N);
            if (T==TEXT("int32") && N>=MIN_int32 && N<=MAX_int32 && FMath::FloorToDouble(N)==N) R=Bag.SetValueInt32(D.Name,static_cast<int32>(N));
        }
        if (T==TEXT("vector") && ReadVector(V,Vec)) R=Bag.SetValueStruct(D.Name,Vec);
        if (V->Type==EJson::String && V->TryGetString(S))
        {
            if (T==TEXT("name")) R=Bag.SetValueName(D.Name,FName(*S));
            if (T==TEXT("string")) R=Bag.SetValueString(D.Name,S);
            if (T==TEXT("enum"))
            {
                const UEnum* E=Cast<UEnum>(D.ValueTypeObject);
                const int64 Val=E?E->GetValueByNameString(S):INDEX_NONE;
                if (Val>=0 && Val<=255) R=Bag.SetValueEnum(D.Name,static_cast<uint8>(Val),E);
            }
            if (T==TEXT("soft_object") || T==TEXT("soft_class"))
            {
                const FSoftObjectPath Path(S);
                if (S.IsEmpty() || Path.IsValid()) R=Bag.SetValueSoftPath(D.Name,Path);
            }
        }
    }
    if (R!=EPropertyBagResult::Success) { Error=TEXT("Invalid value/type for field ")+D.Name.ToString()+TEXT(" (")+T+TEXT(")"); return false; }
    return true;
}
static bool Compatible(const FPropertyBagPropertyDesc& A, const FPropertyBagPropertyDesc& B)
{
    return A.ValueType==B.ValueType && A.ValueTypeObject==B.ValueTypeObject && A.ContainerTypes==B.ContainerTypes;
}
bool MigrateFields(FInstancedPropertyBag& Bag, const FInstancedPropertyBag& Defaults, bool ExplicitReset, FString& Error)
{
    TArray<FName> CompatibleNames;
    if (Bag.GetPropertyBagStruct()) for (const auto& Old:Bag.GetPropertyBagStruct()->GetPropertyDescs())
    {
        const auto* New=Defaults.FindPropertyDescByID(Old.ID);
        if (!New || !Compatible(Old,*New))
        {
            if (!ExplicitReset) { Error=TEXT("Schema conflict at ")+Old.Name.ToString()+TEXT("; old values retained. Restore definition or explicitly upgrade/reset."); return false; }
        }
        else CompatibleNames.Add(Old.Name);
    }
    FInstancedPropertyBag Candidate=Defaults;
    // Explicit typed copy avoids the engine's permissive numeric conversions on incompatible fields.
    for (FName Name:CompatibleNames)
    {
        const auto* Old=Bag.FindPropertyDescByName(Name);
        const auto* New=Defaults.FindPropertyDescByID(Old->ID);
        auto Value=FieldValue(Bag,*Old);
        if (!Value || !SetField(Candidate,*New,Value,Error)) return false;
    }
    Bag=MoveTemp(Candidate); return true;
}
void ValidateType(const UGuLiMapTypeDefinition& T, TArray<FGuLiMapIssue>& Issues)
{
    auto Fail=[&](const FString& Message,const FString& Field=TEXT("")) { Issues.Emplace(T.TypeId.ToString()+TEXT(": ")+Message,FGuid(),FGuid(),Field); };
    if (!IsKey(T.TypeId.ToString()) || T.TypeId.IsNone()) Fail(TEXT("TypeId must be a nonempty ASCII identifier."));
    TSet<FGuid> Ids; TSet<FName> Names;
    if (T.DefaultParameters.GetPropertyBagStruct()) for (const auto& D:T.DefaultParameters.GetPropertyBagStruct()->GetPropertyDescs())
    {
        if (!D.ID.IsValid() || Ids.Contains(D.ID) || Names.Contains(D.Name) || !IsKey(D.Name.ToString())) Fail(TEXT("Invalid/duplicate field identity."),D.Name.ToString());
        Ids.Add(D.ID); Names.Add(D.Name);
        if (FieldType(D).IsEmpty() || !FieldValue(T.DefaultParameters,D)) Fail(TEXT("Unsupported or invalid field/default."),D.Name.ToString());
    }
    TSet<FGuid> Rules;
    for (const auto& R:T.FieldRules)
    {
        if (!Ids.Contains(R.FieldId) || Rules.Contains(R.FieldId)) Fail(TEXT("Rule must reference one existing unique field GUID."));
        Rules.Add(R.FieldId);
        if ((R.bUseMinimum&&!FMath::IsFinite(R.Minimum)) || (R.bUseMaximum&&!FMath::IsFinite(R.Maximum)) ||
            (R.bUseMinimum&&R.bUseMaximum&&R.Minimum>R.Maximum)) Fail(TEXT("Invalid minimum/maximum."));
    }
    for (FName S:T.AllowedShapes) if (!FindGeometry(S)) Fail(TEXT("Unregistered allowed shape: ")+S.ToString());
    TSet<FName> RegionKeys;
    for (const auto& R:T.DefaultRegions)
    {
        FString Error; auto H=FindGeometry(R.Geometry);
        if (R.RegionKey.IsNone()||!IsKey(R.RegionKey.ToString())||RegionKeys.Contains(R.RegionKey)) Fail(TEXT("Invalid/duplicate default region key."));
        RegionKeys.Add(R.RegionKey);
        if (!H||!T.AllowedShapes.Contains(H->GetShapeType())||!H->Validate(R.Geometry,Error)) Fail(TEXT("Invalid default region geometry. ")+Error);
        if (R.Translation.ContainsNaN()||R.Rotation.ContainsNaN()) Fail(TEXT("Invalid default region transform."));
    }
}
}
