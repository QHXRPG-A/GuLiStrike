#include "GuLiGroundMechFireEditorLibrary.h"
#include "Editor.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraScriptSource.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraLightRendererProperties.h"
#include "NiagaraRibbonRendererProperties.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "Materials/MaterialInterface.h"
#include "EdGraphSchema_Niagara.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "Serialization/JsonSerializer.h"
#include "NiagaraComponent.h"
#include "NiagaraSystemInstanceController.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraDataSetAccessor.h"
#include "Engine/StaticMesh.h"

FString UGuLiGroundMechFireEditorLibrary::ReadExplosiveImpactParticles(UNiagaraComponent* Component)
{
    if(!Component || !Component->GetAsset() || Component->GetAsset()->GetOutermost()->GetName()!=TEXT("/Game/GuLiStrike/FX/GroundMech/ExplosivePackage/NS_GroundExplosiveImpact"))return TEXT("{}");
    auto Controller=Component->GetSystemInstanceController();if(!Controller.IsValid())return TEXT("{}");
    Controller->WaitForConcurrentTickAndFinalize();const auto* Instance=Controller->GetSystemInstance_Unsafe();if(!Instance)return TEXT("{}");
    auto Root=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Emitters;
    for(const auto& E:Instance->GetEmitters())
    {
        if(!E->GetEmitterHandle().GetIsEnabled())continue;
        const auto& Data=E->GetParticleData();const auto* Buffer=Data.GetCurrentData();const int32 Count=Buffer?Buffer->GetNumInstances():0;
        const auto Position=FNiagaraDataSetAccessor<FNiagaraPosition>(Data,TEXT("Position")).GetReader(Data);
        const auto Scale=FNiagaraDataSetAccessor<FVector3f>(Data,TEXT("Scale")).GetReader(Data);
        const auto Age=FNiagaraDataSetAccessor<float>(Data,TEXT("Age")).GetReader(Data);
        const auto Life=FNiagaraDataSetAccessor<float>(Data,TEXT("Lifetime")).GetReader(Data);
        const auto Color=FNiagaraDataSetAccessor<FLinearColor>(Data,TEXT("Color")).GetReader(Data);
        const auto Orientation=FNiagaraDataSetAccessor<FQuat4f>(Data,TEXT("MeshOrientation")).GetReader(Data);
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("emitter"),E->GetEmitterHandle().GetName().ToString());
        Row->SetNumberField(TEXT("count"),Count);TArray<TSharedPtr<FJsonValue>> Particles,Meshes;
        for(int32 I=0;I<Count;++I)
        {
            auto P=MakeShared<FJsonObject>();P->SetNumberField(TEXT("index"),I);
            if(Position.IsValid())P->SetStringField(TEXT("position"),FVector(Position.Get(I)).ToString());
            if(Scale.IsValid())P->SetStringField(TEXT("scale"),FVector(Scale.Get(I)).ToString());
            if(Age.IsValid())P->SetNumberField(TEXT("age"),Age.Get(I));if(Life.IsValid())P->SetNumberField(TEXT("lifetime"),Life.Get(I));
            if(Color.IsValid())P->SetStringField(TEXT("color"),Color.Get(I).ToString());
            if(Orientation.IsValid())P->SetStringField(TEXT("orientation"),FQuat(Orientation.Get(I)).ToString());
            for(const TCHAR* Name:{TEXT("Color_2"),TEXT("Color002")})
            {
                const auto Values=FNiagaraDataSetAccessor<FLinearColor>(Data,Name).GetReader(Data);
                if(Values.IsValid())P->SetStringField(Name,Values.Get(I).ToString());
            }
            for(const TCHAR* Name:{TEXT("Dyn_2"),TEXT("Dyn002")})
            {
                const auto Values=FNiagaraDataSetAccessor<FVector4f>(Data,Name).GetReader(Data);
                if(Values.IsValid())P->SetStringField(Name,FVector4(Values.Get(I)).ToString());
            }
            for(int32 N=0;N<4;++N)
            {
                const FString Name=N?FString::Printf(TEXT("DynamicMaterialParameter%d"),N):TEXT("DynamicMaterialParameter");
                const auto Values=FNiagaraDataSetAccessor<FVector4f>(Data,FName(*Name)).GetReader(Data);
                if(Values.IsValid())P->SetStringField(Name,FVector4(Values.Get(I)).ToString());
            }
            Particles.Add(MakeShared<FJsonValueObject>(P));
        }
        if(const auto* ED=E->GetEmitterHandle().GetEmitterData())for(const auto* Renderer:ED->GetRenderers())
            if(const auto* Mesh=Cast<UNiagaraMeshRendererProperties>(Renderer))for(const auto& Item:Mesh->Meshes)if(Item.Mesh)
            {
                auto M=MakeShared<FJsonObject>();M->SetStringField(TEXT("mesh"),Item.Mesh->GetPathName());
                M->SetStringField(TEXT("mesh_scale"),Item.Scale.ToString());M->SetNumberField(TEXT("sphere_radius"),Item.Mesh->GetBounds().SphereRadius);
                M->SetStringField(TEXT("box_extent"),Item.Mesh->GetBounds().BoxExtent.ToString());M->SetStringField(TEXT("scale_binding"),Mesh->ScaleBinding.GetName().ToString());
                Meshes.Add(MakeShared<FJsonValueObject>(M));
            }
        Row->SetArrayField(TEXT("particles"),Particles);Row->SetArrayField(TEXT("meshes"),Meshes);Emitters.Add(MakeShared<FJsonValueObject>(Row));
    }
    Root->SetArrayField(TEXT("emitters"),Emitters);Root->SetBoolField(TEXT("complete"),Component->IsComplete());
    FString Result;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Result));return Result;
}

