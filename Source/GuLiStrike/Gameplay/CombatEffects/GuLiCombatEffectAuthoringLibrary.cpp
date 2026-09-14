#include "Gameplay/CombatEffects/GuLiCombatEffectAuthoringLibrary.h"

#if WITH_EDITOR
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "Engine/StaticMesh.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceArrayFloat.h"
#include "NiagaraDataChannel.h"
#include "NiagaraDataChannel_Global.h"
#include "NiagaraNodeInput.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeWithDynamicPins.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOp.h"
#include "EdGraphSchema_Niagara.h"
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
		if (!System || System->GetOutermost()->GetName() != TEXT("/Game/GuLiStrike/FX/WingmanWeapons/NS_WingmanLaserPool")) return false;
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

bool UGuLiCombatEffectAuthoringLibrary::WireLaserPoolReader(UNiagaraSystem* System,
	UNiagaraScript* ParticleUpdateScript, const bool bMuzzle, FString& Error)
{
	const FString Path = TEXT("/Game/GuLiStrike/FX/WingmanWeapons/NS_WingmanLaserPool");
	if (!System || System->GetOutermost()->GetName() != Path || !ParticleUpdateScript
		|| ParticleUpdateScript->GetOutermost() != System->GetOutermost())
	{ Error = TEXT("Laser reader is outside its task-owned system"); return false; }
	FModuleGraph Module;
	if (!Module.Initialize(ParticleUpdateScript)) { Error = TEXT("Laser scratch graph is invalid"); return false; }
	struct FBinding { UClass* Class; FName User; FName Attribute; };
	const TArray<FBinding> Bindings = {
		{UNiagaraDataInterfaceArrayPosition::StaticClass(), bMuzzle ? TEXT("User.MuzzlePositions") : TEXT("User.LaserPositions"), TEXT("Particles.Position")},
		{UNiagaraDataInterfaceArrayFloat3::StaticClass(), TEXT("User.LaserDirections"), TEXT("Particles.SpriteAlignment")},
		{UNiagaraDataInterfaceArrayFloat2::StaticClass(), bMuzzle ? TEXT("User.MuzzleSizes") : TEXT("User.LaserSizes"), TEXT("Particles.SpriteSize")},
		{UNiagaraDataInterfaceArrayColor::StaticClass(), bMuzzle ? TEXT("User.MuzzleColors") : TEXT("User.LaserColors"), TEXT("Particles.Color")}
	};
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
#endif
