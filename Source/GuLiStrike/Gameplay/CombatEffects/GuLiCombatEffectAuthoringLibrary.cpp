#include "Gameplay/CombatEffects/GuLiCombatEffectAuthoringLibrary.h"

#if WITH_EDITOR
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "NiagaraSystemInstanceController.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraDataSet.h"
#include "NiagaraDataSetAccessor.h"
#include "Engine/World.h"
#include "NiagaraEmitter.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraRibbonRendererProperties.h"
#include "NiagaraLightRendererProperties.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "StaticMeshCompiler.h"
#include "Materials/MaterialInterface.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "NiagaraScript.h"
#include "NiagaraShared.h"
#include "NiagaraConstants.h"
#include "NiagaraScriptSource.h"
#include "NiagaraScriptVariable.h"
#include "NiagaraGraph.h"
#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceArrayFloat.h"
#include "NiagaraDataInterfaceArrayInt.h"
#include "NiagaraDataChannel.h"
#include "NiagaraDataChannel_Global.h"
#include "NiagaraNodeInput.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeWithDynamicPins.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOp.h"
#include "NiagaraNodeCustomHlsl.h"
#include "NiagaraNodeWriteDataSet.h"
#include "NiagaraNodeReadDataSet.h"
#include "EdGraphSchema_Niagara.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectHash.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

UStaticMesh* UGuLiCombatEffectAuthoringLibrary::CreateMissileMeshContainer()
{
	const TCHAR* PackageName = TEXT("/Game/GuLiStrike/FX/CommanderWeapons/SM_WM01_Missile");
	if (FPackageName::DoesPackageExist(PackageName)
		|| FindObject<UStaticMesh>(nullptr, TEXT("/Game/GuLiStrike/FX/CommanderWeapons/SM_WM01_Missile.SM_WM01_Missile"))) return nullptr;
	UPackage* Package = CreatePackage(PackageName);
	auto* Mesh = NewObject<UStaticMesh>(Package, TEXT("SM_WM01_Missile"), RF_Public | RF_Standalone | RF_Transactional);
	FAssetRegistryModule::AssetCreated(Mesh);
	Mesh->MarkPackageDirty();
	return Mesh;
}

namespace
{
	bool InScope(const UObject* Object)
	{ return Object && Object->GetOutermost()->GetName().StartsWith(TEXT("/Game/GuLiStrike/FX/CommanderWeapons/")); }
	bool IsMuzzleBatch(const UNiagaraSystem* System)
	{ return InScope(System) && System->GetName().StartsWith(TEXT("NS_MachineGunMuzzle_Batch")); }
	FName BatchName(const UNiagaraSystem* System,const TCHAR* ImpactName)
	{ FString Name(ImpactName); if (IsMuzzleBatch(System)) Name.ReplaceInline(TEXT("Impact"),TEXT("Muzzle"),ESearchCase::CaseSensitive); return FName(Name); }
	UClass* ReaderClass()
	{ return LoadClass<UNiagaraDataInterface>(nullptr, TEXT("/Script/Niagara.NiagaraDataInterfaceDataChannelRead")); }
	UNiagaraNodeWithDynamicPins* AddMapNode(UNiagaraGraph* Graph, const TCHAR* ClassPath)
	{
		UClass* Class = FindObject<UClass>(nullptr, ClassPath);
		if (!Class || !Class->IsChildOf<UNiagaraNodeWithDynamicPins>()) return nullptr;
		auto* Node = NewObject<UNiagaraNodeWithDynamicPins>(Graph, Class, NAME_None, RF_Transactional);
		Graph->AddNode(Node, false, false); Node->CreateNewGuid(); Node->PostPlacedNewNode(); Node->AllocateDefaultPins(); return Node;
	}
	UNiagaraNodeFunctionCall* AddFunction(UNiagaraGraph* Graph, FName Name)
	{
		TArray<FNiagaraFunctionSignature> Signatures;
		ReaderClass()->GetDefaultObject<UNiagaraDataInterface>()->GetFunctionSignatures(Signatures);
		for (const auto& Signature : Signatures) if (Signature.Name == Name)
		{
			auto* Node = NewObject<UNiagaraNodeFunctionCall>(Graph, NAME_None, RF_Transactional);
			Node->Signature = Signature; Graph->AddNode(Node, false, false);
			Node->CreateNewGuid(); Node->PostPlacedNewNode(); Node->AllocateDefaultPins(); return Node;
		}
		return nullptr;
	}
	UEdGraphPin* Read(UNiagaraNodeWithDynamicPins* Node, FNiagaraTypeDefinition Type, FName Name)
	{
		auto* Pin = Node->CreatePin(EGPD_Output, UEdGraphSchema_Niagara::TypeDefinitionToPinType(Type), Name);
		Node->CancelEditablePinName(FText::GetEmpty(), Pin); return Pin;
	}
	UEdGraphPin* Write(UNiagaraNodeWithDynamicPins* Node, FNiagaraTypeDefinition Type, FName Name)
	{
		auto* Pin = Node->CreatePin(EGPD_Input, UEdGraphSchema_Niagara::TypeDefinitionToPinType(Type), Name);
		Node->CancelEditablePinName(FText::GetEmpty(), Pin); return Pin;
	}
	struct FModuleGraph
	{
		UNiagaraGraph* Graph = nullptr;
		UNiagaraNodeInput* Input = nullptr;
		UNiagaraNodeOutput* Output = nullptr;
		UNiagaraNodeWithDynamicPins* Get = nullptr;
		UNiagaraNodeWithDynamicPins* Set = nullptr;
		bool bValid = true;
		bool Initialize(UNiagaraScript* Script)
		{
			auto* Source = Script ? Cast<UNiagaraScriptSource>(Script->GetLatestSource()) : nullptr;
			Graph = Source ? Source->NodeGraph : nullptr;
			if (!Graph) return false;
			TArray<UNiagaraNodeInput*> Inputs; Graph->GetNodesOfClass(Inputs);
			TArray<UNiagaraNodeOutput*> Outputs; Graph->GetNodesOfClass(Outputs);
			const auto* MapInput=Inputs.FindByPredicate([](const UNiagaraNodeInput* Node)
			{ return Node->Input.GetType()==FNiagaraTypeDefinition::GetParameterMapDef(); });
			if (!MapInput || Outputs.Num()!=1) return false;
			Input=*MapInput; Output = Outputs[0];
			// These are task-owned scratch graphs, never shared Niagara engine modules.
			Graph->Modify(); const auto Nodes = Graph->Nodes;
			for (UEdGraphNode* Node : Nodes) if (Node != Input && Node != Output) Graph->RemoveNode(Node);
			Input->BreakAllNodeLinks(); Output->BreakAllNodeLinks();
			Get = AddMapNode(Graph, TEXT("/Script/NiagaraEditor.NiagaraNodeParameterMapGet"));
			Set = AddMapNode(Graph, TEXT("/Script/NiagaraEditor.NiagaraNodeParameterMapSet"));
			if (!Get || !Set) return false;
			Link(Input->GetOutputPin(0), Get->GetInputPin(0));
			Link(Input->GetOutputPin(0), Set->GetInputPin(0));
			Link(Set->GetOutputPin(0), Output->GetInputPin(0));
			return bValid;
		}
		void Link(UEdGraphPin* A, UEdGraphPin* B)
		{ bValid = A && B && Graph->GetSchema()->TryCreateConnection(A, B) && bValid; }
		void BindReader(UNiagaraNodeFunctionCall* Function)
		{
			auto* Pin = Read(Get, FNiagaraTypeDefinition(ReaderClass()), TEXT("Emitter.GunfireReader"));
			Function->AutowireNewNode(Pin);
			if (auto* Emitter = Function->FindPin(TEXT("Emitter ID"), EGPD_Input))
			{
				auto* Struct = FindObject<UScriptStruct>(nullptr, TEXT("/Script/Niagara.NiagaraEmitterID"));
				if (!Struct) { bValid = false; return; }
				Link(Read(Get, FNiagaraTypeDefinition(Struct), TEXT("Engine.Emitter.ID")), Emitter);
			}
		}
	};
	void AddImpactTransforms(FModuleGraph& Module,UNiagaraNodeWithDynamicPins* Set,UEdGraphPin* Position,UEdGraphPin* Rotation,UEdGraphPin* Scale,const TCHAR* Prefix=TEXT("Impact"))
	{
		// Niagara's component matrices contain the component's LWC-relative origin.
		// A shared consumer has identity transform; each hit must supply those same
		// matrices to the copied coordinate-space helpers instead.
		auto* Node=NewObject<UNiagaraNodeCustomHlsl>(Module.Graph,NAME_None,RF_Transactional);
		Node->Signature.Name=FName(TEXT("GuLiImpactTransforms")+FGuid::NewGuid().ToString(EGuidFormats::Digits));
		Node->Signature.Inputs={
			{FNiagaraTypeDefinition::GetPositionDef(),TEXT("P")},{FNiagaraTypeDefinition::GetQuatDef(),TEXT("Q")},
			{FNiagaraTypeDefinition::GetVec3Def(),TEXT("S")}};
		const TArray<FName> Matrices={TEXT("LocalToWorld"),TEXT("WorldToLocal"),TEXT("LocalToWorldTransposed"),
			TEXT("WorldToLocalTransposed"),TEXT("LocalToWorldNoScale"),TEXT("WorldToLocalNoScale")};
		for (const auto& Name:Matrices) Node->Signature.Outputs.Emplace(FNiagaraTypeDefinition::GetMatrix4Def(),Name);
		for (const auto& Name:{TEXT("XAxis"),TEXT("YAxis"),TEXT("ZAxis")}) Node->Signature.Outputs.Emplace(FNiagaraTypeDefinition::GetVec3Def(),Name);
		Module.Graph->AddNode(Node,false,false); Node->CreateNewGuid(); Node->PostPlacedNewNode(); Node->AllocateDefaultPins();
		const FString Code=TEXT("float4 R=normalize(Q); float x=R.x,y=R.y,z=R.z,w=R.w; "
			"XAxis=float3(1-2*(y*y+z*z),2*(x*y+w*z),2*(x*z-w*y)); "
			"YAxis=float3(2*(x*y-w*z),1-2*(x*x+z*z),2*(y*z+w*x)); "
			"ZAxis=float3(2*(x*z+w*y),2*(y*z-w*x),1-2*(x*x+y*y)); "
			"LocalToWorld=float4x4(float4(XAxis*S.x,0),float4(YAxis*S.y,0),float4(ZAxis*S.z,0),float4(P,1)); "
			"float3 A=XAxis/S.x,B=YAxis/S.y,C=ZAxis/S.z; "
			"WorldToLocal=float4x4(float4(A.x,B.x,C.x,0),float4(A.y,B.y,C.y,0),float4(A.z,B.z,C.z,0),float4(-dot(P,A),-dot(P,B),-dot(P,C),1)); "
			"LocalToWorldTransposed=transpose(LocalToWorld); WorldToLocalTransposed=transpose(WorldToLocal); "
			"LocalToWorldNoScale=float4x4(float4(XAxis,0),float4(YAxis,0),float4(ZAxis,0),float4(P,1)); "
			"WorldToLocalNoScale=float4x4(float4(XAxis.x,YAxis.x,ZAxis.x,0),float4(XAxis.y,YAxis.y,ZAxis.y,0),float4(XAxis.z,YAxis.z,ZAxis.z,0),float4(-dot(P,XAxis),-dot(P,YAxis),-dot(P,ZAxis),1));");
		auto* Property=FindFProperty<FStrProperty>(Node->GetClass(),TEXT("CustomHlsl"));
		Property->SetPropertyValue_InContainer(Node,Code);
		Module.Link(Position,Node->FindPin(TEXT("P"),EGPD_Input)); Module.Link(Rotation,Node->FindPin(TEXT("Q"),EGPD_Input)); Module.Link(Scale,Node->FindPin(TEXT("S"),EGPD_Input));
		for (const auto& Name:Matrices) Module.Link(Node->FindPin(Name,EGPD_Output),Write(Set,FNiagaraTypeDefinition::GetMatrix4Def(),FName(FString(TEXT("Particles."))+Prefix+Name.ToString())));
		for (const auto& Name:{TEXT("XAxis"),TEXT("YAxis"),TEXT("ZAxis")}) Module.Link(Node->FindPin(Name,EGPD_Output),Write(Set,FNiagaraTypeDefinition::GetVec3Def(),FName(FString(TEXT("Particles."))+Prefix+Name)));
	}
}

bool UGuLiCombatEffectAuthoringLibrary::ConfigureGunfireChannel(UNiagaraDataChannelAsset* Asset, FString& Error)
{
	if (!InScope(Asset)) { Error = TEXT("Channel is outside CommanderWeapons"); return false; }
	Asset->Modify();
	UNiagaraDataChannel* Channel = Asset->Get();
	if (!Channel)
	{
		Channel = NewObject<UNiagaraDataChannel_Global>(Asset, TEXT("GlobalGunfire"), RF_Transactional);
		auto* Property = FindFProperty<FObjectPropertyBase>(Asset->GetClass(), TEXT("DataChannel"));
		if (!Property) { Error = TEXT("DataChannel property unavailable"); return false; }
		Property->SetObjectPropertyValue_InContainer(Asset, Channel);
	}
	auto* Variables = FindFProperty<FArrayProperty>(Channel->GetClass(), TEXT("ChannelVariables"));
	if (!Variables) { Error = TEXT("ChannelVariables property unavailable"); return false; }
	TArray<FNiagaraDataChannelVariable> Payload;
	for (const auto& Pair : TArray<TPair<FName, FNiagaraTypeDefinition>>{
		{TEXT("Position"), FNiagaraTypeDefinition::GetPositionDef()}, {TEXT("Direction"), FNiagaraTypeHelper::GetVectorDef()},
		{TEXT("Length"), FNiagaraTypeDefinition::GetFloatDef()}, {TEXT("Mode"), FNiagaraTypeDefinition::GetIntDef()},
		{TEXT("Tint"), FNiagaraTypeDefinition::GetColorDef()}, {TEXT("Lifetime"), FNiagaraTypeDefinition::GetFloatDef()},
		{TEXT("Width"), FNiagaraTypeDefinition::GetFloatDef()}, {TEXT("VisualIntensity"), FNiagaraTypeDefinition::GetFloatDef()},
		{TEXT("LightBrightness"), FNiagaraTypeDefinition::GetFloatDef()}, {TEXT("LightRadius"), FNiagaraTypeDefinition::GetFloatDef()}})
	{
		FNiagaraDataChannelVariable Var; Var.SetName(Pair.Key); Var.SetType(FNiagaraDataChannelVariable::ToDataChannelType(Pair.Value)); Payload.Add(Var);
	}
	Channel->PreEditChange(Variables); Variables->CopyCompleteValue(Variables->ContainerPtrToValuePtr<void>(Channel), &Payload);
	FPropertyChangedEvent Changed(Variables); Channel->PostEditChangeProperty(Changed); Asset->MarkPackageDirty();
	return Channel->IsValid();
}