FString UGuLiGroundMechFireEditorLibrary::ConfigureExplosiveHead(UNiagaraSystem* System,bool bApply)
{
    if (!System || !GEditor || GEditor->PlayWorld || System->GetPathName()!=TEXT("/Game/GuLiStrike/FX/GroundMech/ExplosivePackage/NS_GroundExplosiveFlight.NS_GroundExplosiveFlight")) return TEXT("{\"success\":false}");
    int32 Count=0;bool OK=true;FGuid Carrier;
    for(const auto& Handle:System->GetEmitterHandles())if(Handle.GetName()==TEXT("GuLiMissileBody"))Carrier=Handle.GetId();
    if(!Carrier.IsValid())return TEXT("{\"success\":false}");
    for(auto& Handle:System->GetEmitterHandles())
    {
        if(!Handle.GetIsEnabled())continue;
        auto* Data=Handle.GetInstance().GetEmitterData();if(!Data)return TEXT("{\"success\":false}");
        if(bApply)Data->bLocalSpace=false;
        // Missile_Cluster's carrier owns the location stream; VFX_Missile's
        // smoke/fire/ribbon followers consume it in world space.
        for(auto& Event:Data->EventHandlerScriptProps)
        {if(bApply)Event.SourceEmitterID=Carrier;OK &= Event.SourceEmitterID==Carrier;}
        const FVersionedNiagaraEmitterBase Emitter(Handle.GetInstance().Emitter,Handle.GetInstance().Version);
        for(auto* Renderer:Data->GetRenderers())
        {
            if(auto* Light=Cast<UNiagaraLightRendererProperties>(Renderer)){if(bApply)Light->SetIsEnabled(false);continue;}
            FNiagaraVariableAttributeBinding* Size=nullptr;FName Name;
            if(auto* Sprite=Cast<UNiagaraSpriteRendererProperties>(Renderer)){Size=&Sprite->SpriteSizeBinding;Name=TEXT("Particles.GuLiScaledSpriteSize");}
            if(auto* Mesh=Cast<UNiagaraMeshRendererProperties>(Renderer)){Size=&Mesh->ScaleBinding;Name=TEXT("Particles.GuLiScaledMeshScale");}
            if(auto* Ribbon=Cast<UNiagaraRibbonRendererProperties>(Renderer)){Size=&Ribbon->RibbonWidthBinding;Name=TEXT("Particles.GuLiScaledRibbonWidth");}
            if(Size){if(bApply){Renderer->Modify();Size->SetValue(Name,Emitter,Renderer->GetCurrentSourceMode());}OK &= Size->GetName()==Name;++Count;}
        }
    }
    if (bApply) {System->RequestCompile(false);System->WaitForCompilationComplete();System->MarkPackageDirty();}
    return FString::Printf(TEXT("{\"success\":%s,\"sprite_renderers\":%d}"),OK && Count>0?TEXT("true"):TEXT("false"),Count);
}

