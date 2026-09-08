#include "GuLiMapMarker.h"
#include "GuLiMapAuthoring.h"
#include "ScopedTransaction.h"
#include "Editor.h"

AGuLiMapMarker::AGuLiMapMarker()
{
    PrimaryActorTick.bCanEverTick=false;
    bIsEditorOnlyActor=true; bIsSpatiallyLoaded=false;
    SetActorEnableCollision(false);
    Visualization=CreateDefaultSubobject<UGuLiMapVisualizationComponent>(TEXT("MapRegions"));
    SetRootComponent(Visualization);
}
void AGuLiMapMarker::RegenerateIdentity()
{
    Record.MarkerId=FGuid::NewGuid();
    const auto* Type=Record.Type.LoadSynchronous();
    Record.MarkerKey=FName(*(Type?Type->TypeId.ToString():TEXT("Marker")).Append(TEXT("_")).Append(Record.MarkerId.ToString(EGuidFormats::Digits)));
    for (auto& R:Record.Regions) R.RegionId=FGuid::NewGuid();
}
void AGuLiMapMarker::EnsureRegionIdentities()
{
    TSet<FGuid> Seen; TSet<FName> Keys;
    for (auto& R:Record.Regions)
    {
        const bool New=!R.RegionId.IsValid()||Seen.Contains(R.RegionId);
        if (New)
        {
            R.RegionId=FGuid::NewGuid();
            FString Base=R.RegionKey.IsNone()?TEXT("Area"):R.RegionKey.ToString();
            for (int I=1; Keys.Contains(R.RegionKey)||R.RegionKey.IsNone();++I) R.RegionKey=FName(*(Base+TEXT("_")+FString::FromInt(I)));
            if (!R.Geometry.IsValid()) R.Geometry=FInstancedStruct::Make<FGuLiMapCylinder>();
        }
        Seen.Add(R.RegionId); Keys.Add(R.RegionKey);
    }
}
void AGuLiMapMarker::PostActorCreated() { Super::PostActorCreated(); if (!IsTemplate()&&!Record.MarkerId.IsValid()) RegenerateIdentity(); }
void AGuLiMapMarker::PostDuplicate(EDuplicateMode::Type Mode) { Super::PostDuplicate(Mode); if (Mode==EDuplicateMode::Normal&&!IsTemplate()) RegenerateIdentity(); }
void AGuLiMapMarker::PostEditImport() { Super::PostEditImport(); RegenerateIdentity(); }
void AGuLiMapMarker::RefreshVisuals()
{
    if (Visualization) { Visualization->UpdateBounds(); Visualization->MarkRenderStateDirty(); }
    if (GEditor) GEditor->RedrawLevelEditingViewports();
}
void AGuLiMapMarker::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
    EnsureRegionIdentities();
    SetActorScale3D(FVector::OneVector);
    Super::PostEditChangeProperty(Event); RefreshVisuals();
}
void AGuLiMapMarker::PostEditMove(bool Finished) { SetActorScale3D(FVector::OneVector); Super::PostEditMove(Finished); RefreshVisuals(); }
void AGuLiMapMarker::PostEditUndo() { Super::PostEditUndo(); RefreshVisuals(); }
void AGuLiMapMarker::SynchronizeType()
{
    if (const auto* T=Record.Type.LoadSynchronous())
    {
        auto Copy=Record.Parameters; FString Error;
        if (GuLiMap::MigrateFields(Copy,T->DefaultParameters,false,Error))
        { const FScopedTransaction Tx(NSLOCTEXT("GuLiMap","Sync","同步地图类型字段")); Modify(); Record.Parameters=MoveTemp(Copy); RefreshVisuals(); }
        else UE_LOG(LogTemp,Warning,TEXT("GuLiMap: %s"),*Error);
    }
}
void AGuLiMapMarker::UpgradeAndResetConflicts()
{
    if (const auto* T=Record.Type.LoadSynchronous())
    {
        auto Copy=Record.Parameters; FString Error;
        if (GuLiMap::MigrateFields(Copy,T->DefaultParameters,true,Error))
        { const FScopedTransaction Tx(NSLOCTEXT("GuLiMap","Upgrade","升级并重置冲突字段")); Modify(); Record.Parameters=MoveTemp(Copy); RefreshVisuals(); }
    }
}
