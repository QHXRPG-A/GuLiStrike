#include "Gameplay/CombatEffects/GuLiCombatEffectAuthoringLibrary.h"
#if WITH_EDITOR
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeInput.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeWithDynamicPins.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraDataInterfaceArrayFloat.h"
#include "NiagaraDataInterfaceArrayInt.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraLightRendererProperties.h"
#include "EdGraphSchema_Niagara.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"

namespace
{
	bool OwnsBulletCandidate(const UNiagaraSystem* System)
	{ return System && System->GetOutermost()->GetName().StartsWith(TEXT("/Game/GuLiStrike/FX/BulletTrails/")); }
	UNiagaraNodeWithDynamicPins* BulletMapNode(UNiagaraGraph* Graph,const TCHAR* ClassName)
	{
		auto* Class=FindObject<UClass>(nullptr,ClassName); if (!Class) return nullptr;
		auto* Node=NewObject<UNiagaraNodeWithDynamicPins>(Graph,Class,NAME_None,RF_Transactional);
		Graph->AddNode(Node,false,false); Node->CreateNewGuid(); Node->PostPlacedNewNode(); Node->AllocateDefaultPins(); return Node;
	}
	UEdGraphPin* BulletMapPin(UNiagaraNodeWithDynamicPins* Node,EEdGraphPinDirection Direction,FNiagaraTypeDefinition Type,FName Name)
	{
		auto* Pin=Node->CreatePin(Direction,UEdGraphSchema_Niagara::TypeDefinitionToPinType(Type),Name);
		Node->CancelEditablePinName(FText::GetEmpty(),Pin); return Pin;
	}
}