namespace
{
bool IsCandidate(const UNiagaraSystem* System)
{
    return System && System->GetPathName().StartsWith(TEXT("/Game/GuLiStrike/FX/GroundMech/FiringFeedback_A3/"))
        && GEditor && !GEditor->PlayWorld;
}
bool SetInput(FVersionedNiagaraEmitterData& Data,const FString& Emitter,const TCHAR* Module,const TCHAR* Input,
              const FNiagaraTypeDefinition& Type,const FString& Value)
{
    auto* Source=Cast<UNiagaraScriptSource>(Data.GraphSource); if (!Source || !Source->NodeGraph) return false;
    TArray<UNiagaraNodeFunctionCall*> Nodes; Source->NodeGraph->GetNodesOfClass(Nodes);
    for (auto* Node:Nodes) if (Node->GetFunctionName()==Module)
    {
        Source->NodeGraph->Modify(); Node->Modify();
        const FString Name=FString(Module)+TEXT(".")+Input;
        auto& Pin=FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(*Node,FNiagaraParameterHandle(*Name),Type,FGuid(),FGuid());
        Pin.BreakAllPinLinks();
        CastChecked<UEdGraphSchema_Niagara>(Source->NodeGraph->GetSchema())->TrySetDefaultValue(Pin,Value,true);
        const FNiagaraVariable RI(Type,FName(*(TEXT("Constants.")+Emitter+TEXT(".")+Name)));
        for (UNiagaraScript* Script:{Data.EmitterSpawnScriptProps.Script,Data.EmitterUpdateScriptProps.Script,Data.SpawnScriptProps.Script,Data.UpdateScriptProps.Script})
            if (Script) Script->RapidIterationParameters.RemoveParameter(RI);
        Node->MarkNodeRequiresSynchronization(TEXT("Ground single-curve fire authoring"),true);
        return Pin.DefaultValue==Value && Pin.LinkedTo.IsEmpty();
    }
    return false;
}
FString GroundMechFireJson(const TSharedRef<FJsonObject>& R)
{ FString Text; FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Text)); return Text; }
}

FString UGuLiGroundMechFireEditorLibrary::ConfigureImpactVariant(UNiagaraSystem* System,const TArray<UMaterialInterface*>& OriginalMaterials,
    const TArray<UMaterialInterface*>& VariantMaterials,bool bApply)
{
    auto R=MakeShared<FJsonObject>(); R->SetBoolField(TEXT("success"),false);
    if (!System || !GEditor || GEditor->PlayWorld || OriginalMaterials.Num()!=VariantMaterials.Num()
        || !System->GetPathName().StartsWith(TEXT("/Game/GuLiStrike/FX/GroundMech/BlinkImpact_B1/NS_GroundMech_Impact_Adjusted"))) return GroundMechFireJson(R);
    // Preserve the original six emitters, spawn/motion/size curves and renderer
    // geometry. Only fork their material color/fade outputs and bind per-effect fade.
    if (bApply)
    {
        System->Modify();
        const FLinearColor Tint(FColor::FromHex(TEXT("EE9D58")));
        System->GetExposedParameters().SetParameterValue(FVector4f(Tint.R,Tint.G,Tint.B,1),
            FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(),TEXT("User.GroundImpactTintFade")),true);
    }
    bool OK=System->GetEmitterHandles().Num()==6; int32 Renderers=0;
    TArray<TSharedPtr<FJsonValue>> Values;
    for (auto& Handle:System->GetEmitterHandles())
    {
        auto* Data=Handle.GetInstance().GetEmitterData(); if (!Data) {OK=false;continue;}
        for (auto* Renderer:Data->GetRenderers())
        {
            ++Renderers;
            auto* Sprite=Cast<UNiagaraSpriteRendererProperties>(Renderer);
            auto* Ribbon=Cast<UNiagaraRibbonRendererProperties>(Renderer);
            if (!Sprite && !Ribbon) {OK=false;continue;}
            auto& Material=Sprite?Sprite->Material:Ribbon->Material;
            auto& Binding=Sprite?Sprite->DynamicMaterial3Binding:Ribbon->DynamicMaterial3Binding;
            const int32 Index=OriginalMaterials.IndexOfByKey(Material.Get());
            if (bApply)
            {
                Renderer->Modify();
                if (Index!=INDEX_NONE && VariantMaterials[Index]) Material=VariantMaterials[Index];
                const FVersionedNiagaraEmitterBase Emitter(Handle.GetInstance().Emitter,Handle.GetInstance().Version);
                Binding.SetValue(TEXT("User.GroundImpactTintFade"),Emitter,Sprite?Sprite->SourceMode:ENiagaraRendererSourceDataMode::Particles);
            }
            OK &= VariantMaterials.Contains(Material.Get()) && Binding.CanBindToHostParameterMap()
                && Binding.GetName()==TEXT("User.GroundImpactTintFade");
            auto V=MakeShared<FJsonObject>(); V->SetStringField(TEXT("emitter"),Handle.GetName().ToString());
            V->SetStringField(TEXT("material"),Material?Material->GetPathName():TEXT(""));
            V->SetStringField(TEXT("binding"),Binding.GetName().ToString());
            V->SetBoolField(TEXT("host_binding"),Binding.CanBindToHostParameterMap());
            Values.Add(MakeShared<FJsonValueObject>(V));
        }
    }
    if (bApply) {System->RequestCompile(false); System->WaitForCompilationComplete(); System->MarkPackageDirty();}
    R->SetArrayField(TEXT("renderers"),Values); R->SetNumberField(TEXT("emitter_count"),System->GetEmitterHandles().Num());
    R->SetBoolField(TEXT("success"),OK && Renderers==5); return GroundMechFireJson(R);
}