bool UGuLiCombatEffectAuthoringLibrary::WireGunfireReader(UNiagaraSystem* System, UNiagaraDataChannelAsset* Channel,
	UNiagaraScript* EmitterSpawnScript, UNiagaraScript* EmitterUpdateScript, UNiagaraScript* ParticleSpawnScript, FString& Error)
{
	if (!InScope(System) || !InScope(Channel) || !InScope(EmitterSpawnScript) || !InScope(EmitterUpdateScript) || !InScope(ParticleSpawnScript) || !Channel->Get() || !ReaderClass())
	{ Error = TEXT("Invalid or out-of-scope NDC reader inputs"); return false; }
	System->Modify();
	// NDC spawning needs compile-time access information. A runtime User DI has no such
	// information in UE5.7 and silently produces no particles; keep the DI inside the graph.
	System->GetExposedParameters().RemoveParameter(FNiagaraVariable(FNiagaraTypeDefinition(ReaderClass()), TEXT("User.GunfireReader")));
	FModuleGraph Bind, Spawn, Init;
	if (!Bind.Initialize(EmitterSpawnScript) || !Spawn.Initialize(EmitterUpdateScript) || !Init.Initialize(ParticleSpawnScript))
	{ Error = TEXT("Expected freshly created scratch module graphs"); return false; }
	auto* ReaderInput = NewObject<UNiagaraNodeInput>(Bind.Graph, NAME_None, RF_Transactional);
	ReaderInput->Input = FNiagaraVariable(FNiagaraTypeDefinition(ReaderClass()), TEXT("GunfireReader"));
	ReaderInput->Usage = ENiagaraInputNodeUsage::Parameter;
	ReaderInput->ExposureOptions.bExposed = false;
	Bind.Graph->AddNode(ReaderInput, false, false); ReaderInput->CreateNewGuid(); ReaderInput->PostPlacedNewNode();
	auto* Reader = NewObject<UNiagaraDataInterface>(ReaderInput, ReaderClass(), TEXT("GunfireReader"), RF_Transactional);
	FindFProperty<FObjectPropertyBase>(ReaderClass(), TEXT("Channel"))->SetObjectPropertyValue_InContainer(Reader, Channel);
	FindFProperty<FBoolProperty>(ReaderClass(), TEXT("bReadCurrentFrame"))->SetPropertyValue_InContainer(Reader, false);
	FindFProperty<FBoolProperty>(ReaderClass(), TEXT("bAutoLinkToSpawningNDC"))->SetPropertyValue_InContainer(Reader, false);
	FNDCAccessContextInst Access(Channel->Get()->GetAccessContextType());
	auto* AccessProperty = FindFProperty<FStructProperty>(ReaderClass(), TEXT("AccessContext"));
	AccessProperty->CopyCompleteValue(AccessProperty->ContainerPtrToValuePtr<void>(Reader), &Access);
	FindFProperty<FObjectPropertyBase>(ReaderInput->GetClass(), TEXT("DataInterface"))->SetObjectPropertyValue_InContainer(ReaderInput, Reader);
	ReaderInput->AllocateDefaultPins();
	Bind.Link(ReaderInput->GetOutputPin(0), Write(Bind.Set, FNiagaraTypeDefinition(ReaderClass()), TEXT("Emitter.GunfireReader")));
	auto* SpawnCall = AddFunction(Spawn.Graph, TEXT("SpawnConditional"));
	auto* SpawnData = AddFunction(Init.Graph, TEXT("GetNDCSpawnData"));
	auto* ReadCall = AddFunction(Init.Graph, TEXT("Read"));
	if (!SpawnCall || !SpawnData || !ReadCall) { Error = TEXT("Installed Niagara NDC signatures unavailable"); return false; }
	for (const auto& Var : Channel->Get()->GetVariables())
	{
		const auto Type = Var.GetName() == TEXT("Position")
			? FNiagaraTypeDefinition::GetPositionDef() : FNiagaraTypeDefinition(FNiagaraTypeHelper::GetSWCStruct(Var.GetType().GetScriptStruct()));
		ReadCall->Signature.Outputs.Emplace(Type, Var.GetName());
		ReadCall->CreatePin(EGPD_Output, UEdGraphSchema_Niagara::TypeDefinitionToPinType(Type), Var.GetName());
	}
	Spawn.BindReader(SpawnCall);
	Spawn.Set->BreakAllNodeLinks(); Spawn.Graph->RemoveNode(Spawn.Set);
	Spawn.Link(Spawn.Input->GetOutputPin(0), SpawnCall->GetInputPin(0));
	Spawn.Link(SpawnCall->GetOutputPin(0), Spawn.Output->GetInputPin(0));
	Init.BindReader(SpawnData); Init.BindReader(ReadCall);
	auto* Exec = NewObject<UNiagaraNodeOp>(Init.Graph, NAME_None, RF_Transactional); Exec->OpName = TEXT("Util::ExecIndex");
	Init.Graph->AddNode(Exec, false, false); Exec->CreateNewGuid(); Exec->PostPlacedNewNode(); Exec->AllocateDefaultPins();
	Init.Link(Exec->GetOutputPin(0), SpawnData->FindPin(TEXT("Spawned Particle Exec Index"), EGPD_Input));
	Init.Link(SpawnData->GetOutputPin(0), ReadCall->GetInputPin(1));
	Init.Link(ReadCall->GetOutputPin(0), Write(Init.Set, FNiagaraTypeDefinition::GetBoolDef(), TEXT("Particles.Alive")));
	for (const auto& Var : Channel->Get()->GetVariables())
	{
		const auto Type = Var.GetName() == TEXT("Position")
			? FNiagaraTypeDefinition::GetPositionDef() : FNiagaraTypeDefinition(FNiagaraTypeHelper::GetSWCStruct(Var.GetType().GetScriptStruct()));
		auto* Pin = ReadCall->FindPin(Var.GetName(), EGPD_Output);
		Init.Link(Pin, Write(Init.Set, Type, FName(TEXT("Particles.") + Var.GetName().ToString())));
	}
	Bind.Graph->NotifyGraphChanged(); Spawn.Graph->NotifyGraphChanged(); Init.Graph->NotifyGraphChanged();
	EmitterSpawnScript->MarkPackageDirty(); EmitterUpdateScript->MarkPackageDirty(); ParticleSpawnScript->MarkPackageDirty(); System->MarkPackageDirty();
	FinalizeScratchPins(System);
	if (!Bind.bValid || !Spawn.bValid || !Init.bValid) { Error = TEXT("One or more NDC graph connections failed"); return false; }
	return true;
}

bool UGuLiCombatEffectAuthoringLibrary::FinalizeScratchPins(UNiagaraSystem* System)
{
	if (!InScope(System) && (!System || !System->GetOutermost()->GetName().StartsWith(
		TEXT("/Game/GuLiStrike/FX/WingmanFlight/"))))
	{
		if (!System || (System->GetOutermost()->GetName() != TEXT("/Game/GuLiStrike/FX/WingmanWeapons/NS_WingmanLaserPool")
			&& System->GetOutermost()->GetName() != TEXT("/Game/GuLiStrike/FX/RogueCards/NS_RogueUpgrade_Lite")
			&& !System->GetOutermost()->GetName().StartsWith(TEXT("/Game/GuLiStrike/FX/Mining/"))
			&& !System->GetOutermost()->GetName().StartsWith(TEXT("/Game/GuLiStrike/Buildings/Construction/"))
			&& !System->GetOutermost()->GetName().StartsWith(TEXT("/Game/GuLiStrike/FX/WarMachineHover/"))
			&& !System->GetOutermost()->GetName().StartsWith(TEXT("/Game/GuLiStrike/FX/WM01Missiles/")))) return false;
	}
	ForEachObjectWithOuter(System, [](UObject* Object)
	{
		auto* Node = Cast<UNiagaraNodeFunctionCall>(Object);
		if (!Node) return;
		Node->Modify();
		auto IsAdd = [](const UEdGraphPin* Pin) { return Pin->PinType.PinSubCategory == TEXT("DynamicAddPin"); };
		TArray<UEdGraphPin*> Regular, Add;
		for (UEdGraphPin* Pin : Node->Pins) (IsAdd(Pin) ? Add : Regular).Add(Pin);
		Node->Pins = MoveTemp(Regular); Node->Pins.Append(Add);
		Node->GetGraph()->NotifyGraphChanged();
	}, true);
	return true;
}

FString UGuLiCombatEffectAuthoringLibrary::GetConstructionCompileDiagnostics(UNiagaraSystem* System)
{
	if (!System || !System->GetOutermost()->GetName().StartsWith(TEXT("/Game/GuLiStrike/Buildings/Construction/")))
		return TEXT("Expected a project construction system");
	FString Result;
	ForEachObjectWithOuter(System, [&Result](UObject* Object)
	{
		if (auto* Script = Cast<UNiagaraScript>(Object))
		{
			const auto& VM = Script->GetVMExecutableData();
			if (!VM.ErrorMsg.IsEmpty()) Result += Script->GetPathName() + TEXT(": ") + VM.ErrorMsg + TEXT("\n");
			for (const auto& Event : VM.LastCompileEvents)
				Result += FString::Printf(TEXT("%s [%d] %s\n"), *Script->GetPathName(), int32(Event.Severity), *Event.Message);
		}
	}, true);
	return Result;
}

bool UGuLiCombatEffectAuthoringLibrary::WireLaserPoolReader(UNiagaraSystem* System,
	UNiagaraScript* ParticleUpdateScript, const bool bMuzzle, FString& Error)
{
	const FString Path = TEXT("/Game/GuLiStrike/FX/WingmanWeapons/NS_WingmanLaserPool");
	if (!System || (System->GetOutermost()->GetName() != Path
		&& System->GetOutermost()->GetName() != TEXT("/Game/GuLiStrike/FX/WarMachineHover/NS_WarMachineHoverPool")
		&& !System->GetOutermost()->GetName().StartsWith(TEXT("/Game/GuLiStrike/FX/WM01Missiles/NS_WM01MissileCluster_"))) || !ParticleUpdateScript
		|| ParticleUpdateScript->GetOutermost() != System->GetOutermost())
	{ Error = TEXT("Laser reader is outside its task-owned system"); return false; }
	FModuleGraph Module;
	if (!Module.Initialize(ParticleUpdateScript)) { Error = TEXT("Laser scratch graph is invalid"); return false; }
	struct FBinding { UClass* Class; FName User; FName Attribute; };
	TArray<FBinding> Bindings = {
		{UNiagaraDataInterfaceArrayPosition::StaticClass(), bMuzzle ? TEXT("User.MuzzlePositions") : TEXT("User.LaserPositions"), TEXT("Particles.Position")},
		{UNiagaraDataInterfaceArrayFloat3::StaticClass(), TEXT("User.LaserDirections"), TEXT("Particles.SpriteAlignment")},
		{UNiagaraDataInterfaceArrayFloat2::StaticClass(), bMuzzle ? TEXT("User.MuzzleSizes") : TEXT("User.LaserSizes"), TEXT("Particles.SpriteSize")},
		{UNiagaraDataInterfaceArrayColor::StaticClass(), bMuzzle ? TEXT("User.MuzzleColors") : TEXT("User.LaserColors"), TEXT("Particles.Color")}
	};
	// Hover and missile batches also use this reader. Only the laser bolt emitter receives lights.
	if (System->GetOutermost()->GetName() == Path && !bMuzzle)
	{
		Bindings.Append({
			{UNiagaraDataInterfaceArrayPosition::StaticClass(), TEXT("User.LaserLightPositions"), TEXT("Particles.LaserLightPosition")},
			{UNiagaraDataInterfaceArrayColor::StaticClass(), TEXT("User.LaserLightColors"), TEXT("Particles.LaserLightColor")},
			{UNiagaraDataInterfaceArrayFloat::StaticClass(), TEXT("User.LaserLightRadii"), TEXT("Particles.LightRadius")},
			{UNiagaraDataInterfaceArrayBool::StaticClass(), TEXT("User.LaserLightEnabled"), TEXT("Particles.LightEnabled")}
		});
		bool bBoundLight = false;
		for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			if (Handle.GetName() != TEXT("LaserBolts")) continue;
			const FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
			if (!Data) continue;
			const FVersionedNiagaraEmitterBase Emitter(Handle.GetEmitterBase(), Handle.GetInstance().Version);
			for (UNiagaraRendererProperties* Renderer : Data->GetRenderers())
			{
				auto* Light = Cast<UNiagaraLightRendererProperties>(Renderer);
				if (!Light) continue;
				Light->Modify();
				Light->PositionBinding.SetValue(TEXT("Particles.LaserLightPosition"), Emitter, Light->SourceMode);
				Light->ColorBinding.SetValue(TEXT("Particles.LaserLightColor"), Emitter, Light->SourceMode);
				Light->RadiusBinding.SetValue(TEXT("Particles.LightRadius"), Emitter, Light->SourceMode);
				Light->LightRenderingEnabledBinding.SetValue(TEXT("Particles.LightEnabled"), Emitter, Light->SourceMode);
				Light->bUseInverseSquaredFalloff = false;
				Light->bAlphaScalesBrightness = true;
				Light->bAffectsTranslucency = false;
				Light->bAllowMegaLights = false;
				Light->bMegaLightsCastShadows = false;
				Light->RadiusScale = 1.0f; Light->DefaultExponent = 2.0f;
				Light->SpecularScale = 0.2f; Light->DiffuseScale = 1.0f;
				bBoundLight = true;
			}
		}
		if (!bBoundLight) { Error = TEXT("Add the LaserBolts light renderer before wiring its pool reader"); return false; }
	}
	System->Modify();
	for (const FBinding& Binding : Bindings)
	{
		const FNiagaraTypeDefinition DIType(Binding.Class);
		const FNiagaraVariable Variable(DIType, Binding.User);
		auto& Parameters = System->GetExposedParameters();
		if (!Parameters.FindParameterOffset(Variable))
		{
			Parameters.AddParameter(Variable);
			Parameters.SetDataInterface(NewObject<UNiagaraDataInterface>(System, Binding.Class, NAME_None, RF_Transactional), Variable);
		}
		TArray<FNiagaraFunctionSignature> Signatures;
		Binding.Class->GetDefaultObject<UNiagaraDataInterface>()->GetFunctionSignatures(Signatures);
		const auto* Signature = Signatures.FindByPredicate([](const auto& S) { return S.Name == TEXT("Get"); });
		if (!Signature || Signature->Outputs.Num() != 1) { Error = TEXT("Array Get signature is unavailable"); return false; }
		auto* Node = NewObject<UNiagaraNodeFunctionCall>(Module.Graph, NAME_None, RF_Transactional);
		Node->Signature = *Signature; Module.Graph->AddNode(Node, false, false);
		Node->CreateNewGuid(); Node->PostPlacedNewNode(); Node->AllocateDefaultPins();
		Module.Link(Read(Module.Get, DIType, Binding.User), Node->FindPin(TEXT("Array interface"), EGPD_Input));
		Module.Link(Read(Module.Get, FNiagaraTypeDefinition::GetIntDef(), TEXT("Particles.LaserSlot")), Node->FindPin(TEXT("Index"), EGPD_Input));
		Module.Link(Node->FindPin(TEXT("Value"), EGPD_Output), Write(Module.Set, Signature->Outputs[0].GetType(), Binding.Attribute));
	}
	Module.Graph->NotifyGraphChanged(); ParticleUpdateScript->MarkPackageDirty(); System->MarkPackageDirty();
	FinalizeScratchPins(System);
	if (!Module.bValid) { Error = TEXT("Laser array graph connection failed"); return false; }
	return true;
}

bool UGuLiCombatEffectAuthoringLibrary::NormalizeExplosionRefractionSpace(UNiagaraSystem* System, FString& Error)
{
	Error.Reset();
	const FString Package = System ? System->GetOutermost()->GetName() : FString();
	if (!Package.StartsWith(TEXT("/Game/GuLiStrike/FX/UnitFeedback/NS_WingmanDestruction_Aerial_"))
		&& Package != TEXT("/Game/GuLiStrike/FX/WingmanWeapons/NS_WingmanGroundShockwave_Big_17"))
	{ Error = TEXT("Only the project aerial/ground explosion refraction copies are supported"); return false; }
	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		if (Handle.GetName() != TEXT("refr_mesh")) continue;
		FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
		UNiagaraEmitter* Emitter = Handle.GetInstance().Emitter.Get();
		if (!Data || !Emitter) { Error = TEXT("Missing versioned refraction emitter"); return false; }
		if (!Data->bLocalSpace) return true;
		System->Modify();
		Emitter->Modify();
		// Initialize Particle supplies the simulation-space owner position. In world space it
		// therefore stays at the explosion origin, while its authored owner scale is applied once.
		Data->bLocalSpace = false;
		FPropertyChangedEvent Changed(FindFProperty<FBoolProperty>(FVersionedNiagaraEmitterData::StaticStruct(), TEXT("bLocalSpace")));
		Emitter->PostEditChangeVersionedProperty(Changed, Handle.GetInstance().Version);
		System->MarkPackageDirty();
		return true;
	}
	Error = TEXT("The required refr_mesh emitter was not found");
	return false;
}

bool UGuLiCombatEffectAuthoringLibrary::WireRogueUpgradePoolReader(UNiagaraSystem* System,
	UNiagaraScript* ParticleUpdateScript,FString& Error)
{
	if (!System || System->GetOutermost()->GetName()!=TEXT("/Game/GuLiStrike/FX/RogueCards/NS_RogueUpgrade_Lite")
		|| !ParticleUpdateScript || ParticleUpdateScript->GetOutermost()!=System->GetOutermost())
	{ Error=TEXT("Upgrade reader is outside the task-owned system"); return false; }
	FModuleGraph Module;
	if (!Module.Initialize(ParticleUpdateScript)) { Error=TEXT("Invalid upgrade scratch graph"); return false; }
	struct FBinding { UClass* Class; FName User,Attribute; };
	const TArray<FBinding> Bindings={
		{UNiagaraDataInterfaceArrayPosition::StaticClass(),TEXT("User.UpgradePositions"),TEXT("Particles.UpgradeCenter")},
		{UNiagaraDataInterfaceArrayFloat3::StaticClass(),TEXT("User.UpgradeParameters"),TEXT("Particles.UpgradeParameters")},
		{UNiagaraDataInterfaceArrayColor::StaticClass(),TEXT("User.UpgradeColors"),TEXT("Particles.UpgradeTint")}};
	System->Modify();
	for (const auto& Binding:Bindings)
	{
		const FNiagaraTypeDefinition DIType(Binding.Class); const FNiagaraVariable Variable(DIType,Binding.User);
		auto& Parameters=System->GetExposedParameters();
		if (!Parameters.FindParameterOffset(Variable))
		{ Parameters.AddParameter(Variable);
			Parameters.SetDataInterface(NewObject<UNiagaraDataInterface>(System,Binding.Class,NAME_None,RF_Transactional),Variable); }
		TArray<FNiagaraFunctionSignature> Signatures;
		Binding.Class->GetDefaultObject<UNiagaraDataInterface>()->GetFunctionSignatures(Signatures);
		const auto* Signature=Signatures.FindByPredicate([](const auto& S) { return S.Name==TEXT("Get"); });
		if (!Signature || Signature->Outputs.Num()!=1) { Error=TEXT("Array Get signature missing"); return false; }
		auto* Node=NewObject<UNiagaraNodeFunctionCall>(Module.Graph,NAME_None,RF_Transactional);
		Node->Signature=*Signature; Module.Graph->AddNode(Node,false,false);
		Node->CreateNewGuid(); Node->PostPlacedNewNode(); Node->AllocateDefaultPins();
		Module.Link(Read(Module.Get,DIType,Binding.User),Node->FindPin(TEXT("Array interface"),EGPD_Input));
		Module.Link(Read(Module.Get,FNiagaraTypeDefinition::GetIntDef(),TEXT("Particles.UpgradeSlot")),Node->FindPin(TEXT("Index"),EGPD_Input));
		Module.Link(Node->FindPin(TEXT("Value"),EGPD_Output),Write(Module.Set,Signature->Outputs[0].GetType(),Binding.Attribute));
	}
	Module.Graph->NotifyGraphChanged(); ParticleUpdateScript->MarkPackageDirty(); System->MarkPackageDirty();
	FinalizeScratchPins(System);
	if (!Module.bValid) { Error=TEXT("Upgrade array graph connection failed"); return false; }
	return true;
}

