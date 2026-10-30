#include "GuLiHybridAuthoringLibrary.h"
#include "Network/GuLiHybridManifestRegistry.h"
#include "GuLiInitialArmyAuthoring.h"
#include "Network/GuLiHybridManifest.h"
#include "Gameplay/Data/GuLiUnitDataSubsystem.h"
#include "Gameplay/Data/GuLiCommanderSoldierResolver.h"
#include "Gameplay/Data/GuLiCommanderDataSettings.h"
#include "Gameplay/Data/GuLiSpellFieldDataSubsystem.h"
#include "Gameplay/Data/Generated/GuLiStrikeCommanderTableRows.h"
#include "Gameplay/Building/GuLiBuildingCatalog.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Gameplay/Navigation/Grid/GuLiGridQueries.h"
#include "Editor.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "NavigationSystem.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Gameplay/Models/GuLiModelRegistrySubsystem.h"
#include "UObject/UnrealType.h"

namespace
{
FString Report(bool Success,const FString& Error,const FString& File,int32 Objects,int32 Rules,int32 Tables)
{
    auto J=MakeShared<FJsonObject>(); J->SetBoolField(TEXT("success"),Success); J->SetStringField(TEXT("error"),Error);
    J->SetStringField(TEXT("file"),File); J->SetNumberField(TEXT("objects"),Objects);
    J->SetNumberField(TEXT("rules"),Rules); J->SetNumberField(TEXT("tables"),Tables);
    FString Result; FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Result)); return Result;
}

bool CatalogProperty(const FProperty& Property,const void* Value,const FString& Key,
    FGuLiHybridManifestData& Out,FString& Error,int32 Depth=0);