FString UGuLiGroundMechFireEditorLibrary::ConfigureBullet(UNiagaraSystem* System,UMaterialInterface* Material,FVector Scale,float Lifetime,bool bApply)
{
    auto R=MakeShared<FJsonObject>(); R->SetBoolField(TEXT("success"),false);
    if (!IsCandidate(System) || !Material || Scale.ContainsNaN() || Scale.GetMin()<=0 || !FMath::IsFinite(Lifetime) || Lifetime<=0) return GroundMechFireJson(R);
    // Baseline authored projectile: 12 cm wide, 90 cm long. The material uses
    // three head-widths of canvas for the curve, while its head occupies 1/3.
    const float HeadWidth=12.f*Scale.Y, Length=90.f*Scale.X;
    const FVector2D Size(HeadWidth*3.f,Length);
    bool OK=System->GetEmitterHandles().Num()==1; int32 Renderers=0;
    TArray<TSharedPtr<FJsonValue>> Inputs;
    if (bApply) System->Modify();
    for (auto& Handle:System->GetEmitterHandles())
    {
        auto* Data=Handle.GetInstance().GetEmitterData(); if (!Data) { OK=false; continue; }
        if (bApply)
        {
            Data->bLocalSpace=true; Data->CalculateBoundsMode=ENiagaraEmitterCalculateBoundMode::Fixed;
            Data->FixedBounds=FBox(FVector(-Length-10,-Size.X,-Size.X),FVector(10,Size.X,Size.X));
            const FString Name=Handle.GetName().ToString();
            OK &= SetInput(*Data,Name,TEXT("InitializeParticle"),TEXT("Sprite Size"),FNiagaraTypeDefinition::GetVec2Def(),FString::Printf(TEXT("(X=%.6f,Y=%.6f)"),Size.X,Size.Y));
            OK &= SetInput(*Data,Name,TEXT("InitializeParticle"),TEXT("Lifetime"),FNiagaraTypeDefinition::GetFloatDef(),FString::SanitizeFloat(Lifetime+.1f));
            OK &= SetInput(*Data,Name,TEXT("InitializeParticle"),TEXT("Sprite Rotation"),FNiagaraTypeDefinition::GetFloatDef(),TEXT("0.0"));
            OK &= SetInput(*Data,Name,TEXT("AddVelocity"),TEXT("Velocity"),FNiagaraTypeDefinition::GetVec3Def(),TEXT("(X=1,Y=0,Z=0)"));
        }
        for (UNiagaraRendererProperties* Renderer:Data->GetRenderers())
        {
            ++Renderers;
            auto* Sprite=Cast<UNiagaraSpriteRendererProperties>(Renderer); if (!Sprite) { OK=false; continue; }
            if (bApply)
            {
                Sprite->Modify(); Sprite->Material=Material;
                Sprite->Alignment=ENiagaraSpriteAlignment::VelocityAligned;
                Sprite->PivotInUVSpace=FVector2D(.5f,0.f); // Tip at the physical projectile, tail behind it.
                Sprite->bUseMaterialCutoutTexture=false; Sprite->CutoutTexture=nullptr;
            }
            OK &= Sprite->Material==Material && Sprite->Alignment==ENiagaraSpriteAlignment::VelocityAligned
                && Sprite->PivotInUVSpace.Equals(FVector2D(.5f,0.f));
            R->SetStringField(TEXT("material"),Sprite->Material?Sprite->Material->GetPathName():TEXT(""));
            R->SetStringField(TEXT("pivot"),Sprite->PivotInUVSpace.ToString());
        }
        if (auto* Source=Cast<UNiagaraScriptSource>(Data->GraphSource); Source && Source->NodeGraph)
            for (UEdGraphNode* Node:Source->NodeGraph->Nodes) for (auto* Pin:Node->Pins)
                if (Pin->Direction==EGPD_Input && (Pin->PinName==TEXT("InitializeParticle.Sprite Size")
                    || Pin->PinName==TEXT("InitializeParticle.Lifetime") || Pin->PinName==TEXT("AddVelocity.Velocity")
                    || Pin->PinName==TEXT("SpawnBurst_Instantaneous.Spawn Count")))
                {
                    auto V=MakeShared<FJsonObject>(); V->SetStringField(TEXT("name"),Pin->PinName.ToString());
                    V->SetStringField(TEXT("value"),Pin->DefaultValue); V->SetNumberField(TEXT("links"),Pin->LinkedTo.Num());
                    Inputs.Add(MakeShared<FJsonValueObject>(V));
                }
    }
    if (bApply) { System->RequestCompile(false); System->WaitForCompilationComplete(); System->MarkPackageDirty(); }
    R->SetNumberField(TEXT("head_width_cm"),HeadWidth); R->SetNumberField(TEXT("length_cm"),Length);
    R->SetStringField(TEXT("canvas_cm"),Size.ToString()); R->SetNumberField(TEXT("renderers"),Renderers);
    R->SetArrayField(TEXT("inputs"),Inputs); R->SetBoolField(TEXT("success"),OK && Renderers==1);
    return GroundMechFireJson(R);
}