bool UGuLiCombatEffectAuthoringLibrary::ConfigureRogueUpgradeSystem(UNiagaraSystem* System,FString& Error)
{
	if (!System || System->GetOutermost()->GetName()!=TEXT("/Game/GuLiStrike/FX/RogueCards/NS_RogueUpgrade_Lite"))
	{ Error=TEXT("Unexpected upgrade asset"); return false; }
	System->Modify();
	for (const auto& Handle:System->GetEmitterHandles())
	{
		auto* Data=Handle.GetEmitterData(); auto* Emitter=Handle.GetInstance().Emitter.Get();
		if (!Data || !Emitter) { Error=TEXT("Missing upgrade emitter data"); return false; }
		Emitter->Modify(); Data->SimTarget=ENiagaraSimTarget::GPUComputeSim; Data->bLocalSpace=false;
		Data->CalculateBoundsMode=ENiagaraEmitterCalculateBoundMode::Fixed;
		Data->FixedBounds=FBox(FVector(-500),FVector(500));
		for (auto* Renderer:Data->GetRenderers())
		{
			Renderer->Modify();
			if (auto* Mesh=Cast<UNiagaraMeshRendererProperties>(Renderer))
			{
				Mesh->SortMode=ENiagaraSortMode::None; Mesh->Meshes.SetNum(1);
				Mesh->Meshes[0].Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/GuLiStrike/FX/RogueCards/SM_UpgradeCylinder.SM_UpgradeCylinder"));
				Mesh->bOverrideMaterials=true; Mesh->OverrideMaterials.SetNum(1);
				Mesh->OverrideMaterials[0].ExplicitMat=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/GuLiStrike/FX/RogueCards/M_UpgradeGlow.M_UpgradeGlow"));
				if (!Mesh->Meshes[0].Mesh || !Mesh->OverrideMaterials[0].ExplicitMat) { Error=TEXT("Upgrade mesh/material missing"); return false; }
			}
			if (auto* Sprite=Cast<UNiagaraSpriteRendererProperties>(Renderer)) Sprite->SortMode=ENiagaraSortMode::None;
		}
		FPropertyChangedEvent Changed(FindFProperty<FEnumProperty>(FVersionedNiagaraEmitterData::StaticStruct(),TEXT("SimTarget")));
		Emitter->PostEditChangeVersionedProperty(Changed,Handle.GetInstance().Version);
	}
	System->MarkPackageDirty(); return true;
}

bool UGuLiCombatEffectAuthoringLibrary::ConfigureWarMachineHoverSystem(UNiagaraSystem* System, FString& Error)
{
	if (!System || System->GetOutermost()->GetName() != TEXT("/Game/GuLiStrike/FX/WarMachineHover/NS_WarMachineHoverPool"))
	{ Error = TEXT("Unexpected hover asset"); return false; }
	System->Modify();
	// VibeUE's generic parameter factory does not expose Niagara Position (LWC) user parameters.
	const FNiagaraVariable Camera(FNiagaraTypeDefinition::GetPositionDef(), TEXT("User.HoverCamera"));
	if (!System->GetExposedParameters().FindParameterOffset(Camera)) System->GetExposedParameters().AddParameter(Camera, true);
	for (const auto& Handle : System->GetEmitterHandles())
	{
		auto* Data = Handle.GetEmitterData(); auto* Emitter = Handle.GetInstance().Emitter.Get();
		if (!Data || !Emitter) { Error = TEXT("Missing hover emitter data"); return false; }
		Emitter->Modify(); Data->SimTarget = ENiagaraSimTarget::GPUComputeSim; Data->bLocalSpace = false;
		Data->CalculateBoundsMode = ENiagaraEmitterCalculateBoundMode::Fixed;
		// Runtime overrides this envelope with nozzle + recent-history bounds on each batch component.
		Data->FixedBounds = FBox(FVector(-1000), FVector(1000));
		for (auto* Renderer : Data->GetRenderers())
			if (auto* Sprite = Cast<UNiagaraSpriteRendererProperties>(Renderer))
			{ Sprite->Modify(); Sprite->SortMode = ENiagaraSortMode::None; }
		FPropertyChangedEvent Changed(FindFProperty<FEnumProperty>(FVersionedNiagaraEmitterData::StaticStruct(), TEXT("SimTarget")));
		Emitter->PostEditChangeVersionedProperty(Changed, Handle.GetInstance().Version);
	}
	// Refresh cached GPU-emitter flags through the public editor lifecycle.
	System->PostEditChange();
	System->MarkPackageDirty(); return true;
}

bool UGuLiCombatEffectAuthoringLibrary::ConfigureToolLaserGPUCandidate(UNiagaraSystem* System, FString& Error)
{
 if (!System || !(System->GetOutermost()->GetName().StartsWith(TEXT("/Game/GuLiStrike/FX/Mining/NS_MiningLaser_Optimized_GPU"))
  || System->GetOutermost()->GetName().StartsWith(TEXT("/Game/GuLiStrike/Buildings/Construction/NS_ConstructionLaser_Optimized_GPU"))))
 { Error=TEXT("Only tool-laser GPU review copies are supported"); return false; }
 System->Modify();
 for (const auto& H : System->GetEmitterHandles())
 {
  if (!H.GetName().ToString().StartsWith(TEXT("Beam"))) continue;
  auto* D=H.GetEmitterData(); auto* E=H.GetInstance().Emitter.Get();
  if (!D || !E) { Error=TEXT("Missing beam data"); return false; }
  E->Modify(); D->SimTarget=ENiagaraSimTarget::GPUComputeSim;
  D->CalculateBoundsMode=ENiagaraEmitterCalculateBoundMode::Fixed;
  D->FixedBounds=FBox(FVector(-30000),FVector(30000));
  FPropertyChangedEvent Changed(FindFProperty<FEnumProperty>(FVersionedNiagaraEmitterData::StaticStruct(),TEXT("SimTarget")));
  E->PostEditChangeVersionedProperty(Changed,H.GetInstance().Version);
 }
 System->PostEditChange(); System->MarkPackageDirty(); return true;
}

FString UGuLiCombatEffectAuthoringLibrary::GetWarMachineHoverCompileDiagnostics(UNiagaraSystem* System)
{
	if (!System || (System->GetOutermost()->GetName() != TEXT("/Game/GuLiStrike/FX/WarMachineHover/NS_WarMachineHoverPool")
		&& !System->GetOutermost()->GetName().StartsWith(TEXT("/Game/GuLiStrike/FX/WM01Missiles/NS_WM01MissileCluster_"))
		&& !System->GetOutermost()->GetName().StartsWith(TEXT("/Game/GuLiStrike/FX/Mining/NS_MiningLaser_"))
		&& !System->GetOutermost()->GetName().StartsWith(TEXT("/Game/GuLiStrike/Buildings/Construction/NS_ConstructionLaser_"))
		&& !System->GetOutermost()->GetName().StartsWith(TEXT("/Game/GuLiStrike/FX/CommanderWeapons/NS_MachineGun"))
		&& !System->GetOutermost()->GetName().StartsWith(TEXT("/Game/GuLiStrike/FX/CommanderWeapons/NS_WingmanGroundFlight_"))))
		return TEXT("Unexpected hover system");
	FString Result = FString::Printf(TEXT("System valid=%d ready=%d gpu=%d\n"),
		System->IsValid(), System->IsReadyToRun(), System->HasAnyGPUEmitters());
	Result += FString::Printf(TEXT("Editor needs_compile=%d outstanding_including_gpu=%d\n"),
		System->NeedsRequestCompile(), System->HasOutstandingCompilationRequests(true));
	// Ignore template versions and removed emitters retained by editor undo history.
	TArray<UNiagaraScript*> Scripts { System->GetSystemSpawnScript(), System->GetSystemUpdateScript() };
	for (const auto& Handle : System->GetEmitterHandles())
		if (const auto* Data = Handle.GetEmitterData())
		{
			Result += FString::Printf(TEXT("Emitter %s enabled=%d simulation=%d bounds=%s\n"), *Handle.GetName().ToString(), Handle.GetIsEnabled(), int32(Data->SimTarget), *Data->FixedBounds.ToString());
			Data->GetScripts(Scripts);
		}
	for (auto* Script : Scripts)
	{
		if (Script)
		{
			const auto& VM = Script->GetVMExecutableData();
			Result += FString::Printf(TEXT("%s status=%s\n"), *Script->GetPathName(),
				*StaticEnum<ENiagaraScriptCompileStatus>()->GetNameStringByValue(int64(Script->GetLastCompileStatus())));
			if (!VM.ErrorMsg.IsEmpty()) Result += TEXT("VM ERROR: ") + VM.ErrorMsg + TEXT("\n");
			for (const auto& Event : VM.LastCompileEvents)
				Result += FString::Printf(TEXT("EVENT [%d] %s\n"), int32(Event.Severity), *Event.Message);
			if (Script->GetUsage() == ENiagaraScriptUsage::ParticleGPUComputeScript)
			{
				if (const auto* Shader = Script->GetRenderThreadScript())
				{
					Result += FString::Printf(TEXT("GPU finished=%d complete=%d\n"), Shader->IsCompilationFinished(), Shader->IsShaderMapComplete());
					for (const auto& Error : Shader->GetCompileErrors()) Result += TEXT("GPU ERROR: ") + Error + TEXT("\n");
				}
				else Result += TEXT("GPU shader missing\n");
			}
		}
	}
	return Result;
}

bool UGuLiCombatEffectAuthoringLibrary::BindLaserPoolCapacity(UNiagaraSystem* System, FString& Error)
{
	if (!System || System->GetOutermost()->GetName() != TEXT("/Game/GuLiStrike/FX/WingmanWeapons/NS_WingmanLaserPool_SmallBatches"))
	{ Error = TEXT("Unexpected small-batch laser asset"); return false; }
	System->Modify();
	const FNiagaraVariable Parameter(FNiagaraTypeDefinition::GetIntDef(), TEXT("User.LaserSlotCount"));
	System->GetExposedParameters().SetParameterValue<int32>(1024, Parameter, true);
	int32 Bound = 0;
	for (const auto& Handle : System->GetEmitterHandles())
	{
		auto* Data = Handle.GetEmitterData();
		auto* Source = Data ? Cast<UNiagaraScriptSource>(Data->GraphSource) : nullptr;
		if (!Source || !Source->NodeGraph) { Error = TEXT("Laser emitter graph missing"); return false; }
		TArray<UNiagaraNodeFunctionCall*> Functions; Source->NodeGraph->GetNodesOfClass(Functions);
		for (auto* Function : Functions)
		{
			if (!Function->FunctionScript || Function->FunctionScript->GetName() != TEXT("SpawnBurst_Instantaneous")) continue;
			auto& Pin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(*Function,
				FNiagaraParameterHandle(FName(Function->GetFunctionName() + TEXT(".Spawn Count"))), FNiagaraTypeDefinition::GetIntDef(), FGuid(), FGuid());
			if (Pin.LinkedTo.IsEmpty())
				FNiagaraStackGraphUtilities::SetLinkedParameterValueForFunctionInput(Pin, Parameter, TSet<FNiagaraVariableBase>{Parameter});
			else if (Pin.LinkedTo.Num() != 1 || Pin.LinkedTo[0]->PinName != Parameter.GetName())
			{ Error = TEXT("Unexpected laser capacity override"); return false; }
			const FNiagaraVariable OldCount(FNiagaraTypeDefinition::GetIntDef(), FName(FString::Printf(
				TEXT("Constants.%s.%s.Spawn Count"), *Handle.GetName().ToString(), *Function->GetFunctionName())));
			TArray<UNiagaraScript*> Scripts{System->GetSystemSpawnScript(), System->GetSystemUpdateScript()};
			Data->GetScripts(Scripts);
			for (auto* Script : Scripts) if (Script && Script->RapidIterationParameters.RemoveParameter(OldCount))
				Script->MarkScriptAndSourceDesynchronized(TEXT("Laser batch capacity"), FGuid());
			++Bound;
		}
		Source->NodeGraph->NotifyGraphChanged();
	}
	if (Bound != 2) { Error = TEXT("Expected exactly two laser pool bursts"); return false; }
	System->RequestCompile(false); System->MarkPackageDirty();
	return true;
}

bool UGuLiCombatEffectAuthoringLibrary::ConfigureMissileClusterSystem(UNiagaraSystem* System, FString& Error)
{
	if (!System || !System->GetOutermost()->GetName().StartsWith(TEXT("/Game/GuLiStrike/FX/WM01Missiles/NS_WM01MissileCluster_")))
	{ Error = TEXT("Unexpected missile cluster asset"); return false; }
	System->Modify();
	const FNiagaraVariable Contract(FNiagaraTypeDefinition::GetIntDef(), TEXT("User.MissileContractVersion"));
	System->GetExposedParameters().SetParameterValue<int32>(0, Contract, true);
	for (const auto& Handle : System->GetEmitterHandles())
	{
		auto* Data = Handle.GetEmitterData(); auto* Emitter = Handle.GetInstance().Emitter.Get();
		if (!Data || !Emitter) { Error = TEXT("Missing cluster emitter data"); return false; }
		Emitter->Modify(); Data->SimTarget = ENiagaraSimTarget::GPUComputeSim; Data->bLocalSpace = false;
		// Bind the burst to the occupied batch capacity, not an unconditional 64 slots.
		auto* Source = Cast<UNiagaraScriptSource>(Data->GraphSource);
		if (!Source || !Source->NodeGraph) { Error = TEXT("Cluster emitter graph missing"); return false; }
		TArray<UNiagaraNodeFunctionCall*> Functions; Source->NodeGraph->GetNodesOfClass(Functions);
		bool bBound = false;
		for (auto* Function : Functions)
		{
			if (!Function->FunctionScript || Function->FunctionScript->GetName() != TEXT("SpawnBurst_Instantaneous")) continue;
			const FName UserName = Handle.GetName() == TEXT("History") ? TEXT("User.MissileHistoryCount") : TEXT("User.MissileSlotCount");
			const FNiagaraVariable Parameter(FNiagaraTypeDefinition::GetIntDef(), UserName);
			if (!System->GetExposedParameters().FindParameterOffset(Parameter))
				System->GetExposedParameters().AddParameter(Parameter);
			auto& Pin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(*Function,
				FNiagaraParameterHandle(FName(Function->GetFunctionName() + TEXT(".Spawn Count"))), FNiagaraTypeDefinition::GetIntDef(), FGuid(), FGuid());
			// The engine helper asserts on an already linked pin. Repeated authoring
			// must reuse the correct link and reject an unexpected graph safely.
			if (Pin.LinkedTo.IsEmpty())
				FNiagaraStackGraphUtilities::SetLinkedParameterValueForFunctionInput(Pin, Parameter, TSet<FNiagaraVariableBase>{Parameter});
			else if (Pin.LinkedTo.Num() != 1 || Pin.LinkedTo[0]->PinName != UserName)
			{ Error = TEXT("Cluster burst has an unexpected override link"); return false; }
			// Match the stack editor's linked-input conversion: remove the old
			// fixed rapid-iteration constant from both emitter and system scripts.
			const FNiagaraVariable OldCount(FNiagaraTypeDefinition::GetIntDef(), FName(FString::Printf(
				TEXT("Constants.%s.%s.Spawn Count"), *Handle.GetName().ToString(), *Function->GetFunctionName())));
			TArray<UNiagaraScript*> Affected{System->GetSystemSpawnScript(), System->GetSystemUpdateScript()};
			Data->GetScripts(Affected);
			for (auto* Script : Affected)
				if (Script && Script->RapidIterationParameters.RemoveParameter(OldCount))
					Script->MarkScriptAndSourceDesynchronized(TEXT("WM01 occupied batch capacity"), FGuid());
			bBound = true;
		}
		if (!bBound) { Error = TEXT("Cluster burst count was not bound"); return false; }
		Source->NodeGraph->NotifyGraphChanged();
		Data->CalculateBoundsMode = ENiagaraEmitterCalculateBoundMode::Fixed;
		Data->FixedBounds = FBox(FVector(-1000), FVector(1000));
		for (auto* Renderer : Data->GetRenderers())
		{
			Renderer->Modify();
			if (auto* Mesh = Cast<UNiagaraMeshRendererProperties>(Renderer))
			{
				Mesh->SortMode = ENiagaraSortMode::None; Mesh->Meshes.SetNum(1);
				Mesh->Meshes[0].Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/GuLiStrike/FX/WM01Missiles/SM_WM01_Missile.SM_WM01_Missile"));
				if (!Mesh->Meshes[0].Mesh) { Error = TEXT("Dedicated cluster mesh missing"); return false; }
			}
			if (auto* Sprite = Cast<UNiagaraSpriteRendererProperties>(Renderer)) Sprite->SortMode = ENiagaraSortMode::None;
		}
		FPropertyChangedEvent Changed(FindFProperty<FEnumProperty>(FVersionedNiagaraEmitterData::StaticStruct(), TEXT("SimTarget")));
		Emitter->PostEditChangeVersionedProperty(Changed, Handle.GetInstance().Version);
	}
	System->GetExposedParameters().SetParameterValue<int32>(2, Contract, true);
	System->PostEditChange(); System->MarkPackageDirty(); return true;
}

