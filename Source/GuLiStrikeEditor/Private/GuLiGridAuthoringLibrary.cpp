#include "GuLiGridAuthoringLibrary.h"
#include "GuLiResourceAuthoringLibrary.h"
#include "Gameplay/Navigation/Grid/GuLiGridQueries.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "GuLiNetHash.h"
#include "HAL/FileManager.h"
#include "Editor.h"
#include "GameFramework/WorldSettings.h"
#include "EngineUtils.h"
#include "NavMesh/RecastNavMesh.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "Landscape.h"
#include "LandscapeInfo.h"
#include "LandscapeEdit.h"
#include "LandscapeEditLayer.h"
#include "LandscapeComponent.h"
#include "LandscapeLayerInfoObject.h"
#include "Gameplay/Data/GuLiUnitDataSubsystem.h"
#include "Gameplay/Building/GuLiBuildingCatalog.h"
#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Components/CapsuleComponent.h"

namespace
{
UWorld* Current(FString& Error)
{
    UWorld* W=GEditor?GEditor->GetEditorWorldContext().World():nullptr;
    if (!W || GEditor->PlayWorld || W->GetOutermost()->GetName()!=TEXT("/Game/Maps/LVL_CommanderMassPrototype"))
    {Error=TEXT("Open the Commander map in the editor, outside PIE");return nullptr;}return W;
}
bool Save(UObject& Asset,FString& Error)
{
    auto* P=Asset.GetOutermost();const FString File=FPackageName::LongPackageNameToFilename(P->GetName(),FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);
    FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
    if (!UPackage::SavePackage(P,&Asset,*File,Args)) {Error=TEXT("Could not save ")+File;return false;}return true;
}
template<class T> bool MatchesRawHash(const TArray<T>& Values,const FString& Hash)
{
    const auto Signature=GuLiNet::Sha256({reinterpret_cast<const std::uint8_t*>(Values.GetData()),SIZE_T(Values.Num())*sizeof(T)});
    return FString(UTF8_TO_TCHAR(Signature.c_str())).Equals(Hash,ESearchCase::IgnoreCase);
}
template<class T> bool ReadArray(const FString& Dir,const FString& Name,const FJsonObject& Hashes,TArray<T>& Out,FString& Error)
{
    FString Hash;TArray<uint8> Bytes;
    if (FPaths::GetCleanFilename(Name)!=Name || !Hashes.TryGetStringField(Name,Hash)
        || !FFileHelper::LoadFileToArray(Bytes,*(Dir/Name)) || Bytes.Num()%sizeof(T) || Bytes.Num()>32*1024*1024)
    {Error=TEXT("Invalid prepared buffer: ")+Name;return false;}
    if (!MatchesRawHash(Bytes,Hash))
    {Error=TEXT("Prepared buffer hash mismatch: ")+Name;return false;}
    Out.SetNumUninitialized(Bytes.Num()/sizeof(T));FMemory::Memcpy(Out.GetData(),Bytes.GetData(),Bytes.Num());return true;
}
}
int32 UGuLiGridAuthoringLibrary::ImportPreparedGrid(const FString& Directory,FString& OutError,bool bSavePackage)
{
    OutError.Reset();if (!Current(OutError)) return false;
    FString Json;TSharedPtr<FJsonObject> M;
    if (!FFileHelper::LoadFileToString(Json,*(Directory/TEXT("manifest.json")))
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),M) || !M)
    {OutError=TEXT("Missing or invalid prepared terrain manifest");return false;}
    const TSharedPtr<FJsonObject>* Hashes=nullptr;const TArray<TSharedPtr<FJsonValue>>* Hierarchies=nullptr;
    FString Schema,Hash;
    if (!M->TryGetStringField(TEXT("schema"),Schema) || (Schema!=TEXT("guli-grid300/terrain-v1") && Schema!=TEXT("guli-map-design/export-v1"))
        || !M->TryGetStringField(TEXT("source_hash"),Hash) || Hash.Len()!=64 || !M->TryGetObjectField(TEXT("files"),Hashes)
        || !M->TryGetArrayField(TEXT("hierarchies"),Hierarchies) || Hierarchies->Num()>64)
    {OutError=TEXT("Prepared terrain metadata is invalid");return false;}
    auto* Candidate=NewObject<UGuLiGroundGridData>();Candidate->SourceHash=Hash;
    if (Schema==TEXT("guli-map-design/export-v1"))
    {
        const TArray<TSharedPtr<FJsonValue>>* Cells=nullptr;const TArray<TSharedPtr<FJsonValue>>* Origin=nullptr;
        if(!M->TryGetArrayField(TEXT("logical_cells"),Cells)||Cells->Num()!=2||!M->TryGetArrayField(TEXT("origin_cm"),Origin)||Origin->Num()!=2)
        {OutError=TEXT("Missing logical design rectangle");return false;}
        const double X=(*Cells)[0]->AsNumber(),Y=(*Cells)[1]->AsNumber();
        if(!FMath::IsFinite(X)||!FMath::IsFinite(Y)||X<1||Y<1||X*Y>4*1024*1024||FMath::FloorToDouble(X)!=X||FMath::FloorToDouble(Y)!=Y)
        {OutError=TEXT("Invalid logical design dimensions");return false;}
        Candidate->Cells=FIntPoint(int32(X),int32(Y));Candidate->Origin=FVector2D((*Origin)[0]->AsNumber(),(*Origin)[1]->AsNumber());Candidate->bHasAuthoredDesignBounds=true;
    }
    if (!ReadArray(Directory,TEXT("heights_cm.f32"),**Hashes,Candidate->VertexHeights,OutError)
        || !ReadArray(Directory,TEXT("walkable.u8"),**Hashes,Candidate->Walkable,OutError)
        || !ReadArray(Directory,TEXT("clearance.i32"),**Hashes,Candidate->Clearance,OutError)) return false;
    for (const auto& Value:*Hierarchies)
    {
        const auto H=Value->AsObject();FString Prefix;int32 Footprint=0;
        if (!H || !H->TryGetStringField(TEXT("prefix"),Prefix) || !H->TryGetNumberField(TEXT("footprint"),Footprint))
        {OutError=TEXT("Invalid prepared hierarchy");return false;}
        auto& D=Candidate->Hierarchies.AddDefaulted_GetRef();D.Footprint=Footprint;
        if (!ReadArray(Directory,Prefix+TEXT("_regions.i32"),**Hashes,D.Regions,OutError)
            || !ReadArray(Directory,Prefix+TEXT("_offsets.i32"),**Hashes,D.EdgeOffsets,OutError)
            || !ReadArray(Directory,Prefix+TEXT("_neighbours.i32"),**Hashes,D.Neighbours,OutError)) return false;
    }
    if (!Candidate->Validate(OutError)) return false;
    const TCHAR* Package=TEXT("/Game/GuLiStrike/Data/Navigation/DA_CommanderGroundGrid");
    auto* Existing=LoadObject<UGuLiGroundGridData>(nullptr,TEXT("/Game/GuLiStrike/Data/Navigation/DA_CommanderGroundGrid.DA_CommanderGroundGrid"));
    const bool Created=Existing==nullptr;
    if (!Existing) Existing=NewObject<UGuLiGroundGridData>(CreatePackage(Package),TEXT("DA_CommanderGroundGrid"),RF_Public|RF_Standalone|RF_Transactional);
    Existing->Modify();Existing->SourceHash=Hash;Existing->Cells=Candidate->Cells;Existing->Origin=Candidate->Origin;
    Existing->bHasAuthoredDesignBounds=Candidate->bHasAuthoredDesignBounds;
    Existing->SchemaVersion=1;Existing->CellSizeCentimeters=300.;Existing->MaximumSlopeDegrees=44.f;
    Existing->VertexHeights=MoveTemp(Candidate->VertexHeights);Existing->Walkable=MoveTemp(Candidate->Walkable);
    Existing->Clearance=MoveTemp(Candidate->Clearance);Existing->Hierarchies=MoveTemp(Candidate->Hierarchies);
    Existing->MarkPackageDirty();if (Created) FAssetRegistryModule::AssetCreated(Existing);
    return !bSavePackage || Save(*Existing,OutError);
}
int32 UGuLiGridAuthoringLibrary::ImportPreparedLandscape(const FString& Directory,FString& OutError)
{
    OutError.Reset();auto* W=Current(OutError);if (!W) return false;
    FString Json;TSharedPtr<FJsonObject> M;
    if (!FFileHelper::LoadFileToString(Json,*(Directory/TEXT("manifest.json")))
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),M) || !M)
    {OutError=TEXT("Missing terrain manifest");return false;}
    const TSharedPtr<FJsonObject>* Hashes=nullptr;FString Hash,Schema;
    if (!M->TryGetObjectField(TEXT("files"),Hashes) || !M->TryGetStringField(TEXT("source_hash"),Hash)
        || Hash.Len()!=64 || !M->TryGetStringField(TEXT("schema"),Schema) || Schema!=TEXT("guli-grid300/terrain-v1"))
    {OutError=TEXT("Invalid terrain manifest");return false;}
    TArray<uint16> Height;
    if (!ReadArray(Directory,TEXT("height_568.r16"),**Hashes,Height,OutError) || Height.Num()!=568*568) return false;
    ALandscape* Old=nullptr;ALandscape* New=nullptr;int32 Count=0;
    const FName Tag(*(TEXT("Grid300_")+Hash));
    for (TActorIterator<ALandscape> I(W);I;++I)
    {++Count;if (I->Tags.Contains(Tag)) New=*I;else Old=*I;}
    if (Count>2 || (!New && Count!=1)) {OutError=TEXT("Unexpected Landscape set; refusing replacement");return false;}
    auto* Source=Old?Old:New;if (!Source) return false;
    TArray<FLandscapeImportLayerInfo> Layers;
    for (const TCHAR* Name:{TEXT("FlatGround"),TEXT("Hills"),TEXT("Plateau"),TEXT("Rock"),TEXT("Beach"),TEXT("Water")})
    {
        const auto* Settings=Source->GetTargetLayers().Find(FName(Name));
        if (!Settings || !Settings->LayerInfoObj) {OutError=TEXT("Missing Landscape layer ")+FString(Name);return false;}
        FLandscapeImportLayerInfo Layer{FName(Name)};Layer.LayerInfo=Settings->LayerInfoObj;
        Layer.SourceFilePath=Directory/(FString(Name)+TEXT("_568.u8"));
        if (!ReadArray(Directory,FString(Name)+TEXT("_568.u8"),**Hashes,Layer.LayerData,OutError) || Layer.LayerData.Num()!=Height.Num()) return false;
        Layers.Add(MoveTemp(Layer));
    }
    const auto& Position=M->GetArrayField(TEXT("location"));const auto& Scale=M->GetArrayField(TEXT("scale"));
    if (Position.Num()!=3 || Scale.Num()!=3) {OutError=TEXT("Invalid terrain transform");return false;}
    const FVector TargetLocation(-85050,-85050,Position[2]->AsNumber()),TargetScale(300,300,Scale[2]->AsNumber());
    TArray<float> WorldHeight;
    if (!ReadArray(Directory,TEXT("heights_cm.f32"),**Hashes,WorldHeight,OutError) || WorldHeight.Num()!=Height.Num()) return false;
    for (int32 I=0;I<Height.Num();++I)
        if (!FMath::IsNearlyEqual((double(Height[I])-32768.)/128.*TargetScale.Z+TargetLocation.Z,double(WorldHeight[I]),.002))
        {OutError=TEXT("Landscape world height disagrees with the navigation payload");return false;}
    if (Old)
    {
        auto* OldInfo=Old->GetLandscapeInfo();int32 X0=0,Y0=0,X1=0,Y1=0;
        if (!OldInfo || !OldInfo->GetLandscapeExtent(X0,Y0,X1,Y1) || X0 || Y0 || X1!=4080 || Y1!=4080
            || !Old->GetActorLocation().Equals(FVector(-90000,-90000,TargetLocation.Z),.001)
            || !Old->GetActorScale3D().Equals(FVector(180000./4080,180000./4080,TargetScale.Z),.001))
        {OutError=TEXT("Original Landscape changed since capture");return false;}
        FLandscapeEditDataInterface OldEdit(OldInfo);TArray<uint16> OriginalHeight;OriginalHeight.SetNumZeroed(4081*4081);
        OldEdit.GetHeightDataFast(0,0,4080,4080,OriginalHeight.GetData(),4081);
        if (!MatchesRawHash(OriginalHeight,M->GetStringField(TEXT("source_height_raw_sha256"))))
        {OutError=TEXT("Original Landscape height changed since capture");return false;}
        const auto RawWeights=M->GetObjectField(TEXT("source_weight_raw_sha256"));TArray<uint8> OriginalWeight;OriginalWeight.SetNumZeroed(4081*4081);
        for (const auto& Layer:Layers)
        {
            OldEdit.GetWeightDataFast(Layer.LayerInfo,0,0,4080,4080,OriginalWeight.GetData(),4081);
            if (!MatchesRawHash(OriginalWeight,RawWeights->GetStringField(Layer.LayerName.ToString())))
            {OutError=TEXT("Original Landscape weights changed since capture: ")+Layer.LayerName.ToString();return false;}
        }
    }
    if (!New)
    {
        FActorSpawnParameters P;P.ObjectFlags|=RF_Transactional;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        New=W->SpawnActor<ALandscape>(ALandscape::StaticClass(),TargetLocation,FRotator::ZeroRotator,P);
        if (!New) {OutError=TEXT("Could not create replacement Landscape");return false;}
        New->SetActorScale3D(TargetScale);New->LandscapeMaterial=Old->LandscapeMaterial;
        New->LandscapeHoleMaterial=Old->LandscapeHoleMaterial;New->Tags.Add(Tag);
        // Copy presentation settings, never topology, component arrays, GUIDs or edit-layer buffers.
        for (const TCHAR* Name:{TEXT("bCastContactShadow"),TEXT("bCastDynamicShadow"),TEXT("bCastFarShadow"),
            TEXT("bCastHiddenShadow"),TEXT("CastShadow"),TEXT("bCastShadowAsTwoSided"),TEXT("bCastStaticShadow"),
            TEXT("LDMaxDrawDistance"),TEXT("RuntimeVirtualTextures"),TEXT("VirtualTextureLodBias"),
            TEXT("VirtualTextureNumLods"),TEXT("VirtualTextureRenderPassType"),TEXT("VirtualTextureRenderWithQuad"),
            TEXT("VirtualTextureRenderWithQuadHQ"),TEXT("ShadowCacheInvalidationBehavior"),TEXT("StaticLightingResolution"),
            TEXT("LODDistanceFactor"),TEXT("LOD0ScreenSize"),TEXT("LOD0DistributionSetting"),TEXT("LODDistributionSetting")})
            if (const auto* Property=FindFProperty<FProperty>(ALandscape::StaticClass(),Name))
                Property->CopyCompleteValue_InContainer(New,Old);
        New->SetActorLabel(TEXT("Commander Landscape 300 cm"));
        TMap<FGuid,TArray<uint16>> Heights;Heights.Add(FGuid(),Height);
        TMap<FGuid,TArray<FLandscapeImportLayerInfo>> Weights;Weights.Add(FGuid(),Layers);
        New->Import(FGuid::NewGuid(),0,0,567,567,1,63,Heights,*(Directory/TEXT("height_568.r16")),Weights,ELandscapeImportAlphamapType::Additive,{});
    }
    // Never remove the original until full scalar height/weight readback succeeds.
    auto* Info=New->GetLandscapeInfo();
    if (!Info || New->LandscapeComponents.Num()!=81 || !New->GetActorLocation().Equals(TargetLocation,.001)
        || !New->GetActorScale3D().Equals(TargetScale,.001))
    {OutError=TEXT("Imported Landscape dimensions differ from the prepared payload; original retained");return false;}
    if (Old)
    {
        // Import writes blend layers sequentially and balances earlier weights. Replace all
        // prepared channels together in the base edit layer so the exact normalized data survives.
        const auto* BaseLayer=New->GetEditLayerConst(0);
        if (!BaseLayer || Info->Layers.Num()!=Layers.Num())
        {OutError=TEXT("Unexpected imported Landscape edit/paint layer set");return false;}
        TArray<uint8> Packed;Packed.SetNumZeroed(Height.Num()*Layers.Num());
        TSet<ULandscapeLayerInfoObject*> DirtyLayers;
        for (const auto& Layer:Layers)
        {
            const int32 Index=Info->GetLayerInfoIndex(Layer.LayerInfo);
            if (Index==INDEX_NONE) {OutError=TEXT("Imported paint layer is missing");return false;}
            DirtyLayers.Add(Layer.LayerInfo);
            for (int32 I=0;I<Height.Num();++I) Packed[I*Layers.Num()+Index]=Layer.LayerData[I];
        }
        {
            FLandscapeEditDataInterface Write(Info,BaseLayer->GetGuid());
            Write.SetAlphaData(DirtyLayers,0,0,567,567,Packed.GetData(),568*Layers.Num());
            Write.Flush();
        }
        New->ForceLayersFullUpdate();
    }
    FLandscapeEditDataInterface Edit(Info,FGuid());TArray<uint16> ReadHeight;ReadHeight.SetNumZeroed(Height.Num());
    Edit.GetHeightDataFast(0,0,567,567,ReadHeight.GetData(),568);
    for (int32 I=0;I<Height.Num();++I)
        if (ReadHeight[I]!=Height[I])
        {OutError=FString::Printf(TEXT("Landscape height readback mismatch at (%d,%d): %u != %u; original retained"),I%568,I/568,uint32(ReadHeight[I]),uint32(Height[I]));return false;}
    TArray<uint8> ReadWeight;ReadWeight.SetNumZeroed(Height.Num());
    for (const auto& Layer:Layers)
    {
        Edit.GetWeightDataFast(Layer.LayerInfo,0,0,567,567,ReadWeight.GetData(),568);
        for (int32 I=0;I<ReadWeight.Num();++I)
            if (ReadWeight[I]!=Layer.LayerData[I])
            {OutError=FString::Printf(TEXT("Landscape weight readback mismatch: %s at (%d,%d): %u != %u; original retained"),*Layer.LayerName.ToString(),I%568,I/568,uint32(ReadWeight[I]),uint32(Layer.LayerData[I]));return false;}
    }
    if (Old)
    {
        Old->Modify();if (!W->EditorDestroyActor(Old,true)) {OutError=TEXT("Verified terrain imported, old Landscape could not be retired");return false;}
    }
    New->MarkPackageDirty();W->MarkPackageDirty();return true;
}
int32 UGuLiGridAuthoringLibrary::FinalizeMigratedResources(FString& OutError,bool bSavePackage)
{
    OutError.Reset();if (!Current(OutError)) return false;
    auto* Map=LoadObject<UGuLiResourceMapDefinition>(nullptr,TEXT("/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap.DA_CommanderResourceMap"));
    FString Source;TArray<FString> Issues;
    if (!Map || !UGuLiResourceAuthoringLibrary::CalculateCurrentSourceHash(Source,Issues))
    {OutError=TEXT("Could not hash the migrated map authoring: ")+FString::Join(Issues,TEXT("; "));return false;}
    const auto OldSource=Map->SourceHash,OldLayout=Map->LayoutHash;Map->SourceHash=Source;Map->LayoutHash=Map->CalculateLayoutHash();
    if (!Map->ValidateDefinition(OutError)) {Map->SourceHash=OldSource;Map->LayoutHash=OldLayout;return false;}
    Map->MarkPackageDirty();return !bSavePackage || Save(*Map,OutError);
}
int32 UGuLiGridAuthoringLibrary::ReadMigrationState(FString& OutJson,FString& OutError)
{
    OutError.Reset();OutJson.Reset();auto* W=Current(OutError);if (!W) return false;
    const auto* G=LoadObject<UGuLiGroundGridData>(nullptr,TEXT("/Game/GuLiStrike/Data/Navigation/DA_CommanderGroundGrid.DA_CommanderGroundGrid"));
    const auto* M=LoadObject<UGuLiResourceMapDefinition>(nullptr,TEXT("/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap.DA_CommanderResourceMap"));
    if (!G || !G->Validate(OutError) || !M || !M->ValidateDefinition(OutError)) return false;
    auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("map"),W->GetOutermost()->GetName());
    J->SetStringField(TEXT("terrain_hash"),G->SourceHash);J->SetNumberField(TEXT("cell_cm"),G->CellSizeCentimeters);
    J->SetNumberField(TEXT("cells_x"),G->Cells.X);J->SetNumberField(TEXT("cells_y"),G->Cells.Y);
    J->SetNumberField(TEXT("height_vertices"),G->VertexHeights.Num());
    int32 Landscapes=0,Components=0,Recast=0;
    for (TActorIterator<ALandscape> I(W);I;++I) {++Landscapes;Components+=I->LandscapeComponents.Num();}
    for (TActorIterator<ARecastNavMesh> I(W);I;++I) ++Recast;
    J->SetNumberField(TEXT("landscapes"),Landscapes);J->SetNumberField(TEXT("landscape_components"),Components);J->SetNumberField(TEXT("recast_actors"),Recast);
    const auto* Settings=W->GetWorldSettings();const auto* P=FindFProperty<FObjectPropertyBase>(Settings->GetClass(),TEXT("NavigationSystemConfig"));
    const auto* Config=P?P->GetObjectPropertyValue_InContainer(Settings):nullptr;
    J->SetStringField(TEXT("navigation_config"),Config?Config->GetClass()->GetPathName():TEXT(""));
    J->SetNumberField(TEXT("territories"),M->Territories.Num());J->SetNumberField(TEXT("clusters"),M->Clusters.Num());J->SetNumberField(TEXT("nodes"),M->Nodes.Num());
    int32 Amount=0;for (const auto& N:M->Nodes) Amount+=N.InitialAmount;J->SetNumberField(TEXT("total_ore"),Amount);
    TArray<FGuLiSoldierDefinition> Units;if (!UGuLiUnitDataSubsystem::ResolveAuthoredDefinitions(Units,OutError)) return false;
    const auto* Catalog=UGuLiBuildingCatalog::LoadDefaultCatalog();if (!Catalog) return false;
    const auto Dimension=[](int32 Id,int32 Size,double Source,double Scale)
    {
        auto V=MakeShared<FJsonObject>();V->SetNumberField(TEXT("id"),Id);V->SetNumberField(TEXT("footprint_cells"),Size);
        V->SetNumberField(TEXT("source_diameter_cm"),Source);V->SetNumberField(TEXT("diameter_cm"),Size*300.);V->SetNumberField(TEXT("scale"),Scale);
        return MakeShared<FJsonValueObject>(V);
    };
    TArray<TSharedPtr<FJsonValue>> U,B;
    for (const auto& D:Units) U.Add(Dimension(D.UnitTypeId,D.FootprintCells,D.SourceCollisionDiameterCm,D.GridDimensionScale));
    for (const auto& D:Catalog->Definitions) B.Add(Dimension(D.DefinitionId,D.FootprintCells,D.SourceCollisionDiameterCm,D.GridDimensionScale));
    J->SetArrayField(TEXT("units"),U);J->SetArrayField(TEXT("buildings"),B);
    J->SetNumberField(TEXT("player_capsule_diameter_cm"),GetDefault<AGuLiGroundMechCharacter>()->GetCapsuleComponent()->GetScaledCapsuleRadius()*2.);
    return FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&OutJson));
}
