#include "GuLiMapDesignAuthoringLibrary.h"
#include "GuLiGridAuthoringLibrary.h"
#include "GuLiLandscapeAuthoringLibrary.h"
#include "GuLiNavigationBakeLibrary.h"
#include "GuLiResourceAuthoringLibrary.h"
#include "GuLiMapAuthoringSubsystem.h"
#include "GuLiMapAuthoring.h"
#include "GuLiMapMarker.h"
#include "Gameplay/Navigation/Grid/GuLiGroundGridData.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "GuLiNetHash.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Builders/CubeBuilder.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "LandscapeEdit.h"
#include "LandscapeEditLayer.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "Materials/MaterialInterface.h"
#include "InstancedFoliageActor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "NavAreas/NavArea_Null.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavModifierVolume.h"
#include "NavigationSystem.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace GuLiMapDesign
{
constexpr const TCHAR* CanonicalMap=TEXT("/Game/Maps/LVL_CommanderMassPrototype");
const TCHAR* LayerNames[]={TEXT("FlatGround"),TEXT("Hills"),TEXT("Plateau"),TEXT("Rock"),TEXT("Beach"),TEXT("Water")};
FString Hash(TConstArrayView<uint8> Bytes)
{
    const auto Value=GuLiNet::Sha256({reinterpret_cast<const std::uint8_t*>(Bytes.GetData()),SIZE_T(Bytes.Num())});return FString(UTF8_TO_TCHAR(Value.c_str()));
}
template<class T> FString HashArray(const TArray<T>& Values)
{ return Hash({reinterpret_cast<const uint8*>(Values.GetData()),Values.Num()*int32(sizeof(T))}); }
TSharedPtr<FJsonValue> Vec(const FVector& V) { return GuLiMap::VectorJson(V); }
FString Json(const TSharedRef<FJsonObject>& J) { return GuLiMap::CanonicalJson(MakeShared<FJsonValueObject>(J))+TEXT("\n"); }
bool Error(FString& Out,const FString& Message) { Out=Message;return false; }
UWorld* World(FString& OutError)
{
    auto* W=GEditor?GEditor->GetEditorWorldContext().World():nullptr;
    if(!W||GEditor->PlayWorld||GEditor->bIsSimulatingInEditor||W->GetOutermost()->GetName()!=CanonicalMap)
    {OutError=TEXT("Open the canonical commander map outside PIE/SIE");return nullptr;}return W;
}
ALandscape* Landscape(UWorld& W,FString& OutError)
{
    ALandscape* Found=nullptr;for(TActorIterator<ALandscape> It(&W);It;++It){if(Found){OutError=TEXT("Expected one Landscape");return nullptr;}Found=*It;}
    if(!Found)OutError=TEXT("Landscape is missing");return Found;
}
bool ReadJson(const FString& File,TSharedPtr<FJsonObject>& Out,FString& OutError)
{
    FString Text;const int64 Size=IFileManager::Get().FileSize(*File);
    return (Size>=0&&Size<=128*1024*1024&&FFileHelper::LoadFileToString(Text,*File)&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Out)&&Out)
        ||Error(OutError,TEXT("Invalid JSON: ")+FPaths::GetCleanFilename(File));
}
bool ReadRaw(const FString& Dir,const FJsonObject& Manifest,const FString& Name,TArray<uint8>& Out,FString& OutError)
{
    const TSharedPtr<FJsonObject>* Files=nullptr;FString Expected;
    if(Name!=FPaths::GetCleanFilename(Name)||Name.Contains(TEXT(".."))||!Manifest.TryGetObjectField(TEXT("files"),Files)
        ||!(*Files)->TryGetStringField(Name,Expected)||Expected.Len()!=64||IFileManager::Get().FileSize(*(Dir/Name))>128*1024*1024
        ||!FFileHelper::LoadFileToArray(Out,*(Dir/Name))||Hash(Out)!=Expected)return Error(OutError,TEXT("Export file missing/changed: ")+Name);
    return true;
}
bool ReadPair(const FJsonObject& J,const TCHAR* Name,FVector2D& Pair,FString& OutError)
{
    const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;double X=0,Y=0;
    if(!J.TryGetArrayField(Name,Values)||Values->Num()!=2||!(*Values)[0]->TryGetNumber(X)||!(*Values)[1]->TryGetNumber(Y)
        ||!FMath::IsFinite(X)||!FMath::IsFinite(Y))return Error(OutError,TEXT("Invalid numeric pair: ")+FString(Name));
    Pair=FVector2D(X,Y);return true;
}
bool Source(UWorld& W,TSharedRef<FJsonObject> Out,FString& OutError)
{
    auto* L=Landscape(W,OutError);if(!L)return false;auto* Info=L->GetLandscapeInfo();int32 X0=0,Y0=0,X1=0,Y1=0;
    if(!Info||!Info->GetLandscapeExtent(X0,Y0,X1,Y1)||X0||Y0||int64(X1+1)*(Y1+1)>8*1024*1024)return Error(OutError,TEXT("Unsupported Landscape extent"));
    FLandscapeEditDataInterface Edit(Info);TArray<uint16> Heights;Heights.SetNumZeroed((X1+1)*(Y1+1));Edit.GetHeightDataFast(0,0,X1,Y1,Heights.GetData(),X1+1);
    Out->SetStringField(TEXT("height_u16"),HashArray(Heights));TArray<uint8> Weight;Weight.SetNumZeroed(Heights.Num());
    Out->SetStringField(TEXT("material_path"),L->LandscapeMaterial?L->LandscapeMaterial->GetPathName():TEXT(""));
    for(const TCHAR* Name:LayerNames){const auto* Settings=L->GetTargetLayers().Find(FName(Name));if(!Settings||!Settings->LayerInfoObj)return Error(OutError,TEXT("Missing source layer: ")+FString(Name));
        Edit.GetWeightDataFast(Settings->LayerInfoObj,0,0,X1,Y1,Weight.GetData(),X1+1);Out->SetStringField(FString(TEXT("weight_"))+Name,Hash(Weight));
        Out->SetStringField(FString(TEXT("layer_info_"))+Name,Settings->LayerInfoObj->GetPathName());}
    auto Follow=MakeShared<FJsonObject>();for(auto* Marker:GEditor->GetEditorSubsystem<UGuLiMapAuthoringSubsystem>()->LoadedMarkers())Follow->SetBoolField(GuLiMap::Guid(Marker->Record.MarkerId),Marker->bStandaloneFollowTerrain);
    Out->SetStringField(TEXT("follow_metadata"),Json(Follow));
    ARecastNavMesh* Nav=nullptr;for(TActorIterator<ARecastNavMesh> It(&W);It;++It){Nav=*It;break;}
    if(Nav)Out->SetStringField(TEXT("ground_geometry_u64"),FString::Printf(TEXT("%016llx"),UGuLiNavigationBakeLibrary::ComputeGroundSourceHash(&W,Nav)));
    Out->SetField(TEXT("location"),Vec(L->GetActorLocation()));Out->SetField(TEXT("scale"),Vec(L->GetActorScale3D()));
    Out->SetArrayField(TEXT("resolution"),{MakeShared<FJsonValueNumber>(X1+1),MakeShared<FJsonValueNumber>(Y1+1)});return true;
}
struct FPrepared
{
    TSharedPtr<FJsonObject> Manifest;TArray<uint16> Height;TArray<float> LogicalHeights;TArray<FLandscapeImportLayerInfo> Layers;
    FIntPoint Logical,Physical;FVector Location,Scale;FVector2D Origin;
};
float HeightAt(const FPrepared& P,double X,double Y)
{
    const double U=FMath::Clamp((X-P.Origin.X)/300.,0.,double(P.Logical.X)),V=FMath::Clamp((Y-P.Origin.Y)/300.,0.,double(P.Logical.Y));
    const int32 IX=FMath::Min(FMath::FloorToInt32(U),P.Logical.X-1),IY=FMath::Min(FMath::FloorToInt32(V),P.Logical.Y-1),I=IY*(P.Logical.X+1)+IX;
    const double FX=U-IX,FY=V-IY,H00=P.LogicalHeights[I],H10=P.LogicalHeights[I+1],H01=P.LogicalHeights[I+P.Logical.X+1],H11=P.LogicalHeights[I+P.Logical.X+2];
    return float(FX>=FY?H00+(H10-H00)*FX+(H11-H10)*FY:H00+(H11-H01)*FX+(H01-H00)*FY);
}
bool Gameplay(const FString& Directory,const FPrepared& P,FString& OutError)
{
    TSharedPtr<FJsonObject> Layout;if(!ReadJson(Directory/TEXT("layout.json"),Layout,OutError))return false;
    const TArray<TSharedPtr<FJsonValue>>* Markers=nullptr;if(!Layout->TryGetArrayField(TEXT("markers"),Markers))return Error(OutError,TEXT("Missing marker list"));int32 Count=0;
    for(const auto& V:*Markers)
    {
        const auto M=V->AsObject();if(M->GetStringField(TEXT("type_id"))!=TEXT("Outpost"))continue;++Count;
        const FString Key=M->GetStringField(TEXT("marker_key"));int32 Row=0,Column=0;
        if(!M->GetBoolField(TEXT("enabled"))||Key.Len()!=12||!Key.StartsWith(TEXT("Outpost_R"))||Key[10]!='C')return Error(OutError,TEXT("Current gameplay requires enabled canonical Outpost_R1C1 keys"));
        Row=Key[9]-'0';Column=Key[11]-'0';if(Row<1||Row>7||Column<1||Column>7)return Error(OutError,TEXT("Outpost row/column must be 1..7"));
        const auto Params=M->GetObjectField(TEXT("parameters"));if(Params->GetNumberField(TEXT("BoardRow"))!=Row||Params->GetNumberField(TEXT("BoardColumn"))!=Column)return Error(OutError,TEXT("Outpost parameters do not match its canonical key"));
        FVector Position;if(!GuLiMap::ReadVector(M->GetObjectField(TEXT("world_transform"))->TryGetField(TEXT("position")),Position))return Error(OutError,TEXT("Invalid outpost position"));
        const FVector Center=GuLiResources::GetTerritoryCenter(Row,Column);
        if(FMath::Abs(Position.X-Center.X)>=GULI_RESOURCE_TERRITORY_HALF_EXTENT_CM||FMath::Abs(Position.Y-Center.Y)>=GULI_RESOURCE_TERRITORY_HALF_EXTENT_CM)return Error(OutError,Key+TEXT(" is outside its current logical territory"));
        if(Position.X-1500<P.Origin.X||Position.Y-1500<P.Origin.Y||Position.X+1500>P.Origin.X+P.Logical.X*300.||Position.Y+1500>P.Origin.Y+P.Logical.Y*300.)return Error(OutError,Key+TEXT(" 30m pad is outside the design bounds"));
        for(double DX:{-1500.,0.,1500.})for(double DY:{-1500.,0.,1500.})
        {
            const double X=Position.X+DX,Y=Position.Y+DY,H=HeightAt(P,X,Y),HX=(HeightAt(P,X+1,Y)-HeightAt(P,X-1,Y))*.5,HY=(HeightAt(P,X,Y+1)-HeightAt(P,X,Y-1))*.5;
            if(H<=0||FMath::Sqrt(HX*HX+HY*HY)>FMath::Tan(FMath::DegreesToRadians(3.01)))return Error(OutError,Key+TEXT(" requires land above sea level and a <=3 degree 30m pad"));
        }
    }
    return Count==49||Error(OutError,TEXT("Current gameplay requires exactly 49 Outpost markers"));
}
bool Prepare(const FString& Directory,FPrepared& P,FString& OutError)
{
    UWorld* W=World(OutError);if(!W)return false;auto* L=Landscape(*W,OutError);if(!L)return false;
    if(!ReadJson(Directory/TEXT("manifest.json"),P.Manifest,OutError)||P.Manifest->GetStringField(TEXT("schema"))!=TEXT("guli-map-design/export-v1")||P.Manifest->GetStringField(TEXT("map_package"))!=CanonicalMap)return Error(OutError,TEXT("Unsupported export/target map"));
    const TSharedPtr<FJsonObject>* Files=nullptr;if(!P.Manifest->TryGetObjectField(TEXT("files"),Files)||(*Files)->Values.Num()>128)return Error(OutError,TEXT("Invalid export file list"));
    for(const auto& Pair:(*Files)->Values){TArray<uint8> Bytes;if(!ReadRaw(Directory,*P.Manifest,Pair.Key,Bytes,OutError))return false;}
    auto* Service=GEditor->GetEditorSubsystem<UGuLiMapAuthoringSubsystem>();const auto Authoring=Service->PreviewStandaloneSnapshot(Directory);
    if(!Authoring.bSuccess)return Error(OutError,Authoring.Issues.IsEmpty()?TEXT("Authoring preflight failed"):Authoring.Issues[0].Message);
    auto Baseline=MakeShared<FJsonObject>();if(!Source(*W,Baseline,OutError))return false;
    const TSharedPtr<FJsonObject>* Expected=nullptr;if(!P.Manifest->TryGetObjectField(TEXT("baseline"),Expected))return Error(OutError,TEXT("No source baseline attached"));
    for(const auto& Pair:Baseline->Values)if(Pair.Value->Type==EJson::String)
    {FString ExpectedValue;if(!(*Expected)->TryGetStringField(Pair.Key,ExpectedValue)||ExpectedValue!=Pair.Value->AsString())return Error(OutError,TEXT("UE terrain/material/geometry changed since capture: ")+Pair.Key);}
    FVector SourceLocation,SourceScale;
    if(!GuLiMap::ReadVector(P.Manifest->TryGetField(TEXT("source_location")),SourceLocation)||!GuLiMap::ReadVector(P.Manifest->TryGetField(TEXT("source_scale")),SourceScale)
        ||!L->GetActorLocation().Equals(SourceLocation,.0001)||!L->GetActorScale3D().Equals(SourceScale,.0001)||!L->GetActorRotation().IsNearlyZero())return Error(OutError,TEXT("UE source transform changed or is unsupported"));
    FVector2D Logical,Physical;
    if(!ReadPair(*P.Manifest,TEXT("logical_cells"),Logical,OutError)||!ReadPair(*P.Manifest,TEXT("landscape_cells"),Physical,OutError)||!ReadPair(*P.Manifest,TEXT("origin_cm"),P.Origin,OutError))return false;
    if(Logical.GetMin()<1||Logical.GetMax()>8160||Logical.X*Logical.Y>4*1024*1024||FMath::FloorToDouble(Logical.X)!=Logical.X||FMath::FloorToDouble(Logical.Y)!=Logical.Y
        ||Physical.X!=FMath::CeilToDouble(Logical.X/63)*63||Physical.Y!=FMath::CeilToDouble(Logical.Y/63)*63||P.Manifest->GetNumberField(TEXT("cell_size_cm"))!=300)return Error(OutError,TEXT("Invalid 3m grid/63-quad component dimensions"));
    P.Logical=FIntPoint(int32(Logical.X),int32(Logical.Y));P.Physical=FIntPoint(int32(Physical.X),int32(Physical.Y));
    int32 SourceX0=0,SourceY0=0,SourceX1=0,SourceY1=0;
    if(!L->GetLandscapeInfo()||!L->GetLandscapeInfo()->GetLandscapeExtent(SourceX0,SourceY0,SourceX1,SourceY1))return Error(OutError,TEXT("Source extent unavailable"));
    if(SourceX1!=P.Physical.X||SourceY1!=P.Physical.Y)
    {
        for(TActorIterator<AInstancedFoliageActor> It(W);It;++It)
        {TInlineComponentArray<UInstancedStaticMeshComponent*> Components;It->GetComponents(Components);for(const auto* C:Components)if(C->GetInstanceCount()>0)return Error(OutError,TEXT("Topology replacement needs an explicit foliage-base migration; source foliage is retained. Height-only imports remain available."));}
    }
    if(!GuLiMap::ReadVector(P.Manifest->TryGetField(TEXT("location")),P.Location)||!GuLiMap::ReadVector(P.Manifest->TryGetField(TEXT("scale")),P.Scale)
        ||P.Scale.X!=300||P.Scale.Y!=300||P.Scale.Z<=0||P.Location.X!=P.Origin.X||P.Location.Y!=P.Origin.Y)return Error(OutError,TEXT("Invalid Landscape transform"));
    TArray<uint8> Raw;if(!ReadRaw(Directory,*P.Manifest,TEXT("height.r16"),Raw,OutError)||Raw.Num()!=int64(P.Physical.X+1)*(P.Physical.Y+1)*2)return Error(OutError,TEXT("Wrong physical height sample count"));
    P.Height.SetNumUninitialized(Raw.Num()/2);FMemory::Memcpy(P.Height.GetData(),Raw.GetData(),Raw.Num());
    if(!ReadRaw(Directory,*P.Manifest,TEXT("heights_cm.f32"),Raw,OutError)||Raw.Num()!=int64(P.Logical.X+1)*(P.Logical.Y+1)*4)return Error(OutError,TEXT("Wrong logical height sample count"));
    P.LogicalHeights.SetNumUninitialized(Raw.Num()/4);FMemory::Memcpy(P.LogicalHeights.GetData(),Raw.GetData(),Raw.Num());
    for(int32 Y=0;Y<=P.Logical.Y;++Y)for(int32 X=0;X<=P.Logical.X;++X)
    {const double H=(double(P.Height[Y*(P.Physical.X+1)+X])-32768.)/128.*P.Scale.Z+P.Location.Z;if(!FMath::IsFinite(P.LogicalHeights[Y*(P.Logical.X+1)+X])||!FMath::IsNearlyEqual(H,double(P.LogicalHeights[Y*(P.Logical.X+1)+X]),.01))return Error(OutError,TEXT("Physical and logical height buffers disagree"));}
    for(const TCHAR* Name:LayerNames)
    {
        const auto* Layer=L->GetTargetLayers().Find(FName(Name));if(!Layer||!Layer->LayerInfoObj)return Error(OutError,TEXT("Missing target layer"));
        FLandscapeImportLayerInfo Import{FName(Name)};Import.LayerInfo=Layer->LayerInfoObj;Import.SourceFilePath=Directory/(FString(Name)+TEXT(".u8"));
        if(!ReadRaw(Directory,*P.Manifest,FString(Name)+TEXT(".u8"),Import.LayerData,OutError)||Import.LayerData.Num()!=P.Height.Num())return Error(OutError,TEXT("Wrong weight buffer size"));P.Layers.Add(MoveTemp(Import));
    }
    for(int32 I=0;I<P.Height.Num();++I){int32 Sum=0;for(const auto& Layer:P.Layers)Sum+=Layer.LayerData[I];if(Sum!=255)return Error(OutError,TEXT("Weight-blended layer samples must sum to 255"));}
    return Gameplay(Directory,P,OutError);
}
}