FString UGuLiCombatEffectAuthoringLibrary::GetMissilePodMeshDiagnostics(UStaticMesh* Mesh)
{
	if (!Mesh || Mesh->GetPathName() != TEXT("/Game/Commander/Units/Tactical/Cel/WarMachine/Meshes/SM_WarMachine_Rigid.SM_WarMachine_Rigid"))
		return TEXT("{\"error\":\"Unexpected mesh\"}");
	TArray<UStaticMesh*> Pending{Mesh}; FStaticMeshCompilingManager::Get().FinishCompilation(Pending);
	const auto* Render = Mesh->GetRenderData();
	if (!Render) return TEXT("{\"error\":\"No render data\"}");
	FString Result = TEXT("{\"lods\":[");
	for (int32 L = 0; L < Render->LODResources.Num(); ++L)
	{
		const auto& Lod = Render->LODResources[L]; const auto& Vertices = Lod.VertexBuffers.StaticMeshVertexBuffer;
		int32 Left = 0, Right = 0, Mixed = 0, NonIntegral = 0;
		TArray<int32> Parts; Parts.Init(0, Vertices.GetNumVertices());
		if (Vertices.GetNumTexCoords() >= 3)
		{
			for (uint32 V = 0; V < Vertices.GetNumVertices(); ++V)
			{
				const float Encoded = Vertices.GetVertexUV(V, 2).Y; const int32 Part = FMath::RoundToInt(Encoded); Parts[V] = Part;
				Left += Part == 10; Right += Part == 11; NonIntegral += !FMath::IsNearlyEqual(Encoded, float(Part), .01f);
			}
			for (int32 I = 0; I + 2 < Lod.IndexBuffer.GetNumIndices(); I += 3)
			{
				const int32 A = Parts[Lod.IndexBuffer.GetIndex(I)], B = Parts[Lod.IndexBuffer.GetIndex(I+1)], C = Parts[Lod.IndexBuffer.GetIndex(I+2)];
				if ((A == 10 || A == 11 || B == 10 || B == 11 || C == 10 || C == 11) && (A != B || A != C)) ++Mixed;
			}
		}
		Result += FString::Printf(TEXT("%s{\"lod\":%d,\"uv_channels\":%u,\"left_vertices\":%d,\"right_vertices\":%d,\"mixed_pod_triangles\":%d,\"nonintegral_parts\":%d}"),
			L ? TEXT(",") : TEXT(""), L, Vertices.GetNumTexCoords(), Left, Right, Mixed, NonIntegral);
	}
	return Result + TEXT("]}");
}

FString UGuLiCombatEffectAuthoringLibrary::GetMachineGunBoundsInputs(UNiagaraSystem* System)
{
	if (!System || !(System->GetOutermost()->GetName() == TEXT("/Game/Assets/Niagara_Toon_Projectiles_2/Effects/NS_Flash_1")
		|| System->GetOutermost()->GetName().StartsWith(TEXT("/Game/GuLiStrike/FX/CommanderWeapons/NS_MachineGun"))))
		return TEXT("{\"error\":\"Unexpected bounds audit asset\"}");
	auto Report = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Emitters, Interfaces, NumericInputs;
	TArray<UNiagaraScript*> Scripts {System->GetSystemSpawnScript(), System->GetSystemUpdateScript()};
	for (const auto& Handle : System->GetEmitterHandles())
	{
		const auto* Data = Handle.GetEmitterData();
		if (Data) Data->GetScripts(Scripts);
		const auto* Source = Data ? Cast<UNiagaraScriptSource>(Data->GraphSource) : nullptr;
		if (!Source || !Source->NodeGraph) continue;
		auto Emitter = MakeShared<FJsonObject>(); Emitter->SetStringField(TEXT("emitter"), Handle.GetName().ToString());
		Emitter->SetBoolField(TEXT("local_space"),Data->bLocalSpace);
		Emitter->SetBoolField(TEXT("enabled"),Handle.GetIsEnabled());
		Emitter->SetStringField(TEXT("simulation"),Data->SimTarget==ENiagaraSimTarget::CPUSim ? TEXT("CPU") : TEXT("GPU"));
		TArray<TSharedPtr<FJsonValue>> Nodes;
		for (const auto& NodePtr : Source->NodeGraph->Nodes)
		{
			const UEdGraphNode* Node = NodePtr.Get();
			if (!Node) continue;
			auto Entry = MakeShared<FJsonObject>(); Entry->SetStringField(TEXT("node"), Node->GetName());
			Entry->SetStringField(TEXT("class"), Node->GetClass()->GetName());
			if (const auto* Function = Cast<UNiagaraNodeFunctionCall>(Node))
			{
				Entry->SetStringField(TEXT("function"), Function->GetFunctionName());
				Entry->SetStringField(TEXT("function_script"), GetPathNameSafe(Function->FunctionScript));
				Entry->SetBoolField(TEXT("enabled"), Function->IsNodeEnabled());
			}
			TArray<TSharedPtr<FJsonValue>> Pins;
			for (const auto* Pin : Node->Pins)
			{
				if (!Pin) continue;
				auto P = MakeShared<FJsonObject>(); P->SetStringField(TEXT("name"), Pin->PinName.ToString());
				P->SetStringField(TEXT("default"), Pin->DefaultValue);
				P->SetStringField(TEXT("object"), GetPathNameSafe(Pin->DefaultObject));
				TArray<TSharedPtr<FJsonValue>> Links;
				for (const auto* Link : Pin->LinkedTo) if (Link)
					Links.Add(MakeShared<FJsonValueString>(Link->GetOwningNode()->GetName() + TEXT(".") + Link->PinName.ToString()));
				P->SetArrayField(TEXT("links"), Links); Pins.Add(MakeShared<FJsonValueObject>(P));
			}
			Entry->SetArrayField(TEXT("pins"), Pins); Nodes.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Emitter->SetArrayField(TEXT("nodes"), Nodes); Emitters.Add(MakeShared<FJsonValueObject>(Emitter));
	}
	ForEachObjectWithOuter(System, [&Interfaces](UObject* Object)
	{
		if (!Object->IsA<UNiagaraDataInterface>() || !Object->GetClass()->GetName().Contains(TEXT("Curve"))) return;
		auto Entry = MakeShared<FJsonObject>(); Entry->SetStringField(TEXT("path"), Object->GetPathName());
		for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
		{
			if (!It->GetName().Contains(TEXT("Curve"))) continue;
			FString Value; It->ExportText_InContainer(0, Value, Object, Object, Object, PPF_None);
			Entry->SetStringField(It->GetName(), Value);
		}
		Interfaces.Add(MakeShared<FJsonValueObject>(Entry));
	}, true);
	for (const auto* Script : Scripts) if (Script)
	{
		const auto& Store = Script->RapidIterationParameters;
		for (const auto& Variable : Store.ReadParameterVariables())
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			if (Variable.GetType() == FNiagaraTypeDefinition::GetVec2Def())
			{
				const FVector2f Value = Store.GetParameterValue<FVector2f>(Variable);
				Values.Add(MakeShared<FJsonValueNumber>(Value.X)); Values.Add(MakeShared<FJsonValueNumber>(Value.Y));
			}
			else if (Variable.GetType() == FNiagaraTypeDefinition::GetVec3Def())
			{
				const FVector3f Value = Store.GetParameterValue<FVector3f>(Variable);
				Values.Add(MakeShared<FJsonValueNumber>(Value.X)); Values.Add(MakeShared<FJsonValueNumber>(Value.Y)); Values.Add(MakeShared<FJsonValueNumber>(Value.Z));
			}
			else if (Variable.GetType() == FNiagaraTypeDefinition::GetFloatDef()) Values.Add(MakeShared<FJsonValueNumber>(Store.GetParameterValue<float>(Variable)));
			else continue;
			auto Entry = MakeShared<FJsonObject>(); Entry->SetStringField(TEXT("script"), Script->GetPathName());
			Entry->SetStringField(TEXT("parameter"), Variable.GetName().ToString()); Entry->SetArrayField(TEXT("value"), Values);
			NumericInputs.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}
	Report->SetArrayField(TEXT("emitters"), Emitters); Report->SetArrayField(TEXT("curve_interfaces"), Interfaces);
	Report->SetArrayField(TEXT("numeric_inputs"), NumericInputs);
	FString Result; FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Result)); return Result;
}

bool UGuLiCombatEffectAuthoringLibrary::ConfigureMachineGunBoundsEnvelope(UNiagaraSystem* System, FVector Extent, FString& Error)
{
	if (!System || !System->GetOutermost()->GetName().StartsWith(TEXT("/Game/GuLiStrike/FX/CommanderWeapons/NS_MachineGun"))
		|| Extent.ContainsNaN() || Extent.GetMin() <= 0 || Extent.GetMax() > 100000)
	{ Error = TEXT("Expected owned machine-gun candidate and finite positive envelope"); return false; }
	System->Modify();
	System->GetExposedParameters().SetParameterValue<FVector3f>(FVector3f(Extent),
		FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("User.GuLiPreSpawnBoundsExtent")), true);
	System->MarkPackageDirty(); return true;
}

FString UGuLiCombatEffectAuthoringLibrary::GetRogueUpgradeCompileDiagnostics(UNiagaraSystem* System)
{
	if (!System || System->GetOutermost()->GetName()!=TEXT("/Game/GuLiStrike/FX/RogueCards/NS_RogueUpgrade_Lite")) return TEXT("Unexpected upgrade system");
	FString Result;
	ForEachObjectWithOuter(System,[&Result](UObject* Object)
	{
		if (auto* Script=Cast<UNiagaraScript>(Object))
		{ const auto& VM=Script->GetVMExecutableData();
			if (!VM.ErrorMsg.IsEmpty()) Result+=Script->GetPathName()+TEXT(": ")+VM.ErrorMsg+TEXT("\n");
			for (const auto& Event:VM.LastCompileEvents)
				Result+=FString::Printf(TEXT("%s [%d] %s\n"),*Script->GetPathName(),int32(Event.Severity),*Event.Message); }
	},true); return Result;
}

namespace
{
	bool IsImpactBatch(const UNiagaraSystem* System)
	{ return InScope(System) && System->GetName().StartsWith(TEXT("NS_MachineGunImpact_Batch")); }
	UEdGraphPin* EffectMapPin(UEdGraphNode* Node,EEdGraphPinDirection Direction)
	{
		for (auto* Pin:Node->Pins) if (Pin->Direction==Direction && UEdGraphSchema_Niagara::PinToTypeDefinition(Pin)==FNiagaraTypeDefinition::GetParameterMapDef()) return Pin;
		return nullptr;
	}
	UNiagaraNodeCustomHlsl* AddImpactHlsl(FModuleGraph& Module, const FString& Code,
		const TArray<FNiagaraVariable>& Inputs, const TArray<FNiagaraVariable>& Outputs)
	{
		auto* Node=NewObject<UNiagaraNodeCustomHlsl>(Module.Graph,NAME_None,RF_Transactional);
		Node->Signature.Name=TEXT("GuLiImpactAttributes"); Node->Signature.Inputs=Inputs;
		for (const auto& Output:Outputs) Node->Signature.Outputs.Add(Output);
		Node->ScriptUsage=ENiagaraScriptUsage::Module;
		Module.Graph->AddNode(Node,false,false); Node->CreateNewGuid(); Node->PostPlacedNewNode(); Node->AllocateDefaultPins();
		auto* Text=FindFProperty<FStrProperty>(Node->GetClass(),TEXT("CustomHlsl"));
		if (!Text) { Module.bValid=false; return Node; }
		Text->SetPropertyValue_InContainer(Node,Code);
		return Node;
	}
	UNiagaraNodeFunctionCall* AddImpactArrayGet(FModuleGraph& Module, UNiagaraSystem* System, UClass* Class, FName User, FName Result)
	{
		const FNiagaraTypeDefinition Type(Class); const FNiagaraVariable Var(Type,User);
		auto& Params=System->GetExposedParameters();
		if (Params.IndexOf(Var)==INDEX_NONE) { Params.AddParameter(Var); Params.SetDataInterface(NewObject<UNiagaraDataInterface>(System,Class,NAME_None,RF_Transactional),Var); }
		TArray<FNiagaraFunctionSignature> Signatures; Class->GetDefaultObject<UNiagaraDataInterface>()->GetFunctionSignatures(Signatures);
		const auto* Signature=Signatures.FindByPredicate([](const auto& S) { return S.Name==TEXT("Get"); });
		if (!Signature) { Module.bValid=false; return nullptr; }
		auto* Node=NewObject<UNiagaraNodeFunctionCall>(Module.Graph,NAME_None,RF_Transactional); Node->Signature=*Signature;
		Module.Graph->AddNode(Node,false,false); Node->CreateNewGuid(); Node->PostPlacedNewNode(); Node->AllocateDefaultPins();
		Module.Link(Read(Module.Get,Type,User),Node->FindPin(TEXT("Array interface"),EGPD_Input));
		Module.Link(Read(Module.Get,FNiagaraTypeDefinition::GetIntDef(),BatchName(System,TEXT("Particles.ImpactSlot"))),Node->FindPin(TEXT("Index"),EGPD_Input));
		return Node;
	}
}

bool UGuLiCombatEffectAuthoringLibrary::ConfigureImpactChannel(UNiagaraDataChannelAsset* Asset, FString& Error)
{
	if (!InScope(Asset) || Asset->GetName()!=TEXT("NDC_CommanderImpacts")) { Error=TEXT("Expected dedicated owned impact channel"); return false; }
	if (!ConfigureGunfireChannel(Asset,Error)) return false;
	UNiagaraDataChannel* Channel=Asset->Get(); auto* Variables=FindFProperty<FArrayProperty>(Channel->GetClass(),TEXT("ChannelVariables"));
	TArray<FNiagaraDataChannelVariable> Payload;
	for (const auto& Pair:TArray<TPair<FName,FNiagaraTypeDefinition>>{
		{TEXT("Position"),FNiagaraTypeDefinition::GetPositionDef()}, {TEXT("Rotation"),FNiagaraTypeDefinition::GetQuatDef()},
		{TEXT("Scale"),FNiagaraTypeHelper::GetVectorDef()}, {TEXT("Tint"),FNiagaraTypeDefinition::GetColorDef()},
		{TEXT("Seed"),FNiagaraTypeDefinition::GetIntDef()}, {TEXT("Lifetime"),FNiagaraTypeDefinition::GetFloatDef()},
		{TEXT("Slot"),FNiagaraTypeDefinition::GetIntDef()}, {TEXT("Generation"),FNiagaraTypeDefinition::GetIntDef()}, {TEXT("Group"),FNiagaraTypeDefinition::GetIntDef()}})
	{ FNiagaraDataChannelVariable Var; Var.SetName(Pair.Key); Var.SetType(FNiagaraDataChannelVariable::ToDataChannelType(Pair.Value)); Payload.Add(Var); }
	Channel->PreEditChange(Variables); Variables->CopyCompleteValue(Variables->ContainerPtrToValuePtr<void>(Channel),&Payload);
	FPropertyChangedEvent Changed(Variables); Channel->PostEditChangeProperty(Changed); Asset->MarkPackageDirty(); return Channel->IsValid();
}

bool UGuLiCombatEffectAuthoringLibrary::ConfigureMuzzleChannel(UNiagaraDataChannelAsset* Asset, FString& Error)
{
	if (!InScope(Asset) || Asset->GetName()!=TEXT("NDC_CommanderMuzzles")) { Error=TEXT("Expected dedicated owned muzzle channel"); return false; }
	if (!ConfigureGunfireChannel(Asset,Error)) return false;
	UNiagaraDataChannel* Channel=Asset->Get(); auto* Variables=FindFProperty<FArrayProperty>(Channel->GetClass(),TEXT("ChannelVariables"));
	TArray<FNiagaraDataChannelVariable> Payload;
	for (const auto& Pair:TArray<TPair<FName,FNiagaraTypeDefinition>>{
		{TEXT("Position"),FNiagaraTypeDefinition::GetPositionDef()}, {TEXT("Rotation"),FNiagaraTypeDefinition::GetQuatDef()},
		{TEXT("Scale"),FNiagaraTypeHelper::GetVectorDef()}, {TEXT("Tint"),FNiagaraTypeDefinition::GetColorDef()},
		{TEXT("Seed"),FNiagaraTypeDefinition::GetIntDef()}, {TEXT("Lifetime"),FNiagaraTypeDefinition::GetFloatDef()},
		{TEXT("Slot"),FNiagaraTypeDefinition::GetIntDef()}, {TEXT("Generation"),FNiagaraTypeDefinition::GetIntDef()}, {TEXT("Group"),FNiagaraTypeDefinition::GetIntDef()}})
	{ FNiagaraDataChannelVariable Var; Var.SetName(Pair.Key); Var.SetType(FNiagaraDataChannelVariable::ToDataChannelType(Pair.Value)); Payload.Add(Var); }
	Channel->PreEditChange(Variables); Variables->CopyCompleteValue(Variables->ContainerPtrToValuePtr<void>(Channel),&Payload);
	FPropertyChangedEvent Changed(Variables); Channel->PostEditChangeProperty(Changed); Asset->MarkPackageDirty(); return Channel->IsValid();
}