bool CatalogKey(const FProperty& Property,const void* Value,FString& Key,FString& Error,int32 Depth)
{
    FGuLiHybridManifestData ValueData;
    if (!CatalogProperty(Property,Value,TEXT("key"),ValueData,Error,Depth+1)) return false;
    TArray<FString> Names;ValueData.Numbers.GenerateKeyArray(Names);Names.Sort();
    for (const auto& Name:Names) Key+=FString::Printf(TEXT("n%d:%s=%.17g;"),Name.Len(),*Name,ValueData.Numbers[Name]);
    ValueData.Strings.GenerateKeyArray(Names);Names.Sort();
    for (const auto& Name:Names) {const auto& Text=ValueData.Strings[Name];Key+=FString::Printf(TEXT("s%d:%s=%d:%s;"),Name.Len(),*Name,Text.Len(),*Text);}
    return !Key.IsEmpty();
}
bool CatalogStruct(const UStruct& Type,const void* Value,const FString& Prefix,
    FGuLiHybridManifestData& Out,FString& Error,int32 Depth=0)
{
    if (Depth>24) { Error=TEXT("Catalog nesting limit: ")+Prefix; return false; }
    for (TFieldIterator<FProperty> It(&Type); It; ++It)
    {
        const auto* P=*It;
        if (P->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)) continue;
        for (int32 Index=0; Index<P->ArrayDim; ++Index)
        {
            const FString Key=Prefix+TEXT("/")+P->GetName()+(P->ArrayDim>1 ? FString::Printf(TEXT("[%d]"),Index) : TEXT(""));
            if (!CatalogProperty(*P,P->ContainerPtrToValuePtr<void>(Value,Index),Key,Out,Error,Depth+1)) return false;
        }
    }
    return true;
}
bool CatalogProperty(const FProperty& P,const void* Value,const FString& Key,
    FGuLiHybridManifestData& Out,FString& Error,int32 Depth)
{
    if (Depth>24 || Key.Len()>512) { Error=TEXT("Catalog key/nesting limit: ")+Key; return false; }
    if (const auto* N=CastField<FNumericProperty>(&P))
    {
        const bool Unsigned=CastField<FUInt16Property>(&P) || CastField<FUInt32Property>(&P)
            || CastField<FUInt64Property>(&P) || CastField<FByteProperty>(&P);
        const double V=N->IsFloatingPoint() ? N->GetFloatingPointPropertyValue(Value)
            : Unsigned ? static_cast<double>(N->GetUnsignedIntPropertyValue(Value)) : static_cast<double>(N->GetSignedIntPropertyValue(Value));
        if (!FMath::IsFinite(V) || FMath::Abs(V)>9007199254740991.) { Error=TEXT("Invalid numeric catalog value: ")+Key; return false; }
        Out.Numbers.Add(Key,V); return true;
    }
    if (const auto* B=CastField<FBoolProperty>(&P)) { Out.Numbers.Add(Key,B->GetPropertyValue(Value)?1:0); return true; }
    if (const auto* E=CastField<FEnumProperty>(&P))
    { Out.Numbers.Add(Key,static_cast<double>(E->GetUnderlyingProperty()->GetSignedIntPropertyValue(Value))); return true; }
    if (const auto* A=CastField<FArrayProperty>(&P))
    {
        FScriptArrayHelper Array(A,Value); Out.Numbers.Add(Key+TEXT("/count"),Array.Num());
        if (Array.Num()>100000) { Error=TEXT("Catalog array too large: ")+Key; return false; }
        for (int32 I=0; I<Array.Num(); ++I)
            if (!CatalogProperty(*A->Inner,Array.GetRawPtr(I),Key+FString::Printf(TEXT("[%d]"),I),Out,Error,Depth+1)) return false;
        return true;
    }
    if (const auto* S=CastField<FStructProperty>(&P))
    {
        // Native math structs have custom serialization rather than reflected fields.
        if (S->Struct==TBaseStructure<FTransform>::Get())
        {
            const auto& T=*static_cast<const FTransform*>(Value); const FVector L=T.GetLocation(),Scale=T.GetScale3D(); const FQuat Q=T.GetRotation();
            const double N[]={L.X,L.Y,L.Z,Q.X,Q.Y,Q.Z,Q.W,Scale.X,Scale.Y,Scale.Z};
            for (int32 I=0; I<UE_ARRAY_COUNT(N); ++I)
            { if (!FMath::IsFinite(N[I])) { Error=TEXT("Invalid catalog transform: ")+Key; return false; } Out.Numbers.Add(Key+FString::Printf(TEXT("/%d"),I),N[I]); }
            return true;
        }
        return CatalogStruct(*S->Struct,Value,Key,Out,Error,Depth+1);
    }
    if (const auto* Map=CastField<FMapProperty>(&P))
    {
        FScriptMapHelper Items(Map,Value);if (Items.Num()>100000) {Error=TEXT("Catalog map exceeds capacity: ")+Key;return false;}
        TMap<FString,int32> Order;
        for (int32 I=0;I<Items.GetMaxIndex();++I) if (Items.IsValidIndex(I))
        {FString Canonical;if (!CatalogKey(*Map->KeyProp,Items.GetKeyPtr(I),Canonical,Error,Depth) || Order.Contains(Canonical)) {Error=TEXT("Ambiguous catalog map key: ")+Key;return false;}Order.Add(MoveTemp(Canonical),I);}
        TArray<FString> Keys;Order.GenerateKeyArray(Keys);Keys.Sort();Out.Numbers.Add(Key+TEXT("/count"),Keys.Num());
        for (int32 N=0;N<Keys.Num();++N)
        {
            const int32 I=Order[Keys[N]];const FString Entry=Key+FString::Printf(TEXT("[%d]"),N);
            if (!CatalogProperty(*Map->KeyProp,Items.GetKeyPtr(I),Entry+TEXT("/key"),Out,Error,Depth+1)
                || !CatalogProperty(*Map->ValueProp,Items.GetValuePtr(I),Entry+TEXT("/value"),Out,Error,Depth+1)) return false;
        }
        return true;
    }
    if (const auto* Set=CastField<FSetProperty>(&P))
    {
        FScriptSetHelper Items(Set,Value);if (Items.Num()>100000) {Error=TEXT("Catalog set exceeds capacity: ")+Key;return false;}
        TMap<FString,int32> Order;
        for (int32 I=0;I<Items.GetMaxIndex();++I) if (Items.IsValidIndex(I))
        {FString Canonical;if (!CatalogKey(*Set->ElementProp,Items.GetElementPtr(I),Canonical,Error,Depth) || Order.Contains(Canonical)) {Error=TEXT("Ambiguous catalog set item: ")+Key;return false;}Order.Add(MoveTemp(Canonical),I);}
        TArray<FString> Keys;Order.GenerateKeyArray(Keys);Keys.Sort();Out.Numbers.Add(Key+TEXT("/count"),Keys.Num());
        for (int32 N=0;N<Keys.Num();++N) if (!CatalogProperty(*Set->ElementProp,Items.GetElementPtr(Order[Keys[N]]),Key+FString::Printf(TEXT("[%d]"),N),Out,Error,Depth+1)) return false;
        return true;
    }
    FString Text; P.ExportTextItem_Direct(Text,Value,nullptr,nullptr,PPF_None);
    if (Text.Len()>4096) { Error=TEXT("Catalog selector too long: ")+Key; return false; }
    Out.Strings.Add(Key,Text); return true;
}