FGuLiMapDesignResult UGuLiMapDesignAuthoringLibrary::ReadSourceBaseline()
{
    FGuLiMapDesignResult R;auto* W=GuLiMapDesign::World(R.Message);if(!W)return R;auto J=MakeShared<FJsonObject>();R.bSuccess=GuLiMapDesign::Source(*W,J,R.Message);if(R.bSuccess)R.Json=GuLiMapDesign::Json(J);return R;
}
FGuLiMapDesignResult UGuLiMapDesignAuthoringLibrary::PreviewDesignImport(const FString& Directory)
{
    FGuLiMapDesignResult R;GuLiMapDesign::FPrepared P;R.bSuccess=GuLiMapDesign::Prepare(Directory,P,R.Message);if(R.bSuccess){auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("logical_width"),P.Logical.X);J->SetNumberField(TEXT("logical_length"),P.Logical.Y);J->SetNumberField(TEXT("physical_width"),P.Physical.X);J->SetNumberField(TEXT("physical_length"),P.Physical.Y);J->SetStringField(TEXT("status"),TEXT("validated_without_mutation"));R.Json=GuLiMapDesign::Json(J);}return R;
}

namespace GuLiMapDesign
{
bool ApplyLandscape(UWorld& W,const FString& Directory,const FPrepared& P,FString& OutError)
{
    auto* Old=Landscape(W,OutError);if(!Old)return false;auto* OldInfo=Old->GetLandscapeInfo();int32 X0=0,Y0=0,X1=0,Y1=0;
    if(!OldInfo||!OldInfo->GetLandscapeExtent(X0,Y0,X1,Y1))return Error(OutError,TEXT("Source extent unavailable"));
    const bool Replace=X0||Y0||X1!=P.Physical.X||Y1!=P.Physical.Y;ALandscape* Target=Old;
    if(Replace)
    {
        FActorSpawnParameters Params;Params.OverrideLevel=W.PersistentLevel;Params.ObjectFlags|=RF_Transactional;
        Target=W.SpawnActor<ALandscape>(ALandscape::StaticClass(),P.Location,FRotator::ZeroRotator,Params);
        if(!Target)return Error(OutError,TEXT("Could not stage replacement Landscape"));
        Target->SetActorScale3D(P.Scale);Target->LandscapeMaterial=Old->LandscapeMaterial;Target->LandscapeHoleMaterial=Old->LandscapeHoleMaterial;
        for(const TCHAR* Name:{TEXT("bCastContactShadow"),TEXT("bCastDynamicShadow"),TEXT("bCastFarShadow"),TEXT("bCastHiddenShadow"),TEXT("CastShadow"),TEXT("bCastShadowAsTwoSided"),TEXT("bCastStaticShadow"),TEXT("LDMaxDrawDistance"),TEXT("RuntimeVirtualTextures"),TEXT("VirtualTextureLodBias"),TEXT("VirtualTextureNumLods"),TEXT("VirtualTextureRenderPassType"),TEXT("VirtualTextureRenderWithQuad"),TEXT("VirtualTextureRenderWithQuadHQ"),TEXT("ShadowCacheInvalidationBehavior"),TEXT("StaticLightingResolution"),TEXT("LODDistanceFactor"),TEXT("LOD0ScreenSize"),TEXT("LOD0DistributionSetting"),TEXT("LODDistributionSetting")})
            if(const auto* Property=FindFProperty<FProperty>(ALandscape::StaticClass(),Name))Property->CopyCompleteValue_InContainer(Target,Old);
        TMap<FGuid,TArray<uint16>> Heights;Heights.Add(FGuid(),P.Height);TMap<FGuid,TArray<FLandscapeImportLayerInfo>> Weights;Weights.Add(FGuid(),P.Layers);
        Target->Import(FGuid::NewGuid(),0,0,P.Physical.X,P.Physical.Y,1,63,Heights,*(Directory/TEXT("height.r16")),Weights,ELandscapeImportAlphamapType::Additive,{});
    }
    else {Target->Modify();Target->SetActorLocation(P.Location);Target->SetActorScale3D(P.Scale);}
    auto* Info=Target->GetLandscapeInfo();const auto* Base=Target->GetEditLayerConst(0);
    if(!Info||!Base||Info->Layers.Num()!=P.Layers.Num())return Error(OutError,TEXT("Unexpected Landscape edit/paint layer set"));
    TSet<ULandscapeLayerInfoObject*> DirtyLayers;TArray<uint8> Packed;Packed.SetNumZeroed(P.Height.Num()*P.Layers.Num());
    for(const auto& Layer:P.Layers)
    {
        const int32 Index=Info->GetLayerInfoIndex(Layer.LayerInfo);if(Index==INDEX_NONE)return Error(OutError,TEXT("Paint target missing"));DirtyLayers.Add(Layer.LayerInfo);
        for(int32 I=0;I<P.Height.Num();++I)Packed[I*P.Layers.Num()+Index]=Layer.LayerData[I];
    }
    {
        FLandscapeEditDataInterface Edit(Info,Base->GetGuid());
        if(!Replace)Edit.SetHeightData(0,0,P.Physical.X,P.Physical.Y,P.Height.GetData(),P.Physical.X+1,true);
        Edit.SetAlphaData(DirtyLayers,0,0,P.Physical.X,P.Physical.Y,Packed.GetData(),(P.Physical.X+1)*P.Layers.Num());Edit.Flush();
    }
    Target->ForceLayersFullUpdate();Target->PostEditMove(true);
    FLandscapeEditDataInterface Read(Info,FGuid());TArray<uint16> Height;Height.SetNumZeroed(P.Height.Num());Read.GetHeightDataFast(0,0,P.Physical.X,P.Physical.Y,Height.GetData(),P.Physical.X+1);
    if(Height!=P.Height)return Error(OutError,TEXT("Landscape height readback mismatch"));
    TArray<uint8> Weight;Weight.SetNumZeroed(P.Height.Num());
    for(const auto& Layer:P.Layers){Read.GetWeightDataFast(Layer.LayerInfo,0,0,P.Physical.X,P.Physical.Y,Weight.GetData(),P.Physical.X+1);if(Weight!=Layer.LayerData)return Error(OutError,TEXT("Landscape weight readback mismatch: ")+Layer.LayerName.ToString());}
    if(Replace)
    {
        TArray<AActor*> Attached;Old->GetAttachedActors(Attached);for(auto* A:Attached){A->Modify();A->AttachToActor(Target,FAttachmentTransformRules::KeepWorldTransform);}
        Old->Modify();if(!W.EditorDestroyActor(Old,true))return Error(OutError,TEXT("Could not retire old Landscape after verified replacement"));
    }
    Target->SetActorLabel(TEXT("Commander Landscape · Map Designer"));Target->MarkPackageDirty();return true;
}
bool BufferVolumes(UWorld& W,const FPrepared& P,FString& OutError)
{
    double Min=DBL_MAX,Max=-DBL_MAX;for(float H:P.LogicalHeights){Min=FMath::Min(Min,double(H));Max=FMath::Max(Max,double(H));}
    const double Low=Min-10000,High=Max+10000;const FVector2D LogicalMax=P.Origin+FVector2D(P.Logical)*300.,PhysicalMax=P.Origin+FVector2D(P.Physical)*300.;
    for(const FName Name:{FName(TEXT("GuLiMapDesignBuffer_X")),FName(TEXT("GuLiMapDesignBuffer_Y"))})
    {
        ANavModifierVolume* Existing=nullptr;for(TActorIterator<ANavModifierVolume> It(&W);It;++It)if(It->Tags.Contains(Name)){if(Existing)return Error(OutError,TEXT("Duplicate owned design buffer volume"));Existing=*It;}
        const bool X=Name==FName(TEXT("GuLiMapDesignBuffer_X"));const double Width=X?PhysicalMax.X-LogicalMax.X:LogicalMax.X-P.Origin.X,Depth=X?PhysicalMax.Y-P.Origin.Y:PhysicalMax.Y-LogicalMax.Y;
        if(Width<=.001||Depth<=.001){if(Existing){Existing->Modify();W.EditorDestroyActor(Existing,true);}continue;}
        if(!Existing){FActorSpawnParameters Params;Params.OverrideLevel=W.PersistentLevel;Params.ObjectFlags|=RF_Transactional;Existing=W.SpawnActor<ANavModifierVolume>(Params);if(!Existing)return Error(OutError,TEXT("Could not create edge navigation buffer"));Existing->Tags.Add(Name);}
        Existing->Modify();Existing->SetActorLabel(Name.ToString());Existing->SetFolderPath(TEXT("GuLi/MapDesigner"));Existing->SetActorScale3D(FVector::OneVector);
        const double CX=X?(LogicalMax.X+PhysicalMax.X)*.5:(P.Origin.X+LogicalMax.X)*.5,CY=X?(P.Origin.Y+PhysicalMax.Y)*.5:(LogicalMax.Y+PhysicalMax.Y)*.5;
        Existing->SetActorLocation(FVector(CX,CY,(Low+High)*.5));Existing->SetAreaClass(UNavArea_Null::StaticClass());
        auto* Builder=NewObject<UCubeBuilder>();Builder->X=Width;Builder->Y=Depth;Builder->Z=High-Low;
        if(!Builder->Build(&W,Existing))return Error(OutError,TEXT("Could not build edge navigation buffer brush"));Existing->PostEditChange();Existing->MarkPackageDirty();
    }
    for(TActorIterator<ANavMeshBoundsVolume> It(&W);It;++It)
    {
        FVector Center,Extent;It->GetActorBounds(false,Center,Extent);if(Extent.GetMin()<=.001)continue;It->Modify();const auto S=It->GetActorScale3D();
        It->SetActorLocation(FVector((P.Origin.X+PhysicalMax.X)*.5,(P.Origin.Y+PhysicalMax.Y)*.5,(Low+High)*.5));
        It->SetActorScale3D(FVector(S.X*(PhysicalMax.X-P.Origin.X)*.5/Extent.X,S.Y*(PhysicalMax.Y-P.Origin.Y)*.5/Extent.Y,S.Z*(High-Low)*.5/Extent.Z));
        if(auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(&W))Nav->OnNavigationBoundsUpdated(*It);It->MarkPackageDirty();
    }
    return true;
}
bool Backup(UWorld& W,const FString& Directory,FString& OutError)
{
    const FString BackupRoot=FPaths::ProjectDir()/TEXT("ArtSource/MapDesigner/Backups")/FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
    TSet<UPackage*> Packages;Packages.Add(W.GetOutermost());
    for(auto* A:GEditor->GetEditorSubsystem<UGuLiMapAuthoringSubsystem>()->LoadedMarkers())Packages.Add(A->GetOutermost());
    for(auto* T:GEditor->GetEditorSubsystem<UGuLiMapAuthoringSubsystem>()->ListTypes())Packages.Add(T->GetOutermost());
    for(const TCHAR* Asset:{TEXT("/Game/GuLiStrike/Data/Navigation/DA_CommanderGroundGrid.DA_CommanderGroundGrid"),TEXT("/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap.DA_CommanderResourceMap")})if(auto* O=LoadObject<UObject>(nullptr,Asset))Packages.Add(O->GetOutermost());
    for(auto* Package:Packages)
    {
        const bool Map=Package==W.GetOutermost();const FString Name=Package->GetName();if(!Name.StartsWith(TEXT("/Game/")))continue;
        const FString Original=FPackageName::LongPackageNameToFilename(Name,Map?FPackageName::GetMapPackageExtension():FPackageName::GetAssetPackageExtension());
        if(!FPaths::FileExists(Original))continue;const FString Destination=BackupRoot/TEXT("Content")/(Name.Mid(6)+(Map?TEXT(".umap"):TEXT(".uasset")));
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Destination),true);if(IFileManager::Get().Copy(*Destination,*Original)!=COPY_OK)return Error(OutError,TEXT("Backup failed: ")+Name);
    }
    FFileHelper::SaveStringToFile(Directory,*(BackupRoot/TEXT("export-directory.txt")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);return true;
}
}