bool UGuLiCombatEffectAuthoringLibrary::WireImpactReader(UNiagaraSystem* System,UNiagaraDataChannelAsset* Channel,
	UNiagaraScript* BindScript,UNiagaraScript* SpawnScript,UNiagaraScript* InitScript,int32 MinimumCount,int32 MaximumCount,FString& Error)
{
	if ((!IsImpactBatch(System) && !IsMuzzleBatch(System)) || !Channel || Channel->GetName()!=(IsMuzzleBatch(System) ? TEXT("NDC_CommanderMuzzles") : TEXT("NDC_CommanderImpacts")) || MinimumCount<0 || MaximumCount<MinimumCount)
	{ Error=TEXT("Invalid impact reader arguments"); return false; }
	if (!WireGunfireReader(System,Channel,BindScript,SpawnScript,InitScript,Error)) return false;
	for (auto* Script:{BindScript,SpawnScript,InitScript})
	{
		auto* Graph=CastChecked<UNiagaraScriptSource>(Script->GetLatestSource())->NodeGraph.Get();
		const auto Nodes=Graph->Nodes;
		for (UEdGraphNode* Raw:Nodes)
		{
			if (auto* Input=Cast<UNiagaraNodeInput>(Raw); Input && Input->Input.GetName()==TEXT("GunfireReader")) Input->Input.SetName(BatchName(System,TEXT("ImpactReader")));
			for (auto* Pin:Raw->Pins)
			{
				if (Pin->PinName==TEXT("Emitter.GunfireReader")) Pin->PinName=BatchName(System,TEXT("Emitter.ImpactReader"));
				if (Pin->PinName==TEXT("Particles.Alive")) Pin->PinName=TEXT("DataInstance.Alive");
				else if (Pin->PinName.ToString().StartsWith(TEXT("Particles."))) Pin->PinName=FName(FString(IsMuzzleBatch(System) ? TEXT("Particles.Muzzle") : TEXT("Particles.Impact"))+Pin->PinName.ToString().RightChop(10));
			}
			if (auto* Call=Cast<UNiagaraNodeFunctionCall>(Raw); Call && Call->Signature.Name==TEXT("SpawnConditional"))
			{
				Call->FindPin(TEXT("Min Spawn Count"),EGPD_Input)->DefaultValue=FString::FromInt(MinimumCount);
				Call->FindPin(TEXT("Max Spawn Count"),EGPD_Input)->DefaultValue=FString::FromInt(MaximumCount);
				Call->Signature.Inputs.Emplace(FNiagaraTypeDefinition::GetIntDef(),TEXT("Group"));
				auto* GroupPin=Call->CreatePin(EGPD_Input,UEdGraphSchema_Niagara::TypeDefinitionToPinType(FNiagaraTypeDefinition::GetIntDef()),TEXT("Group"));
				auto* Get=AddMapNode(Graph,TEXT("/Script/NiagaraEditor.NiagaraNodeParameterMapGet"));
				TArray<UNiagaraNodeInput*> Inputs; Graph->GetNodesOfClass(Inputs); if (Inputs.Num()!=1) return false;
				auto* Input=Inputs[0];
				Graph->GetSchema()->TryCreateConnection(Input->GetOutputPin(0),Get->GetInputPin(0));
				Graph->GetSchema()->TryCreateConnection(Read(Get,FNiagaraTypeDefinition::GetIntDef(),BatchName(System,TEXT("User.ImpactGroup"))),GroupPin);
			}
		}
		Graph->NotifyGraphChanged();
	}
	// One producer identity per hit; never overwrite Niagara's own persistent particle ID.
	auto* Graph=CastChecked<UNiagaraScriptSource>(InitScript->GetLatestSource())->NodeGraph.Get();
	TArray<UNiagaraNodeWithDynamicPins*> Maps; Graph->GetNodesOfClass(Maps);
	auto* Set=Maps.FindByPredicate([](auto* N) { return N->GetClass()->GetName()==TEXT("NiagaraNodeParameterMapSet"); });
	if (!Set) { Error=TEXT("Impact initialization map missing"); return false; }
	FModuleGraph Init; Init.Graph=Graph;
	TArray<UNiagaraNodeFunctionCall*> Calls; Graph->GetNodesOfClass(Calls);
	auto* ReadCall=Calls.FindByPredicate([](auto* N) { return N->Signature.Name==TEXT("Read"); });
	if (!ReadCall) return false;
	auto* Make=AddImpactHlsl(Init,TEXT("Identity.Index=Slot; Identity.AcquireTag=Generation;"),
		{FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(),TEXT("Slot")),FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(),TEXT("Generation"))},
		{FNiagaraVariable(FNiagaraTypeDefinition::GetIDDef(),TEXT("Identity"))});
	Init.Link((*ReadCall)->FindPin(TEXT("Slot"),EGPD_Output),Make->FindPin(TEXT("Slot"),EGPD_Input));
	Init.Link((*ReadCall)->FindPin(TEXT("Generation"),EGPD_Output),Make->FindPin(TEXT("Generation"),EGPD_Input));
	Init.Link(Make->FindPin(TEXT("Identity"),EGPD_Output),Write(*Set,FNiagaraTypeDefinition::GetIDDef(),BatchName(System,TEXT("Particles.ImpactIdentity"))));
	AddImpactTransforms(Init,*Set,(*ReadCall)->FindPin(TEXT("Position"),EGPD_Output),(*ReadCall)->FindPin(TEXT("Rotation"),EGPD_Output),(*ReadCall)->FindPin(TEXT("Scale"),EGPD_Output),IsMuzzleBatch(System) ? TEXT("Muzzle") : TEXT("Impact"));
	FinalizeScratchPins(System); Graph->NotifyGraphChanged(); System->MarkPackageDirty(); return Init.bValid;
}

bool UGuLiCombatEffectAuthoringLibrary::WireImpactLifecycle(UNiagaraSystem* System, UNiagaraScript* Update, FString& Error)
{
	if ((!IsImpactBatch(System) && !IsMuzzleBatch(System)) || !Update || Update->GetOutermost()!=System->GetOutermost()) return false;
	FModuleGraph Module; if (!Module.Initialize(Update)) return false;
	auto* Generations=AddImpactArrayGet(Module,System,UNiagaraDataInterfaceArrayInt32::StaticClass(),BatchName(System,TEXT("User.ImpactGenerations")),TEXT("Generation"));
	auto* BirthTimes=AddImpactArrayGet(Module,System,UNiagaraDataInterfaceArrayFloat::StaticClass(),BatchName(System,TEXT("User.ImpactBirthTimes")),TEXT("Birth"));
	if (!Generations || !BirthTimes) return false;
	const bool bFirstUpdate=Update->GetName().Contains(TEXT("Lifecycle"));
	// Event-spawned followers execute interpolated Spawn/Update before Receive.
	// Keep that temporary -1/0 identity alive until the event supplies its owner.
	// Array Get clamps -1 to the first active slot, so comparing it prematurely
	// would discard every follower before its payload is applied.
	auto* Code=AddImpactHlsl(Module,bFirstUpdate ? TEXT("bool PendingEvent=Slot<0 && Generation==0; Alive=PendingEvent || CurrentGeneration==Generation; Age=PendingEvent ? 0.0 : max(0.0,WorldTime-Birth);") : TEXT("bool PendingEvent=Slot<0 && Generation==0; Alive=WasAlive && (PendingEvent || CurrentGeneration==Generation); Age=PendingEvent ? 0.0 : max(0.0,WorldTime-Birth);"),
		{FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(),TEXT("WasAlive")),FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(),TEXT("CurrentGeneration")),
		 FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(),TEXT("Generation")),FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(),TEXT("Slot")),FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("WorldTime")),FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("Birth"))},
		{FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(),TEXT("Alive")),FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("Age"))});
	auto* Alive=Read(Module.Get,FNiagaraTypeDefinition::GetBoolDef(),TEXT("DataInstance.Alive"));
	// The first update gate initializes DataInstance.Alive; subsequent gates preserve ParticleState.
	Module.Link(Alive,Code->FindPin(TEXT("WasAlive"),EGPD_Input));
	Module.Link(Generations->FindPin(TEXT("Value"),EGPD_Output),Code->FindPin(TEXT("CurrentGeneration"),EGPD_Input));
	Module.Link(Read(Module.Get,FNiagaraTypeDefinition::GetIntDef(),BatchName(System,TEXT("Particles.ImpactGeneration"))),Code->FindPin(TEXT("Generation"),EGPD_Input));
	Module.Link(Read(Module.Get,FNiagaraTypeDefinition::GetIntDef(),BatchName(System,TEXT("Particles.ImpactSlot"))),Code->FindPin(TEXT("Slot"),EGPD_Input));
	Module.Link(Read(Module.Get,FNiagaraTypeDefinition::GetFloatDef(),BatchName(System,TEXT("User.ImpactWorldTime"))),Code->FindPin(TEXT("WorldTime"),EGPD_Input));
	Module.Link(BirthTimes->FindPin(TEXT("Value"),EGPD_Output),Code->FindPin(TEXT("Birth"),EGPD_Input));
	Module.Link(Code->FindPin(TEXT("Alive"),EGPD_Output),Write(Module.Set,FNiagaraTypeDefinition::GetBoolDef(),TEXT("DataInstance.Alive")));
	Module.Link(Code->FindPin(TEXT("Age"),EGPD_Output),Write(Module.Set,FNiagaraTypeDefinition::GetFloatDef(),BatchName(System,TEXT("Particles.ImpactAge"))));
	if (IsMuzzleBatch(System) && bFirstUpdate)
	{
		auto* Positions=AddImpactArrayGet(Module,System,UNiagaraDataInterfaceArrayPosition::StaticClass(),TEXT("User.MuzzlePositions"),TEXT("Position"));
		auto* Rotations=AddImpactArrayGet(Module,System,UNiagaraDataInterfaceArrayQuat::StaticClass(),TEXT("User.MuzzleRotations"),TEXT("Rotation"));
		if (!Positions || !Rotations) return false;
		auto* P=Positions->FindPin(TEXT("Value"),EGPD_Output); auto* Q=Rotations->FindPin(TEXT("Value"),EGPD_Output);
		Module.Link(P,Write(Module.Set,FNiagaraTypeDefinition::GetPositionDef(),TEXT("Particles.MuzzlePosition")));
		Module.Link(Q,Write(Module.Set,FNiagaraTypeDefinition::GetQuatDef(),TEXT("Particles.MuzzleRotation")));
		AddImpactTransforms(Module,Module.Set,P,Q,Read(Module.Get,FNiagaraTypeDefinition::GetVec3Def(),TEXT("Particles.MuzzleScale")),TEXT("Muzzle"));
	}
	Module.Graph->NotifyGraphChanged(); FinalizeScratchPins(System); System->MarkPackageDirty();
	if (!Module.bValid) Error=TEXT("Impact lifecycle pin connection failed"); return Module.bValid;
}

bool UGuLiCombatEffectAuthoringLibrary::MoveClientEffectModule(UNiagaraSystem* System,FName EmitterName,UNiagaraScript* Module,FString Usage,int32 Index,FString& Error)
{
	if (!InScope(System) || !Module || Module->GetOutermost()!=System->GetOutermost()) return false;
	for (const auto& Handle:System->GetEmitterHandles()) if (Handle.GetName()==EmitterName)
	{
		auto* Data=Handle.GetEmitterData(); UNiagaraScript* Owning=nullptr;
		if (Usage==TEXT("ParticleSpawn")) Owning=Data->SpawnScriptProps.Script;
		else if (Usage==TEXT("ParticleUpdate")) Owning=Data->UpdateScriptProps.Script;
		else if (Usage==TEXT("EmitterSpawn")) Owning=Data->EmitterSpawnScriptProps.Script;
		else if (Usage==TEXT("EmitterUpdate")) Owning=Data->EmitterUpdateScriptProps.Script;
		if (!Owning) return false;
		auto* Source=Cast<UNiagaraScriptSource>(Owning->GetLatestSource()); TArray<UNiagaraNodeFunctionCall*> Calls; Source->NodeGraph->GetNodesOfClass(Calls);
		for (auto* Call:Calls) if (Call->FunctionScript==Module)
		{
			if (Index!=0 && Index!=-1) { Error=TEXT("Use 0 for the first module or -1 for the last module"); return false; }
			auto* In=EffectMapPin(Call,EGPD_Input); auto* Out=EffectMapPin(Call,EGPD_Output);
			if (!In || !Out || In->LinkedTo.Num()!=1) return false;
			auto* Previous=In->LinkedTo[0]; const auto Next=Out->LinkedTo;
			if (Index==-1)
			{
				TArray<UEdGraphPin*> Queue{Out}; TSet<UEdGraphNode*> Seen; UNiagaraNodeOutput* End=nullptr;
				for (int32 Q=0;Q<Queue.Num();++Q) for (auto* Link:Queue[Q]->LinkedTo)
				{
					auto* Node=Link->GetOwningNode(); if (Seen.Contains(Node)) continue; Seen.Add(Node);
					if (auto* Output=Cast<UNiagaraNodeOutput>(Node)) { End=Output; continue; }
					if (auto* MapOut=EffectMapPin(Node,EGPD_Output)) Queue.Add(MapOut);
				}
				auto* Final=End ? EffectMapPin(End,EGPD_Input) : nullptr;
				if (!Final || Final->LinkedTo.Num()!=1) { Error=TEXT("Stage output not found"); return false; }
				auto* Last=Final->LinkedTo[0]; if (Last==Out) return true;
				Previous->BreakLinkTo(In); Out->BreakAllPinLinks();
				for (auto* Pin:Next) Source->NodeGraph->GetSchema()->TryCreateConnection(Previous,Pin);
				Last->BreakLinkTo(Final); Source->NodeGraph->GetSchema()->TryCreateConnection(Last,In);
				Source->NodeGraph->GetSchema()->TryCreateConnection(Out,Final);
				Owning->MarkScriptAndSourceDesynchronized(TEXT("Impact module ordering"),FGuid());
				Source->NodeGraph->NotifyGraphChanged(); System->MarkPackageDirty(); return true;
			}
			// Follow this call's own chain before disconnecting it. The combined emitter
			// graph's output UsageId can differ from the compiled stage script's UsageId.
			UEdGraphPin* First=Previous; TSet<UEdGraphNode*> Visited;
			while (First && !Cast<UNiagaraNodeInput>(First->GetOwningNode()))
			{
				auto* Node=First->GetOwningNode(); if (Visited.Contains(Node)) { First=nullptr; break; }
				Visited.Add(Node); auto* Pin=EffectMapPin(Node,EGPD_Input);
				First=Pin && Pin->LinkedTo.Num()==1 ? Pin->LinkedTo[0] : nullptr;
			}
			if (!First) { Error=TEXT("Stage parameter-map input not found"); return false; }
			if (First==Previous) return true;
			Previous->BreakLinkTo(In); Out->BreakAllPinLinks();
			for (auto* Pin:Next) Source->NodeGraph->GetSchema()->TryCreateConnection(Previous,Pin);
			const auto RootNext=First->LinkedTo; First->BreakAllPinLinks();
			Source->NodeGraph->GetSchema()->TryCreateConnection(First,In);
			for (auto* Pin:RootNext) Source->NodeGraph->GetSchema()->TryCreateConnection(Out,Pin);
			Owning->MarkScriptAndSourceDesynchronized(TEXT("Impact module ordering"),FGuid());
			Source->NodeGraph->NotifyGraphChanged(); System->MarkPackageDirty(); return true;
		}
	}
	Error=TEXT("Module or emitter not found"); return false;
}

