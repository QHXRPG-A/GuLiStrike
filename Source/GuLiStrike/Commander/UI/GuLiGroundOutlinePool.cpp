#include "Commander/UI/GuLiGroundOutlinePool.h"
#include "Commander/UI/GuLiGroundSceneUIState.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
    constexpr int32 OpacityIndex=63,MaskCustomFloats=64;
    const FName AlphaParameter(TEXT("GuLiSceneUIOpacity")),InstanceParameter(TEXT("GuLiSceneUIInstanced"));
    struct FSlot
    {
        int32 Index=INDEX_NONE;uint64 Seen=0;
        FTransform Transform=FTransform::Identity;
        TArray<float,TInlineAllocator<MaskCustomFloats>> Custom;
    };
    struct FBatch
    {
        TWeakObjectPtr<UInstancedStaticMeshComponent> Proxy;
        TMap<FGuLiHybridObjectKey,FSlot> Slots;
        TArray<int32> Free;
        uint64 MaterialFrame=0;
        TArray<float> PrimitiveData;
    };
    // Exact source parameter revisions, evaluated once per shared material/frame.
    // This avoids rebuilding uniform buffers for every proxy on every frame.
    struct FUniformRevision
    {
        uint64 Frame=0,Revision=0,ParentRevision=0;
        TWeakObjectPtr<UMaterialInterface> Parent;
#define GULI_UNIFORM_FIELD(Name) decltype(UMaterialInstance::Name) Name;
        GULI_UNIFORM_FIELD(ScalarParameterValues)
        GULI_UNIFORM_FIELD(VectorParameterValues)
        GULI_UNIFORM_FIELD(DoubleVectorParameterValues)
        GULI_UNIFORM_FIELD(TextureParameterValues)
        GULI_UNIFORM_FIELD(TextureCollectionParameterValues)
        GULI_UNIFORM_FIELD(ParameterCollectionParameterValues)
        GULI_UNIFORM_FIELD(RuntimeVirtualTextureParameterValues)
        GULI_UNIFORM_FIELD(SparseVolumeTextureParameterValues)
        GULI_UNIFORM_FIELD(FontParameterValues)
#undef GULI_UNIFORM_FIELD
        bool Observe(const UMaterialInstance& Source)
        {
            bool Changed=false;
#define GULI_OBSERVE(Name) if(Name!=Source.Name){Name=Source.Name;Changed=true;}
            GULI_OBSERVE(ScalarParameterValues)
            GULI_OBSERVE(VectorParameterValues)
            GULI_OBSERVE(DoubleVectorParameterValues)
            GULI_OBSERVE(TextureParameterValues)
            GULI_OBSERVE(TextureCollectionParameterValues)
            GULI_OBSERVE(ParameterCollectionParameterValues)
            GULI_OBSERVE(RuntimeVirtualTextureParameterValues)
            GULI_OBSERVE(SparseVolumeTextureParameterValues)
            GULI_OBSERVE(FontParameterValues)
#undef GULI_OBSERVE
            return Changed;
        }
    };
    struct FMaterialBinding
    {
        TWeakObjectPtr<UMaterialInterface> Source;
        uint64 Revision=0;
    };
    void SyncPrimitiveData(const UMeshComponent& Source,UMeshComponent& Proxy,TArray<float>& Cached)
    {
        const auto& Data=Source.GetCustomPrimitiveData().Data;
        if(Data==Cached)return;
        for(int32 I=0;I<FMath::Max(Data.Num(),Cached.Num());++I)
            Proxy.SetCustomPrimitiveDataFloat(I,Data.IsValidIndex(I)?Data[I]:0);
        Cached=Data;
    }
    struct FActorMesh
    {
        TWeakObjectPtr<UMeshComponent> Proxy;
        uint64 Seen=0;
        FGuLiHybridObjectKey Life;
        TArray<float> PrimitiveData;
    };
    FTransform HiddenTransform()
    {FTransform Result;Result.SetScale3D(FVector::ZeroVector);return Result;}
}
struct FGuLiGroundOutlineStorage
{
    TMap<TWeakObjectPtr<UInstancedStaticMeshComponent>,FBatch> Batches;
    TMap<TWeakObjectPtr<UMeshComponent>,FActorMesh> ActorMeshes;
    TMap<TWeakObjectPtr<UMaterialInterface>,FUniformRevision> Uniforms;
    TMap<TWeakObjectPtr<UMeshComponent>,TArray<FMaterialBinding>> MaterialBindings;
    uint64 Revision(UMaterialInterface* Source,uint64 Frame)
    {
        if(!Source)return 0;
        auto* Instance=Cast<UMaterialInstance>(Source);
        if(!Instance)return 1;
        // Recurse before acquiring the map reference: insertion can reallocate.
        const uint64 Parent=Revision(Instance->Parent,Frame);
        auto& Entry=Uniforms.FindOrAdd(Source);
        if(Entry.Frame!=Frame)
        {
            const bool Changed=Entry.Observe(*Instance);
            if(!Entry.Revision||Changed||Entry.Parent!=Instance->Parent||Entry.ParentRevision!=Parent)++Entry.Revision;
            Entry.Frame=Frame;Entry.Parent=Instance->Parent;Entry.ParentRevision=Parent;
        }
        return Entry.Revision;
    }
};
UGuLiGroundOutlinePool::UGuLiGroundOutlinePool():Storage(MakeShared<FGuLiGroundOutlineStorage>()){}
UGuLiGroundOutlinePool::~UGuLiGroundOutlinePool()=default;
void UGuLiGroundOutlinePool::Initialize(APlayerController* PC)
{
    Shutdown();if(!PC||!PC->GetWorld())return;
    for(const auto& Binding:GetDefault<UGuLiGroundSceneUISettings>()->MaskMaterials)
        if(auto* Mask=Binding.Mask.LoadSynchronous())Masks.Add(FName(*Binding.Source.ToSoftObjectPath().ToString()),Mask);
    FActorSpawnParameters Params;Params.Owner=PC;Params.ObjectFlags=RF_Transient;
    Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Owner=PC->GetWorld()->SpawnActor<AActor>(AActor::StaticClass(),FTransform::Identity,Params);
    if(!Owner)return;
    Owner->Tags.Add(TEXT("GuLiSceneUIMaskProxy"));Owner->SetActorEnableCollision(false);Owner->SetActorTickEnabled(false);
    auto* Root=NewObject<USceneComponent>(Owner,NAME_None,RF_Transient);
    Owner->SetRootComponent(Root);Owner->AddInstanceComponent(Root);Root->RegisterComponent();
}
void UGuLiGroundOutlinePool::Shutdown()
{
    if(ActiveCapture.IsValid())ActiveCapture->ClearShowOnlyComponents();ActiveCapture.Reset();
    if(Owner)Owner->Destroy();Owner=nullptr;
    Storage->Batches.Reset();Storage->ActorMeshes.Reset();Storage->Uniforms.Reset();Storage->MaterialBindings.Reset();Masks.Reset();MissingMaterials.Reset();
    ActiveInstances=ActiveActorMeshes=0;
}
void UGuLiGroundOutlinePool::Configure(UMeshComponent& C)
{
    C.SetupAttachment(Owner->GetRootComponent());C.SetMobility(EComponentMobility::Movable);
    C.SetCollisionEnabled(ECollisionEnabled::NoCollision);C.SetCanEverAffectNavigation(false);
    C.SetCastShadow(false);C.SetAffectDistanceFieldLighting(false);C.SetAffectDynamicIndirectLighting(false);
    C.SetVisibleInRayTracing(false);C.SetReceivesDecals(false);C.SetVisibleInSceneCaptureOnly(true);
    C.SetOverlayMaterial(nullptr);C.bNeverDistanceCull=true;C.bAllowCullDistanceVolume=false;
    C.PrimaryComponentTick.bCanEverTick=false;Owner->AddInstanceComponent(&C);C.RegisterComponent();
    if(ActiveCapture.IsValid())ActiveCapture->ShowOnlyComponent(&C);
}
bool UGuLiGroundOutlinePool::SyncMaterials(UMeshComponent& Source,UMeshComponent& Proxy,bool Instanced,float Opacity)
{
    bool Valid=true;
    auto& MaterialBindings=Storage->MaterialBindings.FindOrAdd(&Proxy);
    MaterialBindings.SetNum(Source.GetNumMaterials());
    for(int32 Index=0;Index<Source.GetNumMaterials();++Index)
    {
        UMaterialInterface* Original=Source.GetMaterial(Index);
        UMaterialInterface* Base=Original;
        while(auto* Dynamic=Cast<UMaterialInstanceDynamic>(Base))Base=Dynamic->Parent;
        const auto* Found=Base?Masks.Find(FName(*Base->GetPathName())):nullptr;
        if(!Found||!*Found)
        {
            const FName Name=Base?FName(*Base->GetPathName()):NAME_None;
            if(!MissingMaterials.Contains(Name))
            {MissingMaterials.Add(Name);UE_LOG(LogTemp,Error,TEXT("Ground SceneUI mask material missing for %s"),*Name.ToString());}
            Valid=false;continue;
        }
        auto* MID=Cast<UMaterialInstanceDynamic>(Proxy.GetMaterial(Index));
        const bool NewMaterial=!MID||MID->Parent!=*Found;
        if(NewMaterial)
        {MID=UMaterialInstanceDynamic::Create(*Found,&Proxy);Proxy.SetMaterial(Index,MID);}
        // Includes source texture and WPO parameters. Base-color/lighting outputs
        // are replaced only in the capture material, never in the source material.
        auto& Binding=MaterialBindings[Index];const uint64 Revision=Storage->Revision(Original,Frame);
        if(NewMaterial||Binding.Source!=Original||Binding.Revision!=Revision)
        {
            MID->CopyMaterialUniformParameters(Original);
            Binding.Source=Original;Binding.Revision=Revision;
        }
        MID->SetScalarParameterValue(InstanceParameter,Instanced?1.f:0.f);
        MID->SetScalarParameterValue(AlphaParameter,Opacity);
    }
    return Valid;
}
void UGuLiGroundOutlinePool::Begin(USceneCaptureComponent2D& Capture)
{
    ++Frame;ActiveInstances=ActiveActorMeshes=0;
    if(ActiveCapture.Get()!=&Capture)
    {
        ActiveCapture=&Capture;Capture.ClearShowOnlyComponents();
        for(const auto& Pair:Storage->Batches)if(Pair.Value.Proxy.IsValid())Capture.ShowOnlyComponent(Pair.Value.Proxy.Get());
        for(const auto& Pair:Storage->ActorMeshes)if(Pair.Value.Proxy.IsValid())Capture.ShowOnlyComponent(Pair.Value.Proxy.Get());
    }
}
void UGuLiGroundOutlinePool::SubmitInstance(const FGuLiHybridObjectKey& Key,UInstancedStaticMeshComponent& Source,int32 Index,float Opacity)
{
    if(!Owner||!Source.GetStaticMesh()||!Source.PerInstanceSMData.IsValidIndex(Index)||Source.NumCustomDataFloats>OpacityIndex)return;
    FTransform Transform;if(!Source.GetInstanceTransform(Index,Transform,true)||Transform.GetScale3D().IsNearlyZero())return;
    auto& Batch=Storage->Batches.FindOrAdd(&Source);auto* Proxy=Batch.Proxy.Get();
    if(!Proxy||Proxy->GetStaticMesh()!=Source.GetStaticMesh())
    {
        if(Proxy)Proxy->DestroyComponent();Batch=FBatch{};
        Proxy=NewObject<UInstancedStaticMeshComponent>(Owner,NAME_None,RF_Transient);Batch.Proxy=Proxy;
        Proxy->SetStaticMesh(Source.GetStaticMesh());Proxy->SetNumCustomDataFloats(MaskCustomFloats);
        Proxy->SetCullDistances(0,0);Configure(*Proxy);
    }
    if(Proxy->MinLOD!=Source.MinLOD||Proxy->bOverrideMinLOD!=Source.bOverrideMinLOD)
    {Proxy->MinLOD=Source.MinLOD;Proxy->bOverrideMinLOD=Source.bOverrideMinLOD;Proxy->MarkRenderStateDirty();}
    if(Proxy->GetForcedLodModel()!=Source.GetForcedLodModel())Proxy->SetForcedLodModel(Source.GetForcedLodModel());
    if(Batch.MaterialFrame!=Frame)
    {
        Batch.MaterialFrame=Frame;
        SyncPrimitiveData(Source,*Proxy,Batch.PrimitiveData);
        Proxy->SetVisibility(SyncMaterials(Source,*Proxy,true,1));
    }
    if(!Proxy->IsVisible())return;
    auto& Slot=Batch.Slots.FindOrAdd(Key);Slot.Seen=Frame;
    if(Slot.Index==INDEX_NONE)
    {
        Slot.Index=Batch.Free.IsEmpty()?Proxy->AddInstance(HiddenTransform(),true):Batch.Free.Pop(EAllowShrinking::No);
        Slot.Transform=HiddenTransform();
    }
    if(!Slot.Transform.Equals(Transform,0))
    {Proxy->UpdateInstanceTransform(Slot.Index,Transform,true,false,true);Slot.Transform=Transform;++TransformWrites;}
    float Values[MaskCustomFloats]{};
    const int32 Count=Source.NumCustomDataFloats;
    if(Count>0&&Source.PerInstanceSMCustomData.IsValidIndex((Index+1)*Count-1))
        FMemory::Memcpy(Values,Source.PerInstanceSMCustomData.GetData()+Index*Count,Count*sizeof(float));
    Values[OpacityIndex]=Opacity;
    if(Slot.Custom.Num()!=MaskCustomFloats||FMemory::Memcmp(Slot.Custom.GetData(),Values,sizeof(Values)))
    {
        Slot.Custom.SetNumUninitialized(MaskCustomFloats);FMemory::Memcpy(Slot.Custom.GetData(),Values,sizeof(Values));
        Proxy->SetCustomData(Slot.Index,MakeArrayView(Values),false);++CustomDataWrites;
    }
    ++ActiveInstances;
}
void UGuLiGroundOutlinePool::SubmitActor(const FGuLiHybridObjectKey& Key,AActor& Actor,float Opacity)
{
    if(!Owner)return;TInlineComponentArray<UMeshComponent*> Meshes;Actor.GetComponents(Meshes,true);
    for(auto* Source:Meshes)
    {
        if(!Source||!Source->IsRegistered()||!Source->IsVisible()||Source->bHiddenInGame||(Source->GetOwner()&&Source->GetOwner()->IsHidden())||Cast<UInstancedStaticMeshComponent>(Source))continue;
        auto* Static=Cast<UStaticMeshComponent>(Source);auto* Skin=Cast<USkeletalMeshComponent>(Source);
        if((!Static||!Static->GetStaticMesh())&&(!Skin||!Skin->GetSkeletalMeshAsset()))continue;
        auto& Entry=Storage->ActorMeshes.FindOrAdd(Source);auto* Proxy=Entry.Proxy.Get();
        if(!Proxy)
        {
            Proxy=Skin?static_cast<UMeshComponent*>(NewObject<USkeletalMeshComponent>(Owner,NAME_None,RF_Transient))
                :static_cast<UMeshComponent*>(NewObject<UStaticMeshComponent>(Owner,NAME_None,RF_Transient));
            Entry.Proxy=Proxy;Configure(*Proxy);
        }
        if(Skin)
        {
            auto* Copy=CastChecked<USkeletalMeshComponent>(Proxy);
            if(Copy->GetSkeletalMeshAsset()!=Skin->GetSkeletalMeshAsset())Copy->SetSkeletalMeshAsset(Skin->GetSkeletalMeshAsset());
            if(Copy->LeaderPoseComponent.Get()!=Skin)Copy->SetLeaderPoseComponent(Skin,false,false);
            Copy->SetComponentTickEnabled(false);
            if(Copy->MinLodModel!=Skin->MinLodModel||Copy->bOverrideMinLod!=Skin->bOverrideMinLod)
            {Copy->MinLodModel=Skin->MinLodModel;Copy->bOverrideMinLod=Skin->bOverrideMinLod;Copy->MarkRenderStateDirty();}
            if(Copy->GetForcedLOD()!=Skin->GetForcedLOD())Copy->SetForcedLOD(Skin->GetForcedLOD());
        }
        else
        {
            auto* Copy=CastChecked<UStaticMeshComponent>(Proxy);
            if(Copy->GetStaticMesh()!=Static->GetStaticMesh())Copy->SetStaticMesh(Static->GetStaticMesh());
            if(Copy->MinLOD!=Static->MinLOD||Copy->bOverrideMinLOD!=Static->bOverrideMinLOD)
            {Copy->MinLOD=Static->MinLOD;Copy->bOverrideMinLOD=Static->bOverrideMinLOD;Copy->MarkRenderStateDirty();}
            if(Copy->GetForcedLodModel()!=Static->GetForcedLodModel())Copy->SetForcedLodModel(Static->GetForcedLodModel());
        }
        Entry.Seen=Frame;Entry.Life=Key;
        Proxy->SetVisibility(SyncMaterials(*Source,*Proxy,false,Opacity));
        if(!Proxy->GetComponentTransform().Equals(Source->GetComponentTransform(),0))
        {Proxy->SetWorldTransform(Source->GetComponentTransform());++TransformWrites;}
        SyncPrimitiveData(*Source,*Proxy,Entry.PrimitiveData);
        if(Proxy->IsVisible())++ActiveActorMeshes;
    }
}
void UGuLiGroundOutlinePool::End()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(GuLiGroundSceneUI_MaskSubmit);
    for(auto It=Storage->Uniforms.CreateIterator();It;++It)if(!It.Key().IsValid())It.RemoveCurrent();
    for(auto It=Storage->MaterialBindings.CreateIterator();It;++It)if(!It.Key().IsValid())It.RemoveCurrent();
    for(auto It=Storage->Batches.CreateIterator();It;++It)
    {
        auto& Batch=It.Value();auto* Proxy=Batch.Proxy.Get();
        if(!It.Key().IsValid()||!Proxy)
        {if(Proxy)Proxy->DestroyComponent();It.RemoveCurrent();continue;}
        for(auto Slot=Batch.Slots.CreateIterator();Slot;++Slot)if(Slot.Value().Seen!=Frame)
        {
            Proxy->UpdateInstanceTransform(Slot.Value().Index,HiddenTransform(),true,false,true);
            Proxy->SetCustomDataValue(Slot.Value().Index,OpacityIndex,0,false);
            Batch.Free.Add(Slot.Value().Index);Slot.RemoveCurrent();
        }
        if(Batch.Slots.IsEmpty())Proxy->SetVisibility(false);
    }
    for(auto It=Storage->ActorMeshes.CreateIterator();It;++It)
    {
        auto* Proxy=It.Value().Proxy.Get();
        if(!It.Key().IsValid()||!Proxy)
        {if(Proxy)Proxy->DestroyComponent();It.RemoveCurrent();continue;}
        if(It.Value().Seen!=Frame)Proxy->SetVisibility(false);
    }
}
FString UGuLiGroundOutlinePool::GetStatsJson() const
{
    int32 Allocated=0,Free=0;
    for(const auto& Pair:Storage->Batches)
    {if(Pair.Value.Proxy.IsValid())Allocated+=Pair.Value.Proxy->GetInstanceCount();Free+=Pair.Value.Free.Num();}
    return FString::Printf(TEXT("{\"batches\":%d,\"actor_meshes\":%d,\"active_instances\":%d,\"active_actor_meshes\":%d,\"allocated_slots\":%d,\"free_slots\":%d,\"transform_writes\":%llu,\"custom_data_writes\":%llu,\"missing_materials\":%d}"),
        Storage->Batches.Num(),Storage->ActorMeshes.Num(),ActiveInstances,ActiveActorMeshes,Allocated,Free,TransformWrites,CustomDataWrites,MissingMaterials.Num());
}