bool ExportTables(FGuLiHybridManifestData& Out,TSet<FString>& TablePaths,int32& Count,FString& Error)
{
    // Gameplay providers opt numerical dependencies into the hashed manifest.
    // New presentation assets never require a name whitelist in this exporter.
    for (const auto& [Key,Path]:Out.Strings) if (Key.StartsWith(TEXT("numerical_table/"))) TablePaths.Add(Path);
    TArray<FString> Paths=TablePaths.Array(); Paths.Sort();
    for (const auto& Path : Paths)
    {
        auto* Table=LoadObject<UDataTable>(nullptr,*Path);
        if (!Table || !Table->GetRowStruct()) { Error=TEXT("Missing production table: ")+Path; return false; }
        TArray<FName> Rows=Table->GetRowNames(); Rows.Sort(FNameLexicalLess());
        Out.Strings.Add(Path+TEXT("/row_struct"),Table->GetRowStruct()->GetPathName());
        Out.Numbers.Add(Path+TEXT("/row_count"),Rows.Num());
        for (const auto Name : Rows)
        {
            const auto* Row=Table->GetRowMap().Find(Name);
            if (!Row || !*Row || !CatalogStruct(*Table->GetRowStruct(),*Row,Path+TEXT("/")+Name.ToString(),Out,Error)) return false;
        }
        ++Count;
    }
    return true;
}
bool ExportResourceMap(const UGuLiResourceMapDefinition& Map,FGuLiHybridManifestData& Out,FString& Error)
{
    // The validated layout hash covers full client geometry. The service needs
    // accounting identities/amounts, not thousands of art instance transforms.
    for (TFieldIterator<FProperty> It(Map.GetClass()); It; ++It)
    {
        const auto* P=*It;
        if (P->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated) || P->GetFName()==TEXT("Nodes")) continue;
        if (!CatalogProperty(*P,P->ContainerPtrToValuePtr<void>(&Map),TEXT("resource_map/")+P->GetName(),Out,Error)) return false;
    }
    Out.Numbers.Add(TEXT("resource_map/node_count"),Map.Nodes.Num());
    Out.Strings.Add(TEXT("startup/resource_ledger"),TEXT("1"));
    Out.Strings.Add(TEXT("startup/mining_reservations"),TEXT("1"));
    for (const auto& N : Map.Nodes)
    {
        const FString Prefix=FString::Printf(TEXT("ore/%u/"),N.NodeId);
        Out.Numbers.Add(Prefix+TEXT("cluster"),N.ClusterId);
        Out.Numbers.Add(Prefix+TEXT("type"),static_cast<uint8>(N.ResourceType));
        Out.Numbers.Add(Prefix+TEXT("amount"),N.InitialAmount);
    }
    return true;
}
}

FString UGuLiHybridAuthoringLibrary::ValidateRuntimeManifest(const FString& File)
{
    FGuLiHybridManifestData Data;FString Error;
    const bool Success=FGuLiHybridManifest::Read(File,Data,Error);
    return Report(Success,Error,File,Data.Objects.Num(),Data.Rules.Num(),0);
}