FGuLiMapDesignResult UGuLiMapDesignAuthoringLibrary::ApplyDesignImport(const FString& Directory,bool bPrepareNavigation,bool bSavePackages)
{
    FGuLiMapDesignResult R;GuLiMapDesign::FPrepared P;if(!GuLiMapDesign::Prepare(Directory,P,R.Message))return R;
    UWorld* W=GuLiMapDesign::World(R.Message);if(!W||!GuLiMapDesign::Backup(*W,Directory,R.Message))return R;
    TSet<UPackage*> BeforeDirty;for(TObjectIterator<UPackage> It;It;++It)if(It->IsDirty())BeforeDirty.Add(*It);
    bool Applied=false;TArray<UPackage*> Explicit;
    {
        const FScopedTransaction Transaction(NSLOCTEXT("GuLiMapDesign","Apply","Apply reviewed standalone map design"));W->Modify();
        Applied=[&]()
        {
            if(!GuLiMapDesign::ApplyLandscape(*W,Directory,P,R.Message)||!UGuLiGridAuthoringLibrary::ImportPreparedGrid(Directory,R.Message,false))return false;
            const auto Authoring=GEditor->GetEditorSubsystem<UGuLiMapAuthoringSubsystem>()->ApplyStandaloneSnapshot(Directory);
            if(!Authoring.bSuccess){R.Message=Authoring.Issues.IsEmpty()?TEXT("Authoring import failed"):Authoring.Issues[0].Message;return false;}
            if(!GuLiMapDesign::BufferVolumes(*W,P,R.Message))return false;
            if(bPrepareNavigation){const auto Nav=UGuLiNavigationBakeLibrary::PrepareWorldNavigation(W,false);if(!Nav.bSuccess){R.Message=Nav.Message;return false;}}
            const auto Bake=UGuLiResourceAuthoringLibrary::BakeCurrentMap(false);if(!Bake.bSuccess){R.Message=Bake.Message+TEXT(" ")+FString::Join(Bake.Issues,TEXT("; "));return false;}
            const auto Checked=UGuLiResourceAuthoringLibrary::ValidateCurrentBake();if(!Checked.bSuccess){R.Message=Checked.Message;return false;}
            const auto Snapshot=GEditor->GetEditorSubsystem<UGuLiMapAuthoringSubsystem>()->GetSnapshot();if(!Snapshot.bSuccess){R.Message=TEXT("Authoring readback failed");return false;}
            for(const TCHAR* Path:{TEXT("/Game/GuLiStrike/Data/Navigation/DA_CommanderGroundGrid.DA_CommanderGroundGrid"),TEXT("/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap.DA_CommanderResourceMap")})if(auto* O=LoadObject<UObject>(nullptr,Path))Explicit.AddUnique(O->GetOutermost());
            for(auto* T:GEditor->GetEditorSubsystem<UGuLiMapAuthoringSubsystem>()->ListTypes())if(T->GetOutermost()->IsDirty())Explicit.AddUnique(T->GetOutermost());
            Explicit.AddUnique(W->GetOutermost());return true;
        }();
    }
    if(!Applied){R.bRolledBack=GEditor->UndoTransaction(false);if(!R.bRolledBack)R.Message+=TEXT("; automatic undo failed; restore the saved backup before continuing.");return R;}
    if(bSavePackages)
    {
        for(TObjectIterator<UPackage> It;It;++It)if(It->IsDirty()&&!BeforeDirty.Contains(*It)&&It->GetName().StartsWith(TEXT("/Game/")))Explicit.AddUnique(*It);
        for(auto* Package:Explicit)
        {
            if(!Package->IsDirty())continue;const FString Name=Package->GetName();const bool Map=Package==W->GetOutermost();
            const FString File=FPackageName::LongPackageNameToFilename(Name,Map?FPackageName::GetMapPackageExtension():FPackageName::GetAssetPackageExtension());
            UObject* Asset=Map?static_cast<UObject*>(W):nullptr;if(!Asset)for(TObjectIterator<UObject> It;It;++It)if(It->GetOuter()==Package&&It->IsAsset()){Asset=*It;break;}
            if(!Asset){R.Message=TEXT("Cannot identify changed package asset: ")+Name;return R;}
            FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);if(!UPackage::SavePackage(Package,Asset,*File,Args)){R.Message=TEXT("Save failed; verified changes remain in editor: ")+File;return R;}
        }
    }
    auto Report=MakeShared<FJsonObject>();Report->SetBoolField(TEXT("saved"),bSavePackages);Report->SetBoolField(TEXT("navigation_prepared"),bPrepareNavigation);
    Report->SetNumberField(TEXT("logical_width"),P.Logical.X);Report->SetNumberField(TEXT("logical_length"),P.Logical.Y);Report->SetStringField(TEXT("map_package"),GuLiMapDesign::CanonicalMap);
    R.Json=GuLiMapDesign::Json(Report);R.bSuccess=true;R.Message=TEXT("Design imported and production data read back. Player visual/gameplay acceptance remains separate.");return R;
}
