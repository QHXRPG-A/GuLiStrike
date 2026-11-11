#include "Gameplay/Data/GuLiAvoidanceCatalog.h"
#include "Gameplay/Data/Generated/GuLiStrikeAvoidanceTableRows.h"
#include "Gameplay/Data/Generated/GuLiStrikeBuildingsTableRows.h"
#include "Gameplay/Building/GuLiBuildingCatalog.h"
#include "Engine/DataTable.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/SecureHash.h"

UGuLiAvoidanceDataSettings::UGuLiAvoidanceDataSettings()
    : UnitsTable(FSoftObjectPath(TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeAvoidance_Units.DT_GuLiStrikeAvoidance_Units")))
    , BuildingCirclesTable(FSoftObjectPath(TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeAvoidance_BuildingCircles.DT_GuLiStrikeAvoidance_BuildingCircles")))
    , OreClusteringTable(FSoftObjectPath(TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeAvoidance_OreClustering.DT_GuLiStrikeAvoidance_OreClustering")))
{}

namespace
{
bool Positive(float V) {return FMath::IsFinite(V)&&V>0;}
bool Nonnegative(float V) {return FMath::IsFinite(V)&&V>=0;}
template<class T> bool Read(const TSoftObjectPtr<UDataTable>& Reference,TArray<const T*>& Rows,FString& Error)
{
    const auto* Table=Reference.LoadSynchronous();
    if (!Table || Table->GetRowStruct()!=T::StaticStruct())
    {Error=TEXT("Avoidance catalog is missing a required native DataTable: ")+Reference.ToString();return false;}
    TSet<int32> Ids;
    for (FName Name:Table->GetRowNames())
    {
        const T* Row=Table->FindRow<T>(Name,TEXT("AvoidanceCatalog"));
        if (!Row || Row->Id<=0 || Ids.Contains(Row->Id))
        {Error=TEXT("Avoidance catalog contains invalid or repeated row IDs: ")+Reference.ToString();return false;}
        Ids.Add(Row->Id);Rows.Add(Row);
    }
    Rows.Sort([](const T& A,const T& B){return A.Id<B.Id;});return true;
}
}

bool FGuLiAvoidanceCatalog::Load(TConstArrayView<uint16> UnitIds,FGuLiAvoidanceCatalog& Out,FString& Error)
{
    Error.Reset();FGuLiAvoidanceCatalog Cache;const auto* Settings=GetDefault<UGuLiAvoidanceDataSettings>();
    TArray<const FGuLiStrikeAvoidanceUnitsRow*> UnitRows;
    TArray<const FGuLiStrikeAvoidanceBuildingCirclesRow*> CircleRows;
    TArray<const FGuLiStrikeAvoidanceOreClusteringRow*> OreRows;
    TArray<const FGuLiStrikeBuildingsBuildingsRow*> BuildingRows;
    if (!Read(Settings->UnitsTable,UnitRows,Error) || !Read(Settings->BuildingCirclesTable,CircleRows,Error)
        || !Read(Settings->OreClusteringTable,OreRows,Error)
        || !Read(GetDefault<UGuLiBuildingDataSettings>()->BuildingsTable,BuildingRows,Error)) return false;
    auto Fail=[&](const TCHAR* Why){Error=Why;return false;};
    auto Canonical=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> UnitValues,BuildingValues;
    for (const auto* Row:UnitRows)
    {
        if (Row->UnitTypeId<=0 || Row->UnitTypeId>MAX_uint16 || !UnitIds.Contains(uint16(Row->UnitTypeId))
            || Cache.Units.Contains(uint16(Row->UnitTypeId)) || !Positive(Row->RadiusMeters)
            || !Positive(Row->GridReferenceDiameterMeters) || !Nonnegative(Row->ClearanceMeters)
            || !Nonnegative(Row->ContactMarginCm) || !Positive(Row->LookAheadSeconds) || !Positive(Row->YieldSeconds)
            || !Nonnegative(Row->ClearancePreference) || !Nonnegative(Row->SidePreferenceRadians))
            return Fail(TEXT("Avoidance Units has an invalid reference, radius, distance, angle or timing parameter."));
        FGuLiUnitAvoidanceProfile Profile;auto& C=Profile.Solver;
        C.Radius=Row->RadiusMeters*100;C.Clearance=Row->ClearanceMeters*100;C.ContactMargin=Row->ContactMarginCm;
        C.LookAheadSeconds=Row->LookAheadSeconds;C.ClearancePreference=Row->ClearancePreference;
        C.SidePreferenceRadians=Row->SidePreferenceRadians;
        C.YieldSeconds=Row->YieldSeconds;
        Profile.GridReferenceDiameterCm=Row->GridReferenceDiameterMeters*100;
        const float Angles[]={Row->TurnAngle1Degrees,Row->TurnAngle2Degrees,Row->TurnAngle3Degrees,
            Row->TurnAngle4Degrees,Row->TurnAngle5Degrees,Row->TurnAngle6Degrees,Row->TurnAngle7Degrees};
        for (int32 I=0;I<UE_ARRAY_COUNT(Angles);++I)
        {
            if (!Positive(Angles[I]) || Angles[I]>=180 || (I && Angles[I]<=Angles[I-1]))
                return Fail(TEXT("Avoidance turn angles must be strictly increasing between 0 and 180 degrees."));
            C.Turns[I]=GuLiCircleAvoidance::FTurn(Angles[I]);
        }
        Cache.Units.Add(uint16(Row->UnitTypeId),Profile);
        auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("unit"),Row->UnitTypeId);
        TArray<TSharedPtr<FJsonValue>> Values;
        for (float V:{C.Radius,C.Clearance,C.ContactMargin,C.LookAheadSeconds,
            C.ClearancePreference,C.SidePreferenceRadians,C.YieldSeconds,Profile.GridReferenceDiameterCm})
            Values.Add(MakeShared<FJsonValueNumber>(V));
        for (float Angle:Angles) Values.Add(MakeShared<FJsonValueNumber>(Angle));
        J->SetArrayField(TEXT("values"),Values);UnitValues.Add(MakeShared<FJsonValueObject>(J));
    }
    if (Cache.Units.Num()!=UnitIds.Num()) return Fail(TEXT("Avoidance Units must cover every Soldier UnitTypeId exactly once."));
    TMap<int32,uint8> RequiredPhases;TMap<int32,uint8> ActualPhases;
    for (const auto* B:BuildingRows) RequiredPhases.Add(B->Id,B->CompletionUnitTypeId?1:3);
    int32 FactoryWalls=0;
    for (const auto* Row:CircleRows)
    {
        if (!RequiredPhases.Contains(Row->BuildingDefinitionId) || Row->CircleIndex<0 || Row->CircleIndex>MAX_uint16 || !Positive(Row->RadiusMeters)
            || Row->LocalCenterMeters.ContainsNaN() || !FMath::IsFinite(Row->HeightMinimumMeters)
            || !FMath::IsFinite(Row->HeightMaximumMeters) || Row->HeightMinimumMeters>=Row->HeightMaximumMeters
            || Row->PhaseMask<1 || Row->PhaseMask>3)
            return Fail(TEXT("BuildingCircles has invalid geometry, height, phase or building references."));
        auto& List=Cache.Buildings.FindOrAdd(Row->BuildingDefinitionId);
        if (List.ContainsByPredicate([&](const auto& C){return C.Index==Row->CircleIndex;}))
            return Fail(TEXT("BuildingCircles repeats a circle index within a building."));
        List.Add({Row->CircleIndex,Row->LocalCenterMeters*100.,Row->RadiusMeters*100,
            Row->HeightMinimumMeters*100,Row->HeightMaximumMeters*100,uint8(Row->PhaseMask)});
        ActualPhases.FindOrAdd(Row->BuildingDefinitionId)|=uint8(Row->PhaseMask);
        FactoryWalls+=(Row->BuildingDefinitionId==6 && (Row->PhaseMask&2))?1:0;
        auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("building"),Row->BuildingDefinitionId);
        J->SetNumberField(TEXT("index"),Row->CircleIndex);J->SetNumberField(TEXT("phase"),Row->PhaseMask);
        TArray<TSharedPtr<FJsonValue>> Values;
        for (double V:{Row->LocalCenterMeters.X,Row->LocalCenterMeters.Y,Row->LocalCenterMeters.Z,
            double(Row->RadiusMeters),double(Row->HeightMinimumMeters),double(Row->HeightMaximumMeters)})
            Values.Add(MakeShared<FJsonValueNumber>(V));
        J->SetArrayField(TEXT("values"),Values);BuildingValues.Add(MakeShared<FJsonValueObject>(J));
    }
    for (const auto& Pair:RequiredPhases)
        if ((ActualPhases.FindRef(Pair.Key)&Pair.Value)!=Pair.Value)
            return Fail(TEXT("BuildingCircles is missing a required building phase."));
    if (FactoryWalls<2) return Fail(TEXT("Completed factories require multiple wall circles with a preserved entrance."));
    for (auto& Pair:Cache.Buildings) Pair.Value.Sort([](const auto& A,const auto& B){return A.Index<B.Index;});
    if (OreRows.Num()!=1 || OreRows[0]->Id!=1 || !Positive(OreRows[0]->MaximumRadiusRatio)
        || OreRows[0]->MaximumRadiusRatio<1 || !Positive(OreRows[0]->MaximumAreaRatio)
        || OreRows[0]->MaximumAreaRatio<1 || !OreRows[0]->PreserveWorkEntrances)
        return Fail(TEXT("OreClustering requires one valid default profile with protected work entrances."));
    Cache.OreMaximumRadiusRatio=OreRows[0]->MaximumRadiusRatio;Cache.OreMaximumAreaRatio=OreRows[0]->MaximumAreaRatio;
    Canonical->SetArrayField(TEXT("units"),UnitValues);Canonical->SetArrayField(TEXT("buildings"),BuildingValues);
    Canonical->SetNumberField(TEXT("ore_radius_ratio"),Cache.OreMaximumRadiusRatio);
    Canonical->SetNumberField(TEXT("ore_area_ratio"),Cache.OreMaximumAreaRatio);
    FString Json;FJsonSerializer::Serialize(Canonical,TJsonWriterFactory<>::Create(&Json));
    Cache.Fingerprint=FMD5::HashAnsiString(*Json);Out=MoveTemp(Cache);return true;
}
