#include "Gameplay/CombatEffects/GuLiCombatEffectAuthoringLibrary.h"

#if WITH_EDITOR
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraLightRendererProperties.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "StaticMeshCompiler.h"
#include "Materials/MaterialInterface.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "NiagaraScript.h"
#include "NiagaraShared.h"
#include "NiagaraScriptSource.h"
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
#include "EdGraphSchema_Niagara.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectHash.h"
#include "Misc/PackageName.h"

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
			if (Inputs.Num() != 1 || Outputs.Num() != 1) return false;
			Input = Inputs[0]; Output = Outputs[0];
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

FString UGuLiCombatEffectAuthoringLibrary::GetWarMachineHoverCompileDiagnostics(UNiagaraSystem* System)
{
	if (!System || (System->GetOutermost()->GetName() != TEXT("/Game/GuLiStrike/FX/WarMachineHover/NS_WarMachineHoverPool")
		&& !System->GetOutermost()->GetName().StartsWith(TEXT("/Game/GuLiStrike/FX/WM01Missiles/NS_WM01MissileCluster_"))))
		return TEXT("Unexpected hover system");
	FString Result = FString::Printf(TEXT("System valid=%d ready=%d gpu=%d\n"),
		System->IsValid(), System->IsReadyToRun(), System->HasAnyGPUEmitters());
	Result += FString::Printf(TEXT("Editor needs_compile=%d outstanding_including_gpu=%d\n"),
		System->NeedsRequestCompile(), System->HasOutstandingCompilationRequests(true));
	// Ignore template versions and removed emitters retained by editor undo history.
	TArray<UNiagaraScript*> Scripts { System->GetSystemSpawnScript(), System->GetSystemUpdateScript() };
	for (const auto& Handle : System->GetEmitterHandles())
		if (const auto* Data = Handle.GetEmitterData()) Data->GetScripts(Scripts);
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
#endif