namespace
{
	const TArray<FNiagaraVariable>& ImpactEventVariables(const UNiagaraSystem* System)
	{
		static const TArray<FNiagaraVariable> Variables={
			{FNiagaraTypeDefinition::GetIntDef(),TEXT("ImpactSlot")},{FNiagaraTypeDefinition::GetIntDef(),TEXT("ImpactGeneration")},
			{FNiagaraTypeDefinition::GetPositionDef(),TEXT("ImpactPosition")},{FNiagaraTypeDefinition::GetQuatDef(),TEXT("ImpactRotation")},
			{FNiagaraTypeDefinition::GetVec3Def(),TEXT("ImpactScale")},{FNiagaraTypeDefinition::GetColorDef(),TEXT("ImpactTint")},
			{FNiagaraTypeDefinition::GetFloatDef(),TEXT("ImpactLifetime")},{FNiagaraTypeDefinition::GetIntDef(),TEXT("ImpactSeed")}};
		static const TArray<FNiagaraVariable> MuzzleVariables=[]() { auto Copy=Variables; for (auto& V:Copy) V.SetName(FName(V.GetName().ToString().Replace(TEXT("Impact"),TEXT("Muzzle"),ESearchCase::CaseSensitive))); Copy.Emplace(FNiagaraTypeDefinition::GetIDDef(),TEXT("MuzzleIdentity")); return Copy; }();
		return IsMuzzleBatch(System) ? MuzzleVariables : Variables;
	}
	void BindImpactFunctionMap(UNiagaraNodeFunctionCall* Call,UNiagaraGraph* Graph)
	{
		Call->RefreshFromExternalChanges();
		auto* Map=EffectMapPin(Call,EGPD_Input); if (!Map) return;
		// Dynamic inputs can reach an override/default map through several value
		// functions. A default must inherit Begin Defaults, rather than the ordinary
		// module InMap; Niagara rejects the latter during default traversal.
		TArray<UEdGraphNode*> Pending{Call}; TSet<UEdGraphNode*> Seen; UEdGraphPin* Candidate=nullptr;
		while (!Pending.IsEmpty())
		{
			auto* Node=Pending.Pop(EAllowShrinking::No); if (Seen.Contains(Node)) continue; Seen.Add(Node);
			if (Node!=Call)
				if (auto* Previous=EffectMapPin(Node,EGPD_Input); Previous && Previous->LinkedTo.Num()==1)
				{ if (!Candidate) Candidate=Previous->LinkedTo[0]; }
			for (const auto* Output:Node->Pins) if (Output->Direction==EGPD_Output
				&& UEdGraphSchema_Niagara::PinToTypeDefinition(Output)!=FNiagaraTypeDefinition::GetParameterMapDef())
				for (auto* Linked:Output->LinkedTo)
				{
					if (Linked->GetOwningNode()->GetClass()->GetName()==TEXT("NiagaraNodeParameterMapGet")
						&& UEdGraphSchema_Niagara::PinToTypeDefinition(Linked)!=FNiagaraTypeDefinition::GetParameterMapDef())
					{
						TArray<UNiagaraNodeInput*> Inputs; Graph->GetNodesOfClass(Inputs);
						UNiagaraNodeInput* Defaults=nullptr;
						for (auto* Input:Inputs) if (Input->Usage==ENiagaraInputNodeUsage::TranslatorConstant && Input->Input==TRANSLATOR_PARAM_BEGIN_DEFAULTS) { Defaults=Input; break; }
						if (!Defaults)
						{
							Defaults=NewObject<UNiagaraNodeInput>(Graph,NAME_None,RF_Transactional);
							Defaults->Input=TRANSLATOR_PARAM_BEGIN_DEFAULTS; Defaults->Usage=ENiagaraInputNodeUsage::TranslatorConstant;
							Defaults->ExposureOptions.bExposed=false; Defaults->ExposureOptions.bRequired=false;
							Defaults->ExposureOptions.bHidden=true; Defaults->ExposureOptions.bCanAutoBind=true;
							Graph->AddNode(Defaults,false,false); Defaults->CreateNewGuid(); Defaults->PostPlacedNewNode(); Defaults->AllocateDefaultPins();
						}
						Map->BreakAllPinLinks(); Graph->GetSchema()->TryCreateConnection(Defaults->GetOutputPin(0),Map); return;
					}
					Pending.Add(Linked->GetOwningNode());
				}
		}
		if (!Map->LinkedTo.IsEmpty()) return;
		if (Candidate) { Graph->GetSchema()->TryCreateConnection(Candidate,Map); return; }
		TArray<UNiagaraNodeInput*> Inputs; Graph->GetNodesOfClass(Inputs);
		for (auto* Input:Inputs) if (Input->Input.GetType()==FNiagaraTypeDefinition::GetParameterMapDef())
		{ Graph->GetSchema()->TryCreateConnection(Input->GetOutputPin(0),Map); return; }
		// Introducing an event attribute to a value-only helper also introduces a
		// map dependency. Propagate it through every enclosing value function.
		auto* Input=NewObject<UNiagaraNodeInput>(Graph,NAME_None,RF_Transactional);
		Input->Input=FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(),TEXT("InMap"));
		Input->Usage=ENiagaraInputNodeUsage::Parameter;
		Graph->AddNode(Input,false,false); Input->CreateNewGuid(); Input->PostPlacedNewNode(); Input->AllocateDefaultPins();
		Graph->GetSchema()->TryCreateConnection(Input->GetOutputPin(0),Map);
	}
	UNiagaraScript* CopyImpactModule(UNiagaraScript* Original,UNiagaraSystem* System,TMap<UNiagaraScript*,UNiagaraScript*>& Copies)
	{
		if (!Original) return nullptr;
		if (auto* Copy=Copies.Find(Original)) return *Copy;
		UNiagaraScript* Copy=Original->IsIn(System) ? Original : DuplicateObject<UNiagaraScript>(Original,System,MakeUniqueObjectName(System,Original->GetClass(),FName(FString(IsMuzzleBatch(System) ? TEXT("GuLiMuzzle_") : TEXT("GuLiImpact_"))+Original->GetName())));
		Copies.Add(Original,Copy);
		TArray<UNiagaraScriptSource*> Sources;
		for (const auto& Version:Copy->GetAllAvailableVersions())
			if (auto* Source=Cast<UNiagaraScriptSource>(Copy->GetSource(Version.VersionGuid)); Source && Source->NodeGraph) Sources.AddUnique(Source);
		if (auto* Source=Cast<UNiagaraScriptSource>(Copy->GetLatestSource()); Source && Source->NodeGraph) Sources.AddUnique(Source);
		// Function calls preserve their selected version; every copied version must use event inputs.
		for (auto* Source:Sources)
		{
		auto* Graph=Source->NodeGraph.Get(); Graph->Modify();
		TMap<FName,FName> Replacements={
			{TEXT("Engine.Owner.Position"),BatchName(System,TEXT("Particles.ImpactPosition"))},{TEXT("Engine.Owner.Scale"),BatchName(System,TEXT("Particles.ImpactScale"))},
			{TEXT("Engine.Owner.Rotation"),BatchName(System,TEXT("Particles.ImpactRotation"))},{TEXT("System.Age"),TEXT("Particles.ImpactAge")},
			{TEXT("Engine.Owner.SystemLocalToWorld"),TEXT("Particles.ImpactLocalToWorld")},{TEXT("Engine.Owner.SystemWorldToLocal"),TEXT("Particles.ImpactWorldToLocal")},
			{TEXT("Engine.Owner.SystemLocalToWorldTransposed"),TEXT("Particles.ImpactLocalToWorldTransposed")},{TEXT("Engine.Owner.SystemWorldToLocalTransposed"),TEXT("Particles.ImpactWorldToLocalTransposed")},
			{TEXT("Engine.Owner.SystemLocalToWorldNoScale"),TEXT("Particles.ImpactLocalToWorldNoScale")},{TEXT("Engine.Owner.SystemWorldToLocalNoScale"),TEXT("Particles.ImpactWorldToLocalNoScale")},
			{TEXT("Engine.Owner.SystemXAxis"),TEXT("Particles.ImpactXAxis")},{TEXT("Engine.Owner.SystemYAxis"),TEXT("Particles.ImpactYAxis")},{TEXT("Engine.Owner.SystemZAxis"),TEXT("Particles.ImpactZAxis")},
			{TEXT("System.NormalizedAge"),TEXT("Particles.NormalizedAge")},{TEXT("User.Tint"),TEXT("Particles.ImpactTint")},
			{TEXT("Engine.Emitter.RandomSeed"),TEXT("Particles.ImpactSeed")},{TEXT("Emitter.RandomSeed"),TEXT("Particles.ImpactSeed")}};
		if (IsMuzzleBatch(System)) for (auto& Pair:Replacements) Pair.Value=BatchName(System,*Pair.Value.ToString());
		const bool bGenerator=Original->GetName().Contains(TEXT("GenerateLocationEvent"));
		for (const auto& Pair:Graph->GetAllMetaData())
			if (auto* Variable=Pair.Value.Get())
			{
				if (const auto* Name=Replacements.Find(Variable->DefaultBinding.GetName()))
				{
					Variable->Modify(); Variable->DefaultBinding.SetName(*Name); Variable->UpdateChangeId();
				}
				if (bGenerator && Variable->DefaultBinding.GetName()==TEXT("Particles.ID"))
				{ Variable->Modify(); Variable->DefaultBinding.SetName(BatchName(System,TEXT("Particles.ImpactIdentity"))); Variable->UpdateChangeId(); }
			}
		TArray<UNiagaraNodeInput*> Inputs; Graph->GetNodesOfClass(Inputs);
		UNiagaraNodeInput* MapInput=nullptr;
		for (auto* Input:Inputs) if (Input->Input.GetType()==FNiagaraTypeDefinition::GetParameterMapDef()) { MapInput=Input; break; }
		for (auto* Input:Inputs)
		{
			const FName OldName=Input->Input.GetName(); const auto* Replacement=Replacements.Find(OldName);
			FName NewName=Replacement ? *Replacement : OldName;
			if (bGenerator && NewName==TEXT("Particles.ID")) NewName=BatchName(System,TEXT("Particles.ImpactIdentity"));
			if (!Replacement && !NewName.ToString().StartsWith(IsMuzzleBatch(System) ? TEXT("Particles.Muzzle") : TEXT("Particles.Impact"))) continue;
			if (!MapInput)
			{
				MapInput=NewObject<UNiagaraNodeInput>(Graph,NAME_None,RF_Transactional);
				MapInput->Input=FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(),TEXT("InMap"));
				MapInput->Usage=ENiagaraInputNodeUsage::Parameter;
				Graph->AddNode(MapInput,false,false); MapInput->CreateNewGuid(); MapInput->PostPlacedNewNode(); MapInput->AllocateDefaultPins();
			}
			auto* Get=AddMapNode(Graph,TEXT("/Script/NiagaraEditor.NiagaraNodeParameterMapGet"));
			Graph->GetSchema()->TryCreateConnection(MapInput->GetOutputPin(0),Get->GetInputPin(0));
			auto* Value=Read(Get,Input->Input.GetType(),NewName);
			const auto Linked=Input->GetOutputPin(0)->LinkedTo;
			Input->BreakAllNodeLinks();
			for (auto* Pin:Linked) Graph->GetSchema()->TryCreateConnection(Value,Pin);
			Graph->RemoveNode(Input);
		}
		const auto Nodes=Graph->Nodes;
		for (UEdGraphNode* Node:Nodes)
		{
			if (auto* Hlsl=Cast<UNiagaraNodeCustomHlsl>(Node))
			{
				auto RenameSignature=[&](auto& Variables)
				{
					for (auto& Variable:Variables)
					{
						if (const auto* Name=Replacements.Find(Variable.GetName())) Variable.SetName(*Name);
						if (bGenerator && Variable.GetName()==TEXT("Particles.ID")) Variable.SetName(BatchName(System,TEXT("Particles.ImpactIdentity")));
					}
				};
				RenameSignature(Hlsl->Signature.Inputs); RenameSignature(Hlsl->Signature.Outputs);
				if (auto* Property=FindFProperty<FStrProperty>(Hlsl->GetClass(),TEXT("CustomHlsl")))
				{
					FString Code=Property->GetPropertyValue_InContainer(Hlsl);
					for (const auto& Replacement:Replacements) Code.ReplaceInline(*Replacement.Key.ToString(),*Replacement.Value.ToString(),ESearchCase::CaseSensitive);
					if (bGenerator) Code.ReplaceInline(TEXT("Particles.ID"),*BatchName(System,TEXT("Particles.ImpactIdentity")).ToString(),ESearchCase::CaseSensitive);
					Property->SetPropertyValue_InContainer(Hlsl,Code);
				}
			}
			for (auto* Pin:Node->Pins)
			{
				if (const auto* Name=Replacements.Find(Pin->PinName)) Pin->PinName=*Name;
				if (bGenerator && Pin->PinName==TEXT("Particles.ID")) Pin->PinName=BatchName(System,TEXT("Particles.ImpactIdentity"));
			}
			if (auto* Call=Cast<UNiagaraNodeFunctionCall>(Node); Call && Call->FunctionScript)
			{ Call->FunctionScript=CopyImpactModule(Call->FunctionScript,System,Copies); BindImpactFunctionMap(Call,Graph); }
			if (auto* NiagaraNode=Cast<UNiagaraNode>(Node))
				NiagaraNode->MarkNodeRequiresSynchronization(TEXT("Impact per-event node inputs"),true);
		}
		TArray<UNiagaraNodeWriteDataSet*> Writers; Graph->GetNodesOfClass(Writers);
		for (auto* Writer:Writers)
		{
			Writer->ExternalStructAsset=nullptr;
			bool bMissing=false; for (const auto& Var:ImpactEventVariables(System)) bMissing|=!Writer->Variables.Contains(Var);
			if (!bMissing) continue;
			auto* Get=AddMapNode(Graph,TEXT("/Script/NiagaraEditor.NiagaraNodeParameterMapGet"));
			UEdGraphPin* Map=nullptr; for (auto* Pin:Writer->Pins) if (Pin->Direction==EGPD_Input && UEdGraphSchema_Niagara::PinToTypeDefinition(Pin)==FNiagaraTypeDefinition::GetParameterMapDef()) { Map=Pin; break; }
			if (!Map || Map->LinkedTo.IsEmpty()) continue;
			Graph->GetSchema()->TryCreateConnection(Map->LinkedTo[0],Get->GetInputPin(0));
			for (const auto& Var:ImpactEventVariables(System)) if (!Writer->Variables.Contains(Var))
			{
				Writer->Variables.Add(Var); Writer->VariableFriendlyNames.Add(Var.GetName().ToString());
				auto* Pin=Writer->CreatePin(EGPD_Input,UEdGraphSchema_Niagara::TypeDefinitionToPinType(Var.GetType()),Var.GetName());
				Graph->GetSchema()->TryCreateConnection(Read(Get,Var.GetType(),FName(TEXT("Particles.")+Var.GetName().ToString())),Pin);
			}
		}
		TArray<UNiagaraNodeReadDataSet*> Readers; Graph->GetNodesOfClass(Readers);
		for (auto* Reader:Readers)
		{
			Reader->ExternalStructAsset=nullptr;
			bool bMissing=false; for (const auto& Var:ImpactEventVariables(System)) bMissing|=!Reader->Variables.Contains(Var);
			if (!bMissing) continue;
			TArray<UNiagaraNodeOutput*> Outputs; Graph->GetNodesOfClass(Outputs); if (Outputs.Num()!=1) continue;
			auto* Out=Outputs[0]->GetInputPin(0); if (!Out || Out->LinkedTo.IsEmpty()) continue;
			auto* Previous=Out->LinkedTo[0]; Previous->BreakLinkTo(Out);
			auto* Set=AddMapNode(Graph,TEXT("/Script/NiagaraEditor.NiagaraNodeParameterMapSet"));
			Graph->GetSchema()->TryCreateConnection(Previous,Set->GetInputPin(0)); Graph->GetSchema()->TryCreateConnection(Set->GetOutputPin(0),Out);
			for (const auto& Var:ImpactEventVariables(System)) if (!Reader->Variables.Contains(Var))
			{
				Reader->Variables.Add(Var); Reader->VariableFriendlyNames.Add(Var.GetName().ToString());
				auto* Pin=Reader->CreatePin(EGPD_Output,UEdGraphSchema_Niagara::TypeDefinitionToPinType(Var.GetType()),Var.GetName());
				Graph->GetSchema()->TryCreateConnection(Pin,Write(Set,Var.GetType(),FName(TEXT("Particles.")+Var.GetName().ToString())));
			}
			FModuleGraph Module; Module.Graph=Graph;
			AddImpactTransforms(Module,Set,Reader->FindPin(BatchName(System,TEXT("ImpactPosition")),EGPD_Output),Reader->FindPin(BatchName(System,TEXT("ImpactRotation")),EGPD_Output),Reader->FindPin(BatchName(System,TEXT("ImpactScale")),EGPD_Output),IsMuzzleBatch(System) ? TEXT("Muzzle") : TEXT("Impact"));
			if (IsMuzzleBatch(System))
			{
				// Receive is after interpolated Spawn. Publish the follower's first render
				// attributes here as well, after its actual parent identity and pose arrive.
				auto* Get=AddMapNode(Graph,TEXT("/Script/NiagaraEditor.NiagaraNodeParameterMapGet"));
				auto* RenderSet=AddMapNode(Graph,TEXT("/Script/NiagaraEditor.NiagaraNodeParameterMapSet"));
				Set->GetOutputPin(0)->BreakLinkTo(Out);
				Graph->GetSchema()->TryCreateConnection(Set->GetOutputPin(0),Get->GetInputPin(0));
				Graph->GetSchema()->TryCreateConnection(Set->GetOutputPin(0),RenderSet->GetInputPin(0));
				Graph->GetSchema()->TryCreateConnection(RenderSet->GetOutputPin(0),Out);
				auto* H=AddImpactHlsl(Module,TEXT("float3 L=P*S; WorldP=Origin+L+2*cross(Q.xyz,cross(Q.xyz,L)+Q.w*L); WorldWidth=Width*S.x; WorldFacing=Facing+2*cross(Q.xyz,cross(Q.xyz,Facing)+Q.w*Facing);"),
					{{FNiagaraTypeDefinition::GetPositionDef(),TEXT("P")},{FNiagaraTypeDefinition::GetPositionDef(),TEXT("Origin")},{FNiagaraTypeDefinition::GetQuatDef(),TEXT("Q")},{FNiagaraTypeDefinition::GetVec3Def(),TEXT("S")},{FNiagaraTypeDefinition::GetFloatDef(),TEXT("Width")},{FNiagaraTypeDefinition::GetVec3Def(),TEXT("Facing")}},
					{{FNiagaraTypeDefinition::GetPositionDef(),TEXT("WorldP")},{FNiagaraTypeDefinition::GetFloatDef(),TEXT("WorldWidth")},{FNiagaraTypeDefinition::GetVec3Def(),TEXT("WorldFacing")}});
				const TArray<FNiagaraVariable> Values={{FNiagaraTypeDefinition::GetPositionDef(),TEXT("Particles.Position")},{FNiagaraTypeDefinition::GetPositionDef(),TEXT("Particles.MuzzlePosition")},{FNiagaraTypeDefinition::GetQuatDef(),TEXT("Particles.MuzzleRotation")},{FNiagaraTypeDefinition::GetVec3Def(),TEXT("Particles.MuzzleScale")},{FNiagaraTypeDefinition::GetFloatDef(),TEXT("Particles.RibbonWidth")},{FNiagaraTypeDefinition::GetVec3Def(),TEXT("Particles.RibbonFacing")}};
				const TArray<FName> Pins={TEXT("P"),TEXT("Origin"),TEXT("Q"),TEXT("S"),TEXT("Width"),TEXT("Facing")};
				for (int32 I=0;I<Values.Num();++I) Module.Link(Read(Get,Values[I].GetType(),Values[I].GetName()),H->FindPin(Pins[I],EGPD_Input));
				Module.Link(H->FindPin(TEXT("WorldP"),EGPD_Output),Write(RenderSet,FNiagaraTypeDefinition::GetPositionDef(),TEXT("Particles.MuzzleRenderPosition")));
				Module.Link(H->FindPin(TEXT("WorldP"),EGPD_Output),Write(RenderSet,FNiagaraTypeDefinition::GetPositionDef(),TEXT("Particles.Previous.MuzzleRenderPosition")));
				Module.Link(H->FindPin(TEXT("WorldWidth"),EGPD_Output),Write(RenderSet,FNiagaraTypeDefinition::GetFloatDef(),TEXT("Particles.MuzzleRenderWidth")));
				Module.Link(H->FindPin(TEXT("WorldFacing"),EGPD_Output),Write(RenderSet,FNiagaraTypeDefinition::GetVec3Def(),TEXT("Particles.MuzzleRenderRibbonFacing")));
			}

		}
		Graph->NotifyGraphChanged();
		}
		for (const auto& Version:Copy->GetAllAvailableVersions())
			Copy->MarkScriptAndSourceDesynchronized(TEXT("Impact per-event module inputs"),Version.VersionGuid);
		return Copy;
	}
}