bool UGuLiCombatEffectAuthoringLibrary::ConfigureBulletTrailSystem(UNiagaraSystem* System,bool bLights,FString& Error)
{
	if (!OwnsBulletCandidate(System) || System->GetEmitterHandles().Num()!=1)
	{ Error=TEXT("Expected one emitter in the owned BulletTrails candidate"); return false; }
	System->Modify(); auto& Params=System->GetExposedParameters();
	const FNiagaraVariable Capacity(FNiagaraTypeDefinition::GetIntDef(),TEXT("User.TrailSlotCount"));
	Params.SetParameterValue<int32>(bLights ? 6 : 256,Capacity,true);
	if (!bLights)
	{
		Params.SetParameterValue<float>(0,FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.TrailNow")),true);
		Params.SetParameterValue<float>(1200,FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.TrailMaximumLength")),true);
		Params.SetParameterValue<float>(24,FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.TrailEmission")),true);
		Params.SetParameterValue<FVector4f>(FVector4f(6,12,.06f,.18f),FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(),TEXT("User.TrailStyle")),true);
	}
	const auto& Handle=System->GetEmitterHandles()[0];
	auto* Data=Handle.GetEmitterData(); auto* Emitter=Handle.GetInstance().Emitter.Get();
	auto* Source=Data ? Cast<UNiagaraScriptSource>(Data->GraphSource) : nullptr;
	if (!Data || !Emitter || !Source || !Source->NodeGraph) { Error=TEXT("Missing candidate graph"); return false; }
	Emitter->Modify(); Data->SimTarget=bLights ? ENiagaraSimTarget::CPUSim : ENiagaraSimTarget::GPUComputeSim;
	Data->bLocalSpace=false; Data->CalculateBoundsMode=ENiagaraEmitterCalculateBoundMode::Fixed;
	Data->FixedBounds=FBox(FVector(-1500),FVector(1500));
	TArray<UNiagaraNodeFunctionCall*> Functions; Source->NodeGraph->GetNodesOfClass(Functions); int32 Bursts=0;
	for (auto* Function:Functions)
	{
		if (!Function->FunctionScript || Function->FunctionScript->GetName()!=TEXT("SpawnBurst_Instantaneous")) continue;
		auto& Pin=FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(*Function,
			FNiagaraParameterHandle(FName(Function->GetFunctionName()+TEXT(".Spawn Count"))),FNiagaraTypeDefinition::GetIntDef(),FGuid(),FGuid());
		Pin.BreakAllPinLinks();
		FNiagaraStackGraphUtilities::SetLinkedParameterValueForFunctionInput(Pin,Capacity,TSet<FNiagaraVariableBase>{Capacity});
		const FNiagaraVariable Old(FNiagaraTypeDefinition::GetIntDef(),FName(TEXT("Constants.")+Handle.GetName().ToString()+TEXT(".")+Function->GetFunctionName()+TEXT(".Spawn Count")));
		TArray<UNiagaraScript*> Scripts{System->GetSystemSpawnScript(),System->GetSystemUpdateScript()}; Data->GetScripts(Scripts);
		for (auto* Script:Scripts) if (Script) Script->RapidIterationParameters.RemoveParameter(Old);
		++Bursts;
	}
	const FVersionedNiagaraEmitterBase Versioned(Handle.GetEmitterBase(),Handle.GetInstance().Version);
	int32 Renderers=0;
	for (auto* Renderer:Data->GetRenderers())
	{
		if (auto* Sprite=Cast<UNiagaraSpriteRendererProperties>(Renderer); Sprite && !bLights)
		{
			Sprite->Modify(); Sprite->Alignment=ENiagaraSpriteAlignment::CustomAlignment;
			Sprite->FacingMode=ENiagaraSpriteFacingMode::CustomFacingVector; Sprite->SortMode=ENiagaraSortMode::None;
			Sprite->PivotInUVSpace=FVector2D(.5,.5); Sprite->bUseMaterialCutoutTexture=false; Sprite->CutoutTexture=nullptr;
			Sprite->DynamicMaterialBinding.SetValue(TEXT("Particles.DynamicMaterialParameter"),Versioned,Sprite->SourceMode);
			Sprite->DynamicMaterial1Binding.SetValue(TEXT("Particles.DynamicMaterialParameter1"),Versioned,Sprite->SourceMode);
			Sprite->DynamicMaterial2Binding.SetValue(TEXT("Particles.DynamicMaterialParameter2"),Versioned,Sprite->SourceMode);
			++Renderers;
		}
		else if (auto* Light=Cast<UNiagaraLightRendererProperties>(Renderer); Light && bLights)
		{
			Light->Modify(); Light->bUseInverseSquaredFalloff=false; Light->bAlphaScalesBrightness=true;
			Light->bAffectsTranslucency=false; Light->bAllowMegaLights=false; Light->bMegaLightsCastShadows=false;
			Light->RadiusScale=1; Light->DefaultExponent=2; Light->SpecularScale=.2; Light->DiffuseScale=1;
			Light->PositionBinding.SetValue(TEXT("Particles.BulletLightPosition"),Versioned,Light->SourceMode);
			Light->ColorBinding.SetValue(TEXT("Particles.BulletLightColor"),Versioned,Light->SourceMode);
			Light->RadiusBinding.SetValue(TEXT("Particles.BulletLightRadius"),Versioned,Light->SourceMode);
			Light->LightRenderingEnabledBinding.SetValue(TEXT("Particles.BulletLightEnabled"),Versioned,Light->SourceMode);
			Light->VolumetricScatteringBinding.SetValue(TEXT("Particles.BulletLightScattering"),Versioned,Light->SourceMode);
			++Renderers;
		}
		else { Error=TEXT("Unexpected candidate renderer"); return false; }
	}
	FPropertyChangedEvent Changed(FindFProperty<FEnumProperty>(FVersionedNiagaraEmitterData::StaticStruct(),TEXT("SimTarget")));
	Emitter->PostEditChangeVersionedProperty(Changed,Handle.GetInstance().Version);
	Source->NodeGraph->NotifyGraphChanged(); System->PostEditChange(); System->MarkPackageDirty();
	if (Bursts!=1 || Renderers!=1) { Error=TEXT("Expected exactly one burst and renderer"); return false; }
	return true;
}

bool UGuLiCombatEffectAuthoringLibrary::WireBulletTrailReader(UNiagaraSystem* System,UNiagaraScript* Script,bool bLights,FString& Error)
{
	if (!OwnsBulletCandidate(System) || !Script || Script->GetOutermost()!=System->GetOutermost())
	{ Error=TEXT("Reader is outside the owned BulletTrails candidate"); return false; }
	auto* Source=Cast<UNiagaraScriptSource>(Script->GetLatestSource());
	UNiagaraGraph* Graph=Source ? Source->NodeGraph.Get() : nullptr;
	if (!Graph) { Error=TEXT("Missing reader graph"); return false; }
	TArray<UNiagaraNodeInput*> Inputs; Graph->GetNodesOfClass(Inputs);
	TArray<UNiagaraNodeOutput*> Outputs; Graph->GetNodesOfClass(Outputs);
	auto** Input=Inputs.FindByPredicate([](auto* Node){ return Node->Input.GetType()==FNiagaraTypeDefinition::GetParameterMapDef(); });
	if (!Input || Outputs.Num()!=1) { Error=TEXT("Invalid reader parameter map"); return false; }
	Graph->Modify(); const auto Nodes=Graph->Nodes;
	for (UEdGraphNode* Node:Nodes) if (Node!=*Input && Node!=Outputs[0]) Graph->RemoveNode(Node);
	(*Input)->BreakAllNodeLinks(); Outputs[0]->BreakAllNodeLinks();
	auto* Get=BulletMapNode(Graph,TEXT("/Script/NiagaraEditor.NiagaraNodeParameterMapGet"));
	auto* Set=BulletMapNode(Graph,TEXT("/Script/NiagaraEditor.NiagaraNodeParameterMapSet"));
	if (!Get || !Set) { Error=TEXT("Cannot create reader maps"); return false; }
	bool Valid=true;
	auto Link=[&](UEdGraphPin* A,UEdGraphPin* B){ Valid=A && B && Graph->GetSchema()->TryCreateConnection(A,B) && Valid; };
	Link((*Input)->GetOutputPin(0),Get->GetInputPin(0)); Link((*Input)->GetOutputPin(0),Set->GetInputPin(0));
	Link(Set->GetOutputPin(0),Outputs[0]->GetInputPin(0));
	struct FBinding { UClass* Class; FName User, Attribute; };
	const TArray<FBinding> Bindings=bLights ? TArray<FBinding>{
		{UNiagaraDataInterfaceArrayPosition::StaticClass(),TEXT("User.BulletLightPositions"),TEXT("Particles.BulletLightPosition")},
		{UNiagaraDataInterfaceArrayColor::StaticClass(),TEXT("User.BulletLightColors"),TEXT("Particles.BulletLightColor")},
		{UNiagaraDataInterfaceArrayFloat::StaticClass(),TEXT("User.BulletLightRadii"),TEXT("Particles.BulletLightRadius")},
		{UNiagaraDataInterfaceArrayBool::StaticClass(),TEXT("User.BulletLightEnabled"),TEXT("Particles.BulletLightEnabled")}} : TArray<FBinding>{
		{UNiagaraDataInterfaceArrayPosition::StaticClass(),TEXT("User.TrailOrigins"),TEXT("Particles.TrailOrigin")},
		{UNiagaraDataInterfaceArrayFloat3::StaticClass(),TEXT("User.TrailDirections"),TEXT("Particles.TrailDirection")},
		{UNiagaraDataInterfaceArrayFloat3::StaticClass(),TEXT("User.TrailFacings"),TEXT("Particles.TrailFacing")},
		{UNiagaraDataInterfaceArrayFloat4::StaticClass(),TEXT("User.TrailTimes"),TEXT("Particles.TrailTimes")},
		{UNiagaraDataInterfaceArrayFloat::StaticClass(),TEXT("User.TrailSpeeds"),TEXT("Particles.TrailSpeed")},
		{UNiagaraDataInterfaceArrayColor::StaticClass(),TEXT("User.TrailColors"),TEXT("Particles.TrailTint")},
		{UNiagaraDataInterfaceArrayInt32::StaticClass(),TEXT("User.TrailGenerations"),TEXT("Particles.TrailGeneration")}};
	System->Modify();
	for (const auto& Binding:Bindings)
	{
		const FNiagaraTypeDefinition Type(Binding.Class); const FNiagaraVariable Parameter(Type,Binding.User);
		auto& Params=System->GetExposedParameters();
		if (Params.IndexOf(Parameter)==INDEX_NONE)
		{ Params.AddParameter(Parameter); Params.SetDataInterface(NewObject<UNiagaraDataInterface>(System,Binding.Class,NAME_None,RF_Transactional),Parameter); }
		TArray<FNiagaraFunctionSignature> Signatures; Binding.Class->GetDefaultObject<UNiagaraDataInterface>()->GetFunctionSignatures(Signatures);
		const auto* Signature=Signatures.FindByPredicate([](const auto& S){ return S.Name==TEXT("Get"); });
		if (!Signature || Signature->Outputs.Num()!=1) { Error=TEXT("Array Get signature missing"); return false; }
		auto* Node=NewObject<UNiagaraNodeFunctionCall>(Graph,NAME_None,RF_Transactional); Node->Signature=*Signature;
		Graph->AddNode(Node,false,false); Node->CreateNewGuid(); Node->PostPlacedNewNode(); Node->AllocateDefaultPins();
		Link(BulletMapPin(Get,EGPD_Output,Type,Binding.User),Node->FindPin(TEXT("Array interface"),EGPD_Input));
		Link(BulletMapPin(Get,EGPD_Output,FNiagaraTypeDefinition::GetIntDef(),TEXT("Particles.TrailSlot")),Node->FindPin(TEXT("Index"),EGPD_Input));
		Link(Node->FindPin(TEXT("Value"),EGPD_Output),BulletMapPin(Set,EGPD_Input,Signature->Outputs[0].GetType(),Binding.Attribute));
	}
	Graph->NotifyGraphChanged(); Script->MarkPackageDirty(); System->MarkPackageDirty(); FinalizeScratchPins(System);
	if (!Valid) Error=TEXT("Bullet reader pin connection failed"); return Valid;
}
#endif