FString UGuLiHybridAuthoringLibrary::ExportRuntimeManifestWithOverrides(int32 Population,const FString& File,const FString& Overrides)
{
    auto* World=GEditor?GEditor->GetEditorWorldContext().World():nullptr;
    if (!World || World->IsGameWorld() || GEditor->PlayWorld)
        return Report(false,TEXT("Data-profile export requires an idle editor"),File,0,0,0);
    TArray<TSharedPtr<FJsonValue>> Values;
    if (Overrides.Len()>65536 || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Overrides),Values) || Values.Num()>64)
        return Report(false,TEXT("Invalid data profile"),File,0,0,0);
    struct FOriginal {UObject* Object;FProperty* Property;FString Text;};
    TArray<FOriginal> Originals;TSet<FString> Seen;
    const auto Refresh=[&]()
    {
        if (auto* Models=World->GetSubsystem<UGuLiModelRegistrySubsystem>()) Models->ReloadCatalog();
        if (auto* Buildings=UGuLiBuildingCatalog::LoadDefaultCatalog()) Buildings->ResolveTable(true);
    };
    ON_SCOPE_EXIT
    {
        for (int32 I=Originals.Num()-1;I>=0;--I)
        {const auto& O=Originals[I];O.Property->ImportText_Direct(*O.Text,O.Property->ContainerPtrToValuePtr<void>(O.Object),O.Object,PPF_None);}
        Refresh();
    };
    for (const auto& Value:Values)
    {
        const auto J=Value->AsObject();FString ClassPath,Name,Text;
        if (!J || !J->TryGetStringField(TEXT("class"),ClassPath) || !ClassPath.StartsWith(TEXT("/Script/GuLiStrike."))
            || !J->TryGetStringField(TEXT("property"),Name) || !J->TryGetStringField(TEXT("value"),Text)
            || Seen.Contains(ClassPath+TEXT(":")+Name)) return Report(false,TEXT("Malformed or duplicated data setting"),File,0,0,0);
        auto* Class=LoadObject<UClass>(nullptr,*ClassPath);
        auto* P=Class?FindFProperty<FProperty>(Class,*Name):nullptr;
        if (!Class || !Class->HasAnyClassFlags(CLASS_Config) || !P || !P->HasAnyPropertyFlags(CPF_Config))
            return Report(false,TEXT("Data override is not a project config property: ")+ClassPath+TEXT(":")+Name,File,0,0,0);
        auto* Object=Class->GetDefaultObject();auto* Address=P->ContainerPtrToValuePtr<void>(Object);
        FOriginal Old{Object,P,{}};P->ExportTextItem_Direct(Old.Text,Address,nullptr,Object,PPF_None);Originals.Add(MoveTemp(Old));
        const auto* End=P->ImportText_Direct(*Text,Address,Object,PPF_None);
        if (!End || *End) return Report(false,TEXT("Cannot parse data setting: ")+Name,File,0,0,0);
        Seen.Add(ClassPath+TEXT(":")+Name);
    }
    Refresh();return ExportRuntimeManifest(Population,File);
}