bool UGuLiCombatEffectAuthoringLibrary::PrepareImpactBatchSystem(UNiagaraSystem* System,FString& Error)
{
	if (!IsImpactBatch(System) && !IsMuzzleBatch(System)) return false;
	System->Modify(); TMap<UNiagaraScript*,UNiagaraScript*> Copies;
	for (const auto& Handle:System->GetEmitterHandles())
	{
		auto* Data=Handle.GetEmitterData(); auto* Emitter=Handle.GetInstance().Emitter.Get(); if (!Data || !Emitter) return false;
		// The certified muzzle sources simulate locally, including their event followers.
		// A neutral shared component renders explicit per-event world attributes.
		Emitter->Modify(); Data->bLocalSpace=IsMuzzleBatch(System); Data->SimTarget=ENiagaraSimTarget::CPUSim;
		auto* Source=Cast<UNiagaraScriptSource>(Data->SpawnScriptProps.Script->GetLatestSource());
		TArray<UNiagaraNodeOutput*> Outputs; Source->NodeGraph->GetNodesOfClass(Outputs);
		for (auto* Output:Outputs)
		{
			if (Output->GetUsage()!=ENiagaraScriptUsage::ParticleSpawnScript && Output->GetUsage()!=ENiagaraScriptUsage::ParticleUpdateScript
				&& Output->GetUsage()!=ENiagaraScriptUsage::ParticleEventScript) continue;
			TSet<UEdGraphNode*> Visited;
			TArray<UEdGraphNode*> PendingNodes{Output};
			while (!PendingNodes.IsEmpty())
			{
				auto* Node=PendingNodes.Pop(EAllowShrinking::No); if (Visited.Contains(Node)) continue; Visited.Add(Node);
				if (auto* Call=Cast<UNiagaraNodeFunctionCall>(Node); Call && Call->FunctionScript
					&& (!Call->FunctionScript->GetName().StartsWith(IsMuzzleBatch(System) ? TEXT("GuLiMuzzle") : TEXT("GuLiImpact"))
						|| Call->FunctionScript->GetName().StartsWith(IsMuzzleBatch(System) ? TEXT("GuLiMuzzle_") : TEXT("GuLiImpact_"))))
				{ Call->FunctionScript=CopyImpactModule(Call->FunctionScript,System,Copies); BindImpactFunctionMap(Call,Source->NodeGraph); }
				// Overrides and dynamic inputs branch away from the parameter-map spine.
				// They must migrate too, including SimulationPosition and transform helpers.
				for (const auto* Pin:Node->Pins) if (Pin->Direction==EGPD_Input)
					for (auto* Linked:Pin->LinkedTo) PendingNodes.Add(Linked->GetOwningNode());
				if (auto* NiagaraNode=Cast<UNiagaraNode>(Node)) NiagaraNode->MarkNodeRequiresSynchronization(TEXT("Impact particle graph dependency"),true);
			}
		}
		Data->SpawnScriptProps.Script->MarkScriptAndSourceDesynchronized(TEXT("Impact copied module dependencies"),FGuid());
		Source->NodeGraph->NotifyGraphChanged();
	}
	System->GetExposedParameters().SetParameterValue<float>(0,FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),BatchName(System,TEXT("User.GuLiImpactInputVersion"))),true);
	System->GetExposedParameters().SetParameterValue<float>(1.25f,FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),BatchName(System,TEXT("User.GuLiImpactMaximumLifetime"))),true);
	System->GetExposedParameters().SetParameterValue<int32>(0,FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(),BatchName(System,TEXT("User.ImpactGroup"))),true);
	System->GetExposedParameters().SetParameterValue<float>(0,FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),BatchName(System,TEXT("User.ImpactWorldTime"))),true);
	FinalizeScratchPins(System); System->MarkPackageDirty(); System->RequestCompile(true); return true;
}

bool UGuLiCombatEffectAuthoringLibrary::WireMuzzleRender(UNiagaraSystem* System,FName EmitterName,UNiagaraScript* Script,FString& Error)
{
	if (!IsMuzzleBatch(System) || !Script || Script->GetOutermost()!=System->GetOutermost()) return false;
	const auto* Handle=System->GetEmitterHandles().FindByPredicate([&](const auto& H) { return H.GetName()==EmitterName; });
	if (!Handle || !Handle->GetEmitterData()) return false;
	const auto Instance=Handle->GetInstance();
	const FVersionedNiagaraEmitterBase Emitter(Instance.Emitter.Get(),Instance.Version);
	FModuleGraph M; if (!M.Initialize(Script)) return false;
	auto* Positions=AddImpactArrayGet(M,System,UNiagaraDataInterfaceArrayPosition::StaticClass(),TEXT("User.MuzzlePositions"),TEXT("Position"));
	auto* Rotations=AddImpactArrayGet(M,System,UNiagaraDataInterfaceArrayQuat::StaticClass(),TEXT("User.MuzzleRotations"),TEXT("Rotation"));
	if (!Positions || !Rotations) return false;
	const bool bBirth=Script->GetName().Contains(TEXT("Birth"));
	TArray<FNiagaraVariable> Inputs={
		{FNiagaraTypeDefinition::GetPositionDef(),TEXT("P")},{FNiagaraTypeDefinition::GetPositionDef(),TEXT("Origin")},
		{FNiagaraTypeDefinition::GetQuatDef(),TEXT("Q")},{FNiagaraTypeDefinition::GetVec3Def(),TEXT("S")},
		{FNiagaraTypeDefinition::GetVec3Def(),TEXT("V")},{FNiagaraTypeDefinition::GetPositionDef(),TEXT("Previous")}};
	TArray<FNiagaraVariable> Outputs={
		{FNiagaraTypeDefinition::GetPositionDef(),TEXT("WorldP")},{FNiagaraTypeDefinition::GetPositionDef(),TEXT("PreviousP")},
		{FNiagaraTypeDefinition::GetVec3Def(),TEXT("WorldV")}};
	FString Code=TEXT("float3 Axis=Q.xyz; float3 L=P*S; WorldP=Origin+L+2*cross(Axis,cross(Axis,L)+Q.w*L); "
		"float3 LV=V*S; WorldV=LV+2*cross(Axis,cross(Axis,LV)+Q.w*LV); ");
	Code+=bBirth ? TEXT("PreviousP=WorldP; ") : TEXT("PreviousP=Previous; ");
	bool bSprite=false,bMesh=false,bRibbon=false;
	for (auto* Renderer:Handle->GetEmitterData()->GetRenderers())
	{ bSprite|=Renderer->IsA<UNiagaraSpriteRendererProperties>(); bMesh|=Renderer->IsA<UNiagaraMeshRendererProperties>(); bRibbon|=Renderer->IsA<UNiagaraRibbonRendererProperties>(); }
	if (bSprite)
	{
		Inputs.Append({{FNiagaraTypeDefinition::GetVec2Def(),TEXT("Size")},{FNiagaraTypeDefinition::GetVec3Def(),TEXT("Facing")},{FNiagaraTypeDefinition::GetVec3Def(),TEXT("Alignment")}});
		Outputs.Append({{FNiagaraTypeDefinition::GetVec2Def(),TEXT("WorldSize")},{FNiagaraTypeDefinition::GetVec3Def(),TEXT("WorldFacing")},{FNiagaraTypeDefinition::GetVec3Def(),TEXT("WorldAlignment")}});
		Code+=TEXT("WorldSize=Size*S.xy; WorldFacing=Facing+2*cross(Axis,cross(Axis,Facing)+Q.w*Facing); WorldAlignment=Alignment+2*cross(Axis,cross(Axis,Alignment)+Q.w*Alignment); ");
	}
	if (bMesh)
	{
		Inputs.Append({{FNiagaraTypeDefinition::GetQuatDef(),TEXT("Orientation")},{FNiagaraTypeDefinition::GetVec3Def(),TEXT("MeshScale")}});
		Outputs.Append({{FNiagaraTypeDefinition::GetQuatDef(),TEXT("WorldOrientation")},{FNiagaraTypeDefinition::GetVec3Def(),TEXT("WorldScale")}});
		Code+=TEXT("WorldOrientation=float4(Q.w*Orientation.xyz+Orientation.w*Q.xyz+cross(Q.xyz,Orientation.xyz),Q.w*Orientation.w-dot(Q.xyz,Orientation.xyz)); WorldScale=MeshScale*S; ");
	}
	if (bRibbon)
	{
		Inputs.Append({{FNiagaraTypeDefinition::GetFloatDef(),TEXT("Width")},{FNiagaraTypeDefinition::GetVec3Def(),TEXT("RibbonFacing")}});
		Outputs.Append({{FNiagaraTypeDefinition::GetFloatDef(),TEXT("WorldWidth")},{FNiagaraTypeDefinition::GetVec3Def(),TEXT("WorldRibbonFacing")}});
		Code+=TEXT("WorldWidth=Width*S.x; WorldRibbonFacing=RibbonFacing+2*cross(Axis,cross(Axis,RibbonFacing)+Q.w*RibbonFacing); ");
	}
	auto* H=AddImpactHlsl(M,Code,Inputs,Outputs);
	M.Link(Positions->FindPin(TEXT("Value"),EGPD_Output),H->FindPin(TEXT("Origin"),EGPD_Input));
	M.Link(Rotations->FindPin(TEXT("Value"),EGPD_Output),H->FindPin(TEXT("Q"),EGPD_Input));
	const TMap<FName,FName> Names={{TEXT("P"),TEXT("Particles.Position")},{TEXT("S"),TEXT("Particles.MuzzleScale")},
		{TEXT("V"),TEXT("Particles.Velocity")},{TEXT("Previous"),TEXT("Particles.MuzzleRenderPosition")},
		{TEXT("Size"),TEXT("Particles.SpriteSize")},{TEXT("Facing"),TEXT("Particles.SpriteFacing")},{TEXT("Alignment"),TEXT("Particles.SpriteAlignment")},
		{TEXT("Orientation"),TEXT("Particles.MeshOrientation")},{TEXT("MeshScale"),TEXT("Particles.Scale")},
		{TEXT("Width"),TEXT("Particles.RibbonWidth")},{TEXT("RibbonFacing"),TEXT("Particles.RibbonFacing")}};
	for (const auto& Input:Inputs) if (const auto* Name=Names.Find(Input.GetName()))
	{
		if (bBirth && Input.GetName()==TEXT("Previous")) M.Link(Positions->FindPin(TEXT("Value"),EGPD_Output),H->FindPin(TEXT("Previous"),EGPD_Input));
		else M.Link(Read(M.Get,Input.GetType(),*Name),H->FindPin(Input.GetName(),EGPD_Input));
	}
	const TMap<FName,FName> OutNames={{TEXT("WorldP"),TEXT("Particles.MuzzleRenderPosition")},{TEXT("PreviousP"),TEXT("Particles.Previous.MuzzleRenderPosition")},
		{TEXT("WorldV"),TEXT("Particles.MuzzleRenderVelocity")},{TEXT("WorldSize"),TEXT("Particles.MuzzleRenderSize")},
		{TEXT("WorldFacing"),TEXT("Particles.MuzzleRenderFacing")},{TEXT("WorldAlignment"),TEXT("Particles.MuzzleRenderAlignment")},
		{TEXT("WorldOrientation"),TEXT("Particles.MuzzleRenderOrientation")},{TEXT("WorldScale"),TEXT("Particles.MuzzleRenderScale")},
		{TEXT("WorldWidth"),TEXT("Particles.MuzzleRenderWidth")},{TEXT("WorldRibbonFacing"),TEXT("Particles.MuzzleRenderRibbonFacing")}};
	for (const auto& Output:Outputs) M.Link(H->FindPin(Output.GetName(),EGPD_Output),Write(M.Set,Output.GetType(),OutNames[Output.GetName()]));
	for (auto* Renderer:Handle->GetEmitterData()->GetRenderers())
	{
		Renderer->Modify();
		if (auto* Sprite=Cast<UNiagaraSpriteRendererProperties>(Renderer))
		{
			Sprite->PositionBinding.SetValue(TEXT("Particles.MuzzleRenderPosition"),Emitter,Sprite->SourceMode);
			Sprite->VelocityBinding.SetValue(TEXT("Particles.MuzzleRenderVelocity"),Emitter,Sprite->SourceMode);
			Sprite->SpriteSizeBinding.SetValue(TEXT("Particles.MuzzleRenderSize"),Emitter,Sprite->SourceMode);
			Sprite->SpriteFacingBinding.SetValue(TEXT("Particles.MuzzleRenderFacing"),Emitter,Sprite->SourceMode);
			Sprite->SpriteAlignmentBinding.SetValue(TEXT("Particles.MuzzleRenderAlignment"),Emitter,Sprite->SourceMode);
			Sprite->PrevPositionBinding.SetValue(TEXT("Particles.Previous.MuzzleRenderPosition"),Emitter,Sprite->SourceMode);
		}
		if (auto* Mesh=Cast<UNiagaraMeshRendererProperties>(Renderer))
		{
			Mesh->PositionBinding.SetValue(TEXT("Particles.MuzzleRenderPosition"),Emitter,Mesh->SourceMode);
			Mesh->VelocityBinding.SetValue(TEXT("Particles.MuzzleRenderVelocity"),Emitter,Mesh->SourceMode);
			Mesh->ScaleBinding.SetValue(TEXT("Particles.MuzzleRenderScale"),Emitter,Mesh->SourceMode);
			Mesh->MeshOrientationBinding.SetValue(TEXT("Particles.MuzzleRenderOrientation"),Emitter,Mesh->SourceMode);
			Mesh->PrevPositionBinding.SetValue(TEXT("Particles.Previous.MuzzleRenderPosition"),Emitter,Mesh->SourceMode);
		}
		if (auto* Ribbon=Cast<UNiagaraRibbonRendererProperties>(Renderer))
		{
			const auto Source=ENiagaraRendererSourceDataMode::Particles;
			Ribbon->PositionBinding.SetValue(TEXT("Particles.MuzzleRenderPosition"),Emitter,Source);
			Ribbon->VelocityBinding.SetValue(TEXT("Particles.MuzzleRenderVelocity"),Emitter,Source);
			Ribbon->RibbonWidthBinding.SetValue(TEXT("Particles.MuzzleRenderWidth"),Emitter,Source);
			Ribbon->RibbonFacingBinding.SetValue(TEXT("Particles.MuzzleRenderRibbonFacing"),Emitter,Source);
			Ribbon->RibbonIdBinding.SetValue(TEXT("Particles.MuzzleIdentity"),Emitter,Source);
		}
	}
	M.Graph->NotifyGraphChanged(); FinalizeScratchPins(System); System->MarkPackageDirty(); return M.bValid;
}

bool UGuLiCombatEffectAuthoringLibrary::ConfigureToolLaserEndpointLOD(UNiagaraSystem* System,FString& Error)
{
	if (!System || !System->GetName().EndsWith(TEXT("_ThreeTier"))
		|| (!System->GetOutermost()->GetName().StartsWith(TEXT("/Game/GuLiStrike/FX/Mining/"))
		&& !System->GetOutermost()->GetName().StartsWith(TEXT("/Game/GuLiStrike/Buildings/Construction/")))) return false;
	System->Modify(); int32 Count=0;
	for (const auto& Handle:System->GetEmitterHandles())
	{
		if (Handle.GetName()!=TEXT("Spark") && Handle.GetName()!=TEXT("Spark001")) continue;
		auto* Data=Handle.GetEmitterData(); auto* Source=Data ? Cast<UNiagaraScriptSource>(Data->GraphSource) : nullptr;
		if (!Source || !Source->NodeGraph) return false;
		TArray<UNiagaraNodeFunctionCall*> Calls; Source->NodeGraph->GetNodesOfClass(Calls);
		for (auto* Call:Calls) if (Call->FunctionScript &&
			(Call->FunctionScript->GetName()==TEXT("SpawnRate") || Call->FunctionScript->GetName()==TEXT("SpawnBurst_Instantaneous")))
		{
			const bool bBurst=Call->FunctionScript->GetName()==TEXT("SpawnBurst_Instantaneous");
			const FString InputName=bBurst ? TEXT("Spawn Count") : TEXT("SpawnRate");
			const FNiagaraTypeDefinition InputType=bBurst ? FNiagaraTypeDefinition::GetIntDef() : FNiagaraTypeDefinition::GetFloatDef();
			const FName MetaName(TEXT("User.GuLiEndpointBaseSpawnRate_")+Handle.GetName().ToString());
			const FName UserName((bBurst ? TEXT("User.EndpointSpawnCount_") : TEXT("User.EndpointSpawnRate_"))+Handle.GetName().ToString());
			const FNiagaraVariable Meta(FNiagaraTypeDefinition::GetFloatDef(),MetaName), User(InputType,UserName);
			const FNiagaraVariable Old(InputType,FName(TEXT("Constants.")+Handle.GetName().ToString()+TEXT(".")+Call->GetFunctionName()+TEXT(".")+InputName));
			float Rate=-1;
			if (System->GetExposedParameters().IndexOf(Meta)!=INDEX_NONE) Rate=System->GetExposedParameters().GetParameterValue<float>(Meta);
			else if (Data->EmitterUpdateScriptProps.Script->RapidIterationParameters.IndexOf(Old)!=INDEX_NONE)
				Rate=bBurst ? float(Data->EmitterUpdateScriptProps.Script->RapidIterationParameters.GetParameterValue<int32>(Old)) : Data->EmitterUpdateScriptProps.Script->RapidIterationParameters.GetParameterValue<float>(Old);
			if (!FMath::IsFinite(Rate) || Rate<0) { Error=TEXT("Authored endpoint spawn rate missing"); return false; }
			System->GetExposedParameters().SetParameterValue<float>(Rate,Meta,true);
			if (bBurst) System->GetExposedParameters().SetParameterValue<int32>(FMath::RoundToInt(Rate),User,true);
			else System->GetExposedParameters().SetParameterValue<float>(Rate,User,true);
			auto& Pin=FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(*Call,FNiagaraParameterHandle(FName(Call->GetFunctionName()+TEXT(".")+InputName)),InputType,FGuid(),FGuid());
			if (Pin.LinkedTo.IsEmpty()) FNiagaraStackGraphUtilities::SetLinkedParameterValueForFunctionInput(Pin,User,TSet<FNiagaraVariableBase>{User});
			else if (Pin.LinkedTo.Num()!=1 || Pin.LinkedTo[0]->PinName!=UserName) { Error=TEXT("Unexpected endpoint rate override"); return false; }
			TArray<UNiagaraScript*> Scripts{System->GetSystemSpawnScript(),System->GetSystemUpdateScript()}; Data->GetScripts(Scripts);
			for (auto* Script:Scripts) if (Script && Script->RapidIterationParameters.RemoveParameter(Old)) Script->MarkScriptAndSourceDesynchronized(TEXT("Endpoint LOD rate"),FGuid());
			++Count;
		}
		Source->NodeGraph->NotifyGraphChanged();
	}
	System->MarkPackageDirty(); if (Count==0) Error=TEXT("No endpoint spawn modules"); return Count>0;
}