bool UGuLiGroundMechFireEditorLibrary::ConfigureMuzzle(UNiagaraSystem* System)
{
    if (!IsCandidate(System)) return false;
    System->Modify(); bool Found=false,OK=true;
    for (auto& Handle:System->GetEmitterHandles()) if (Handle.GetName()==TEXT("NE_gun_nozzle_fire"))
    {
        auto* Data=Handle.GetInstance().GetEmitterData(); if (!Data) return false;
        // Local-space emitters already inherit the socket rotation. Feeding a
        // world-space owner axis through them would rotate the flame twice.
        OK &= SetInput(*Data,Handle.GetName().ToString(),TEXT("Multiply_VectorByFloat002"),TEXT("Vector"),
                       FNiagaraTypeDefinition::GetVec3Def(),TEXT("(X=1,Y=0,Z=0)"));
        Found=true;
    }
    System->RequestCompile(false); System->WaitForCompilationComplete(); System->MarkPackageDirty();
    return OK && Found;
}

FString UGuLiGroundMechFireEditorLibrary::ConfigurePersistentTrail(UNiagaraSystem* System,UMaterialInterface* Material,
    FVector Dimensions,float HoldSeconds,float FadeSeconds,float ProjectileLifetime,bool bApply)
{
    auto R=MakeShared<FJsonObject>(); R->SetBoolField(TEXT("success"),false);
    if (!System || !Material || !GEditor || GEditor->PlayWorld
        || (!System->GetPathName().StartsWith(TEXT("/Game/GuLiStrike/FX/GroundMech/FiringFeedback_A4/"))
            && System->GetPathName()!=TEXT("/Game/GuLiStrike/FX/GroundMech/ExplosivePackage/NS_GroundExplosiveTrail.NS_GroundExplosiveTrail"))
        || Dimensions.ContainsNaN() || Dimensions.X<=0 || Dimensions.Y<100 || Dimensions.Z<0
        || !FMath::IsFinite(HoldSeconds) || HoldSeconds<0 || !FMath::IsFinite(FadeSeconds) || FadeSeconds<=0
        || !FMath::IsFinite(ProjectileLifetime) || ProjectileLifetime<=0) return GroundMechFireJson(R);
    const float Canvas=2.f*(Dimensions.X+Dimensions.Z);
    bool OK=System->GetEmitterHandles().Num()==1; int32 Renderers=0;
    if (bApply)
    {
        System->Modify();
        auto& P=System->GetExposedParameters();
        P.SetParameterValue(FVector2f(Canvas,0),FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(),TEXT("User.TrailSize")),true);
        P.SetParameterValue(FVector4f(0,0,0,24000),FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(),TEXT("User.TrailData")),true);
        P.SetParameterValue(FVector4f(Dimensions.X,Dimensions.Z,HoldSeconds,FadeSeconds),FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(),TEXT("User.TrailStyle")),true);
        P.SetParameterValue(FVector4f(Dimensions.Y,Canvas,0,0),FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(),TEXT("User.TrailShape")),true);
        P.SetParameterValue(FVector3f(0,1,0),FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(),TEXT("User.TrailFacing")),true);
    }
    for (auto& Handle:System->GetEmitterHandles())
    {
        auto* Data=Handle.GetInstance().GetEmitterData(); if (!Data) { OK=false; continue; }
        if (bApply)
        {
            Data->bLocalSpace=true; Data->CalculateBoundsMode=ENiagaraEmitterCalculateBoundMode::Fixed;
            Data->FixedBounds=FBox(FVector(-20,-Canvas,-Canvas),FVector(Dimensions.Y+20,Canvas,Canvas));
            OK &= SetInput(*Data,Handle.GetName().ToString(),TEXT("InitializeParticle"),TEXT("Lifetime"),
                FNiagaraTypeDefinition::GetFloatDef(),FString::SanitizeFloat(ProjectileLifetime+HoldSeconds+FadeSeconds+.5f));
        }
        for (auto* Renderer:Data->GetRenderers())
        {
            ++Renderers;
            auto* Sprite=Cast<UNiagaraSpriteRendererProperties>(Renderer); if (!Sprite) { OK=false; continue; }
            if (bApply)
            {
                Sprite->Modify(); Sprite->Material=Material;
                Sprite->SourceMode=ENiagaraRendererSourceDataMode::Particles;
                Sprite->Alignment=ENiagaraSpriteAlignment::VelocityAligned;
                Sprite->PivotInUVSpace=FVector2D(.5f,1.f); // Fixed chunk start; geometry extends FORWARD.
                Sprite->FacingMode=ENiagaraSpriteFacingMode::CustomFacingVector;
                Sprite->bUseMaterialCutoutTexture=false; Sprite->CutoutTexture=nullptr;
                const FVersionedNiagaraEmitterBase Emitter(Handle.GetInstance().Emitter,Handle.GetInstance().Version);
                Sprite->SpriteSizeBinding.SetValue(TEXT("User.TrailSize"),Emitter,Sprite->SourceMode);
                Sprite->DynamicMaterialBinding.SetValue(TEXT("User.TrailData"),Emitter,Sprite->SourceMode);
                Sprite->DynamicMaterial1Binding.SetValue(TEXT("User.TrailStyle"),Emitter,Sprite->SourceMode);
                Sprite->DynamicMaterial2Binding.SetValue(TEXT("User.TrailShape"),Emitter,Sprite->SourceMode);
                Sprite->SpriteFacingBinding.SetValue(TEXT("User.TrailFacing"),Emitter,Sprite->SourceMode);
            }
            TArray<TSharedPtr<FJsonValue>> Bindings;
            for (const auto* Binding:{&Sprite->SpriteSizeBinding,&Sprite->DynamicMaterialBinding,&Sprite->DynamicMaterial1Binding,&Sprite->DynamicMaterial2Binding,&Sprite->SpriteFacingBinding})
            {
                auto V=MakeShared<FJsonObject>();
                V->SetStringField(TEXT("name"),Binding->GetName().ToString());
                V->SetBoolField(TEXT("host_binding"),Binding->CanBindToHostParameterMap());
                Bindings.Add(MakeShared<FJsonValueObject>(V));
                OK &= Binding->CanBindToHostParameterMap();
            }
            R->SetArrayField(TEXT("bindings"),Bindings);
            OK &= Sprite->Material==Material && Sprite->Alignment==ENiagaraSpriteAlignment::VelocityAligned
                && Sprite->PivotInUVSpace.Equals(FVector2D(.5f,1.f)) && Sprite->FacingMode==ENiagaraSpriteFacingMode::CustomFacingVector;
        }
    }
    if (bApply) { System->RequestCompile(false); System->WaitForCompilationComplete(); System->MarkPackageDirty(); }
    R->SetNumberField(TEXT("max_length_cm"),Dimensions.Y); R->SetNumberField(TEXT("width_cm"),Dimensions.X);
    R->SetNumberField(TEXT("hold_seconds"),HoldSeconds); R->SetNumberField(TEXT("fade_seconds"),FadeSeconds);
    R->SetNumberField(TEXT("bend_cm"),Dimensions.Z); R->SetNumberField(TEXT("renderers"),Renderers);
    R->SetBoolField(TEXT("success"),OK && Renderers==1); return GroundMechFireJson(R);
}