FString UGuLiHybridAuthoringLibrary::ExportRuntimeManifest(int32 Population,const FString& RequestedFile)
{
    FString Error;
    const FString File=FPaths::ConvertRelativePathToFull(RequestedFile.IsEmpty()
        ? FPaths::ProjectConfigDir()/FString::Printf(TEXT("Hybrid/RuntimeManifest-%d.json"),Population) : RequestedFile);
    auto* World=GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World || World->IsGameWorld() || GEditor->PlayWorld || (Population!=200 && Population!=600))
        return Report(false,TEXT("Export requires an idle editor and population 200 or 600"),File,0,0,0);
    auto* Map=LoadObject<UGuLiResourceMapDefinition>(nullptr,TEXT("/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap.DA_CommanderResourceMap"));
    auto* Economy=LoadObject<UGuLiResourceEconomyConfig>(nullptr,TEXT("/Game/GuLiStrike/Data/Resources/DA_ResourceEconomy.DA_ResourceEconomy"));
    auto* Buildings=UGuLiBuildingCatalog::LoadDefaultCatalog();
    if (!Map || !Economy || !Buildings || !Map->ValidateDefinition(Error) || !Economy->ValidateConfig(Error) || !Buildings->IsUsable())
        return Report(false,Error.IsEmpty()?TEXT("Missing production resource/building catalog"):Error,File,0,0,0);
    if (Map->MapPackage.ToString()!=World->GetOutermost()->GetName())
        return Report(false,TEXT("The resource bake does not belong to the open map"),File,0,0,0);
    FGuLiInitialArmyAuthoring Army; int32 Validated=0;
    if (!Army.Build(*World,Error,Population) || !Army.PlaceLoadOnNavigation(*World,*Map,Error)
        || !Army.Validate(*World,*Map,Validated,Error) || Validated!=Population)
        return Report(false,Error.IsEmpty()?TEXT("Initial population count mismatch"):Error,File,0,0,0);

    FGuLiHybridManifestData Data; Data.MapPackage=Map->MapPackage.ToString();
    Data.RedTeamBlue=Data.BlueTeamBlue=Economy->InitialBlueInventory; Data.RedTeamRed=Data.BlueTeamRed=Economy->InitialRedInventory;
    Data.Numbers.Add(TEXT("acceptance/population"),Population);
    Data.Strings.Add(TEXT("startup/coverage"),TEXT("Mass"));
    TSet<FString> Paths;
    auto RequireTable=[&](const TSoftObjectPtr<UDataTable>& Ref)->UDataTable*
    { Paths.Add(Ref.ToSoftObjectPath().ToString()); return Ref.LoadSynchronous(); };
    UDataTable* Soldiers=RequireTable(GetDefault<UGuLiUnitDataSettings>()->SoldierDataTable);
    const auto* Settings=GetDefault<UGuLiCommanderDataSettings>();
    UDataTable* Skills=RequireTable(Settings->SkillDataTable); UDataTable* UnitSkills=RequireTable(Settings->UnitSkillDataTable);
    RequireTable(Settings->WeaponMountDataTable);
    RequireTable(GetDefault<UGuLiSpellFieldDataSettings>()->DataTable);
    if (!Soldiers || Soldiers->GetRowStruct()!=FGuLiStrikeCommanderSoldiersRow::StaticStruct()
        || !Skills || Skills->GetRowStruct()!=FGuLiStrikeCommanderSkillsRow::StaticStruct()
        || !UnitSkills || UnitSkills->GetRowStruct()!=FGuLiStrikeCommanderUnitSkillsRow::StaticStruct())
        return Report(false,TEXT("Incompatible Soldiers/Skills/UnitSkills production tables"),File,0,0,0);
    TMap<uint16,FGuLiSoldierDefinition> Definitions;
    for (const auto Name : Soldiers->GetRowNames())
    {
        bool Valid=false;
        const auto D=FGuLiCommanderSoldierResolver::Resolve(Soldiers,Name,FGuLiCommanderSoldierResolver::MakeFallbackDefinition(),Valid);
        if (!Valid || Definitions.Contains(D.UnitTypeId)) return Report(false,TEXT("Invalid or duplicate Soldiers row: ")+Name.ToString(),File,0,0,0);
        Definitions.Add(D.UnitTypeId,D);
    }
    const auto Grid=GuLiGrid::AuthoredSnapshot(*World);
    auto* Navigation=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    const auto* NavData=Navigation ? Navigation->GetNavDataForAgentName(TEXT("CommanderSoldier")) : nullptr;
    uint32 Ordinal=0;
    for (const auto& Slot : Army.Slots)
    {
        FNavLocation Projected;
        const bool bProjected=Grid
            ? Grid->Project(Slot.Location,Definitions.FindChecked(Slot.UnitTypeId).FootprintCells,
                FVector(0,0,GuLiCommanderInitialSpawn::ProjectionVerticalExtent),Projected.Location)
            : Navigation && NavData && Navigation->ProjectPointToNavigation(Slot.Location,Projected,
                FVector(Slot.RadiusCentimeters,Slot.RadiusCentimeters,GuLiCommanderInitialSpawn::ProjectionVerticalExtent),NavData);
        if (!bProjected)
            return Report(false,TEXT("Navigation changed during export"),File,Data.Objects.Num(),0,0);
        auto& O=Data.Objects.AddDefaulted_GetRef(); O.Ordinal=++Ordinal; O.Team=Slot.Team;
        const FString AuthoredId=Slot.AuthoredId.IsEmpty()?FString::Printf(TEXT("army/%u/%d/%d"),static_cast<uint8>(Slot.Team),Slot.FormationIndex,Slot.SlotIndex):Slot.AuthoredId;
        O.Key=FGuLiHybridManifest::MakeStableKey(Data.MapPackage,EGuLiHybridObjectKind::Soldier,AuthoredId);
        O.Definition=FString::Printf(TEXT("unit/%u"),Slot.UnitTypeId);
        O.Location=GuLiUnitMovement::WorldPosition(Projected.Location,Definitions.FindChecked(Slot.UnitTypeId).AltitudeCentimeters);
        O.FacingYawDegrees=Slot.FacingYawDegrees; O.Health=Slot.InitialMaxHealthOverride>0?Slot.InitialMaxHealthOverride:Definitions.FindChecked(Slot.UnitTypeId).MaxHealth;O.Shield=Slot.InitialShield;
        if(!Slot.AuthoredId.IsEmpty())
        {
            const FString Prefix=TEXT("deployment/")+O.Key.StableId.ToString(EGuidFormats::Digits);
            Data.Numbers.Add(Prefix+TEXT("/automatic_fire"),Slot.bAllowAutomaticFire?1:0);
            Data.Numbers.Add(Prefix+TEXT("/start_idle"),Slot.bStartIdle?1:0);
            Data.Strings.Add(Prefix+TEXT("/authored_id"),Slot.AuthoredId);
        }
    }
    for (const auto Name : UnitSkills->GetRowNames())
    {
        const auto* Row=UnitSkills->FindRow<FGuLiStrikeCommanderUnitSkillsRow>(Name,TEXT("HybridExport"),false);
        const auto* Skill=Row ? Skills->FindRow<FGuLiStrikeCommanderSkillsRow>(FName(*Row->SkillId),TEXT("HybridExport"),false) : nullptr;
        // SkillId need not equal its DataTable row name.
        if (Row && !Skill) for (const auto SkillName : Skills->GetRowNames())
        { const auto* Candidate=Skills->FindRow<FGuLiStrikeCommanderSkillsRow>(SkillName,TEXT("HybridExport"),false); if (Candidate && Candidate->SkillId==Row->SkillId) { Skill=Candidate; break; } }
        if (!Row || !Skill || Row->UnitTypeId<1 || Row->UnitTypeId>MAX_uint16 || !Definitions.Contains(static_cast<uint16>(Row->UnitTypeId)))
            return Report(false,TEXT("Unresolved UnitSkills row: ")+Name.ToString(),File,Data.Objects.Num(),Data.Rules.Num(),0);
        float Damage=Row->Damage; double Lifetime=Row->ProjectileLifetimeSeconds;
        if (!Skill->EffectConfigId.IsEmpty())
        {
            FGuLiSpellFieldConfig Field;
            if (!FMath::IsNearlyZero(Row->Damage) || !UGuLiSpellFieldDataSubsystem::ResolveAuthoredConfig(FName(*Skill->EffectConfigId),Field))
                return Report(false,TEXT("Invalid authored field damage: ")+Name.ToString(),File,Data.Objects.Num(),Data.Rules.Num(),0);
            Damage=Field.Damage; Lifetime=FMath::Max(Lifetime,static_cast<double>(Field.Delay+Field.Duration));
        }
        if (!FMath::IsFinite(Damage) || Damage<0 || !FMath::IsFinite(Row->AttackRatePerSecond) || Row->AttackRatePerSecond<0 || !FMath::IsFinite(Lifetime) || Lifetime<0)
            return Report(false,TEXT("Invalid damage/rate/lifetime: ")+Name.ToString(),File,Data.Objects.Num(),Data.Rules.Num(),0);
        auto& R=Data.Rules.AddDefaulted_GetRef(); R.SourceDefinition=FString::Printf(TEXT("unit/%d"),Row->UnitTypeId);
        R.Id=FGuLiHybridManifest::MakeDamageRuleId(R.SourceDefinition,FName(*Row->SkillId),FName(*Row->SlotId)); R.Damage=Damage;
        R.CooldownTicks=Row->AttackRatePerSecond>0 ? FMath::Max<int64>(1,FMath::CeilToInt64(60./Row->AttackRatePerSecond)) : 0;
        R.EffectTicks=FMath::Max<int64>(600,FMath::CeilToInt64(Lifetime*60.)); R.MaximumTargets=4096;
    }
    int32 Tables=0;
    if (!ExportResourceMap(*Map,Data,Error)
        || !CatalogStruct(*Economy->GetClass(),Economy,TEXT("economy"),Data,Error))
        return Report(false,Error,File,Data.Objects.Num(),Data.Rules.Num(),Tables);
    for (const auto& B : Buildings->Definitions)
        if (!CatalogStruct(*FGuLiBuildingDefinition::StaticStruct(),&B,FString::Printf(TEXT("building/%d"),B.DefinitionId),Data,Error))
            return Report(false,Error,File,Data.Objects.Num(),Data.Rules.Num(),Tables);
    if (!FGuLiHybridManifestRegistration::AppendAll(*World,Data,Error)
        || !ExportTables(Data,Paths,Tables,Error)
        || !FGuLiHybridManifest::Write(Data,File,Error)) return Report(false,Error,File,Data.Objects.Num(),Data.Rules.Num(),Tables);
    return Report(true,TEXT(""),File,Data.Objects.Num(),Data.Rules.Num(),Tables);
}