bool UGuLiCombatEffectAuthoringLibrary::WireWingmanMeshReader(UNiagaraSystem* System,UNiagaraScript* Script,bool bTrail,FString& Error)
{
	if (!InScope(System) || !System->GetName().StartsWith(TEXT("NS_WingmanGroundFlight_")) || !Script || Script->GetOutermost()!=System->GetOutermost()) return false;
	FModuleGraph Module; if (!Module.Initialize(Script)) return false;
	auto* Exec=NewObject<UNiagaraNodeOp>(Module.Graph,NAME_None,RF_Transactional); Exec->OpName=TEXT("Util::ExecIndex");
	Module.Graph->AddNode(Exec,false,false); Exec->CreateNewGuid(); Exec->PostPlacedNewNode(); Exec->AllocateDefaultPins();
	auto* Row=AddImpactHlsl(Module,bTrail ? TEXT("int i=Index; Row=(i/8)*9+1+(i % 8);") : TEXT("Row=Index*9;"),
		{FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(),TEXT("Index"))},{FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(),TEXT("Row"))});
	Module.Link(Exec->GetOutputPin(0),Row->FindPin(TEXT("Index"),EGPD_Input));
	for (const auto& Binding:TArray<TTuple<UClass*,FName,FName>>{
		{UNiagaraDataInterfaceArrayPosition::StaticClass(),TEXT("User.WingmanPositions"),TEXT("Particles.Position")},
		{UNiagaraDataInterfaceArrayFloat3::StaticClass(),TEXT("User.WingmanScales"),TEXT("Particles.Scale")},
		{UNiagaraDataInterfaceArrayQuat::StaticClass(),TEXT("User.WingmanOrientations"),TEXT("Particles.MeshOrientation")},
		{UNiagaraDataInterfaceArrayColor::StaticClass(),TEXT("User.WingmanColors"),TEXT("Particles.Color")}})
	{
		UClass* Class=Binding.Get<0>(); const FNiagaraTypeDefinition Type(Class); const FNiagaraVariable Variable(Type,Binding.Get<1>());
		auto& Params=System->GetExposedParameters();
		if (Params.IndexOf(Variable)==INDEX_NONE) { Params.AddParameter(Variable); Params.SetDataInterface(NewObject<UNiagaraDataInterface>(System,Class,NAME_None,RF_Transactional),Variable); }
		TArray<FNiagaraFunctionSignature> Signatures; Class->GetDefaultObject<UNiagaraDataInterface>()->GetFunctionSignatures(Signatures);
		const auto* Signature=Signatures.FindByPredicate([](const auto& S) { return S.Name==TEXT("Get"); }); if (!Signature || Signature->Outputs.Num()!=1) return false;
		auto* Node=NewObject<UNiagaraNodeFunctionCall>(Module.Graph,NAME_None,RF_Transactional); Node->Signature=*Signature;
		Module.Graph->AddNode(Node,false,false); Node->CreateNewGuid(); Node->PostPlacedNewNode(); Node->AllocateDefaultPins();
		Module.Link(Read(Module.Get,Type,Binding.Get<1>()),Node->FindPin(TEXT("Array interface"),EGPD_Input));
		Module.Link(Row->FindPin(TEXT("Row"),EGPD_Output),Node->FindPin(TEXT("Index"),EGPD_Input));
		Module.Link(Node->FindPin(TEXT("Value"),EGPD_Output),Write(Module.Set,Signature->Outputs[0].GetType(),Binding.Get<2>()));
	}
	Module.Graph->NotifyGraphChanged(); FinalizeScratchPins(System); System->MarkPackageDirty();
	if (!Module.bValid) Error=TEXT("Wingman mesh reader connection failed"); return Module.bValid;
}

bool UGuLiCombatEffectAuthoringLibrary::ConfigureWingmanMeshSystem(UNiagaraSystem* System,FString& Error)
{
	if (!InScope(System) || System->GetName()!=TEXT("NS_WingmanGroundFlight_SphereTrail")) return false;
	System->Modify();
	for (const auto& Handle:System->GetEmitterHandles())
	{
		auto* Data=Handle.GetEmitterData(); auto* Emitter=Handle.GetInstance().Emitter.Get(); if (!Data || !Emitter) return false;
		Emitter->Modify(); Data->SimTarget=ENiagaraSimTarget::GPUComputeSim; Data->bLocalSpace=false;
		Data->CalculateBoundsMode=ENiagaraEmitterCalculateBoundMode::Fixed; Data->FixedBounds=FBox(FVector(-10000),FVector(10000));
		for (auto* Renderer:Data->GetRenderers()) if (auto* Mesh=Cast<UNiagaraMeshRendererProperties>(Renderer))
		{
			Mesh->Modify(); Mesh->Meshes.SetNum(1); Mesh->SortMode=ENiagaraSortMode::None;
			Mesh->Meshes[0].Mesh=LoadObject<UStaticMesh>(nullptr,Handle.GetName()==TEXT("Head") ? TEXT("/Engine/BasicShapes/Sphere.Sphere") : TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
			if (!Mesh->Meshes[0].Mesh) return false;
		}
		FPropertyChangedEvent Changed(FindFProperty<FEnumProperty>(FVersionedNiagaraEmitterData::StaticStruct(),TEXT("SimTarget")));
		Emitter->PostEditChangeVersionedProperty(Changed,Handle.GetInstance().Version);
	}
	System->GetExposedParameters().SetParameterValue<float>(0,FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.GuLiWingmanInputVersion")),true);
	System->PostEditChange(); System->MarkPackageDirty(); return true;
}

FString UGuLiCombatEffectAuthoringLibrary::GetWingmanCoreBaseline(UNiagaraSystem* System)
{
	if (!InScope(System) || System->GetName()!=TEXT("NS_WM01_MissileFlight")) return TEXT("{}");
	auto Result=MakeShared<FJsonObject>(); Result->SetStringField(TEXT("system"),System->GetPathName());
	for (const auto& Handle:System->GetEmitterHandles()) if (Handle.GetName()==TEXT("Core"))
	{
		const auto* Data=Handle.GetEmitterData(); Result->SetBoolField(TEXT("local_space"),Data->bLocalSpace);
		for (auto* Renderer:Data->GetRenderers()) if (auto* Mesh=Cast<UNiagaraMeshRendererProperties>(Renderer))
			if (!Mesh->Meshes.IsEmpty() && Mesh->Meshes[0].Mesh)
			{
				const FBox Bounds=Mesh->Meshes[0].Mesh->GetBoundingBox();
				Result->SetStringField(TEXT("mesh"),Mesh->Meshes[0].Mesh->GetPathName());
				Result->SetNumberField(TEXT("mesh_y_diameter"),Bounds.GetSize().Y); Result->SetNumberField(TEXT("mesh_z_diameter"),Bounds.GetSize().Z);
				FString Text; auto* Property=FindFProperty<FArrayProperty>(Mesh->GetClass(),TEXT("Meshes"));
				Property->ExportText_InContainer(0,Text,Mesh,Mesh,Mesh,PPF_None); Result->SetStringField(TEXT("renderer_mesh_properties"),Text);
			}
		TArray<TSharedPtr<FJsonValue>> Hlsl;
		ForEachObjectWithOuter(Handle.GetInstance().Emitter.Get(),[&Hlsl](UObject* Object)
		{
			if (auto* Node=Cast<UNiagaraNodeCustomHlsl>(Object))
			{
				auto* Property=FindFProperty<FStrProperty>(Node->GetClass(),TEXT("CustomHlsl"));
				Hlsl.Add(MakeShared<FJsonValueString>(Property->GetPropertyValue_InContainer(Node)));
			}
		},true);
		Result->SetArrayField(TEXT("core_hlsl"),Hlsl);
	}
	FString Text; FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Text)); return Text;
}

FString UGuLiCombatEffectAuthoringLibrary::InspectImpactBatchComponent(UNiagaraComponent* Component)
{
	if (!IsValid(Component) || (!IsImpactBatch(Component->GetAsset()) && !IsMuzzleBatch(Component->GetAsset())) || !Component->GetWorld()
		|| Component->GetWorld()->WorldType!=EWorldType::PIE) return TEXT("{}");
	auto Controller=Component->GetSystemInstanceController(); if (!Controller.IsValid()) return TEXT("{}");
	Controller->WaitForConcurrentTickAndFinalize();
	const auto* Instance=Controller->GetSystemInstance_Unsafe(); if (!Instance) return TEXT("{}");
	auto Result=MakeShared<FJsonObject>(); Result->SetStringField(TEXT("world"),Component->GetWorld()->GetPathName());
	Result->SetNumberField(TEXT("frame"),double(GFrameCounter));
	TArray<TSharedPtr<FJsonValue>> Emitters;
	for (const auto& Emitter:Instance->GetEmitters())
	{
		if (!Emitter->GetEmitterHandle().GetIsEnabled()) continue;
		const auto& Data=Emitter->GetParticleData(); const auto* Buffer=Data.GetCurrentData();
		const int32 Count=Buffer ? Buffer->GetNumInstances() : 0;
		auto Entry=MakeShared<FJsonObject>(); Entry->SetStringField(TEXT("emitter"),Emitter->GetEmitterHandle().GetName().ToString());
		Entry->SetNumberField(TEXT("particles"),Count);
		const auto Slot=FNiagaraDataSetAccessor<int32>(Data,BatchName(Component->GetAsset(),TEXT("ImpactSlot"))).GetReader(Data);
		const auto Generation=FNiagaraDataSetAccessor<int32>(Data,BatchName(Component->GetAsset(),TEXT("ImpactGeneration"))).GetReader(Data);
		auto Identity=FNiagaraDataSetAccessor<FNiagaraID>(Data,BatchName(Component->GetAsset(),TEXT("ImpactIdentity"))).GetReader(Data);
		if (!Identity.IsValid()) Identity=FNiagaraDataSetAccessor<FNiagaraID>(Data,TEXT("RibbonID")).GetReader(Data);
		const auto Position=FNiagaraDataSetAccessor<FNiagaraPosition>(Data,TEXT("Position")).GetReader(Data);
		const auto Age=FNiagaraDataSetAccessor<float>(Data,BatchName(Component->GetAsset(),TEXT("ImpactAge"))).GetReader(Data);
		const auto InputPosition=FNiagaraDataSetAccessor<FNiagaraPosition>(Data,BatchName(Component->GetAsset(),TEXT("ImpactPosition"))).GetReader(Data);
		const auto InputScale=FNiagaraDataSetAccessor<FVector3f>(Data,BatchName(Component->GetAsset(),TEXT("ImpactScale"))).GetReader(Data);
		const auto InputRotation=FNiagaraDataSetAccessor<FQuat4f>(Data,BatchName(Component->GetAsset(),TEXT("ImpactRotation"))).GetReader(Data);
		const auto InputTint=FNiagaraDataSetAccessor<FLinearColor>(Data,BatchName(Component->GetAsset(),TEXT("ImpactTint"))).GetReader(Data);
		const auto InputSeed=FNiagaraDataSetAccessor<int32>(Data,BatchName(Component->GetAsset(),TEXT("ImpactSeed"))).GetReader(Data);
		const auto InputLifetime=FNiagaraDataSetAccessor<float>(Data,BatchName(Component->GetAsset(),TEXT("ImpactLifetime"))).GetReader(Data);
		const auto RenderPosition=FNiagaraDataSetAccessor<FNiagaraPosition>(Data,TEXT("MuzzleRenderPosition")).GetReader(Data);
		Entry->SetBoolField(TEXT("identity_attributes_valid"),Slot.IsValid() && Generation.IsValid());
		Entry->SetBoolField(TEXT("position_attribute_valid"),Position.IsValid());
		TArray<TSharedPtr<FJsonValue>> Variables;
		for (const auto& Variable:Data.GetVariables()) Variables.Add(MakeShared<FJsonValueString>(Variable.GetName().ToString()));
		Entry->SetArrayField(TEXT("variables"),Variables);
		TArray<TSharedPtr<FJsonValue>> Rows; TSet<FString> Identities;
		for (int32 I=0;I<Count;++I)
		{
			const int32 S=Slot.GetSafe(I,-1), G=Generation.GetSafe(I,-1);
			Identities.Add(FString::Printf(TEXT("%d:%d"),S,G));
			if (I>=32) continue;
			auto Row=MakeShared<FJsonObject>(); Row->SetNumberField(TEXT("slot"),S); Row->SetNumberField(TEXT("generation"),G);
			if (Identity.IsValid()) { const auto Id=Identity.Get(I); Row->SetNumberField(TEXT("ribbon_slot"),Id.Index); Row->SetNumberField(TEXT("ribbon_generation"),Id.AcquireTag); }
			const auto P=Position.GetSafe(I,FNiagaraPosition(0,0,0));
			Row->SetArrayField(TEXT("position"),{MakeShared<FJsonValueNumber>(P.X),MakeShared<FJsonValueNumber>(P.Y),MakeShared<FJsonValueNumber>(P.Z)});
			Row->SetNumberField(TEXT("age"),Age.GetSafe(I,-1));
			if (RenderPosition.IsValid()) { const auto V=RenderPosition.Get(I); Row->SetArrayField(TEXT("render_position"),{MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)}); }
			if (InputPosition.IsValid()) { const auto V=InputPosition.Get(I); Row->SetArrayField(TEXT("input_position"),{MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)}); }
			if (InputScale.IsValid()) { const auto V=InputScale.Get(I); Row->SetArrayField(TEXT("input_scale"),{MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)}); }
			if (InputRotation.IsValid()) { const auto V=InputRotation.Get(I); Row->SetArrayField(TEXT("input_rotation"),{MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z),MakeShared<FJsonValueNumber>(V.W)}); }
			if (InputTint.IsValid()) { const auto V=InputTint.Get(I); Row->SetArrayField(TEXT("input_tint"),{MakeShared<FJsonValueNumber>(V.R),MakeShared<FJsonValueNumber>(V.G),MakeShared<FJsonValueNumber>(V.B),MakeShared<FJsonValueNumber>(V.A)}); }
			if (InputSeed.IsValid()) Row->SetNumberField(TEXT("input_seed"),InputSeed.Get(I));
			if (InputLifetime.IsValid()) Row->SetNumberField(TEXT("input_lifetime"),InputLifetime.Get(I));
			Rows.Add(MakeShared<FJsonValueObject>(Row));
		}
		Entry->SetNumberField(TEXT("distinct_identities"),Identities.Num()); Entry->SetArrayField(TEXT("rows"),Rows);
		Emitters.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Result->SetArrayField(TEXT("emitters"),Emitters); FString Json; auto Writer=TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Result,Writer); return Json;
}

FString UGuLiCombatEffectAuthoringLibrary::GetImpactCompiledSource(UNiagaraSystem* System)
{
	if (!IsImpactBatch(System) && !IsMuzzleBatch(System)) return TEXT("{}");
	auto Result=MakeShared<FJsonObject>();
	for (const auto& Handle:System->GetEmitterHandles())
	{
		const auto* Data=Handle.GetEmitterData(); if (!Data) continue;
		auto Entry=MakeShared<FJsonObject>();
		for (const auto& Pair:TArray<TPair<FString,UNiagaraScript*>>{{TEXT("spawn"),Data->SpawnScriptProps.Script},{TEXT("update"),Data->UpdateScriptProps.Script}})
			if (Pair.Value) Entry->SetStringField(Pair.Key,Pair.Value->GetVMExecutableData().LastHlslTranslation);
		for (int32 I=0;I<Data->EventHandlerScriptProps.Num();++I)
			if (auto* Script=Data->EventHandlerScriptProps[I].Script.Get()) Entry->SetStringField(FString::Printf(TEXT("event%d"),I),Script->GetVMExecutableData().LastHlslTranslation);
		Result->SetObjectField(Handle.GetName().ToString(),Entry);
	}
	FString Json; FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Json)); return Json;
}
#endif
