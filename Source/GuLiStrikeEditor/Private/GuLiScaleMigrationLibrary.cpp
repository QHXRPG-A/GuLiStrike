#include "GuLiScaleMigrationLibrary.h"

#include "EdGraph/EdGraphPin.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraSystem.h"
#include "Gameplay/Resources/GuLiMiningVehiclePawn.h"
#include "Gameplay/Resources/GuLiResourceFactoryActor.h"
#include "UObject/UnrealType.h"

AGuLiResourceFactoryActor* UGuLiScaleMigrationLibrary::ReadMiningFactory(AGuLiMiningVehiclePawn* Miner)
{
	if (!IsValid(Miner)) return nullptr;
	const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Miner->GetClass(), TEXT("Factory"));
	return Property ? Cast<AGuLiResourceFactoryActor>(Property->GetObjectPropertyValue_InContainer(Miner)) : nullptr;
}


namespace
{
	UNiagaraSystem* MiningSystem()
	{
		return LoadObject<UNiagaraSystem>(nullptr,
			TEXT("/Game/GuLiStrike/FX/Mining/NS_MiningLaser_Green.NS_MiningLaser_Green"));
	}
}

TMap<FString, FString> UGuLiScaleMigrationLibrary::ReadMiningSizeSwitches()
{
	TMap<FString, FString> Result;
	UNiagaraSystem* System = MiningSystem();
	if (!System) return Result;
	for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
		auto* Source = Data ? Cast<UNiagaraScriptSource>(Data->GraphSource) : nullptr;
		if (!Source || !Source->NodeGraph) continue;
		TArray<UNiagaraNodeFunctionCall*> Nodes;
		Source->NodeGraph->GetNodesOfClass(Nodes);
		for (const auto* Node : Nodes)
		{
			const FString Name = Node->GetFunctionName();
			if (Name != TEXT("ScaleSpriteSize") && Name != TEXT("InitializeParticle")) continue;
			for (const UEdGraphPin* Pin : Node->Pins)
				if (Pin && Pin->Direction == EGPD_Input)
					Result.Add(Handle.GetName().ToString() + TEXT(".") + Name + TEXT(".") + Pin->PinName.ToString(), Pin->DefaultValue);
		}
	}
	return Result;
}

bool UGuLiScaleMigrationLibrary::SetMiningSpriteSize020(const FString& Emitter, const FString& Stage,
	const FString& Parameter, const FVector2D ExpectedBefore, const FVector2D Target)
{
	if ((Emitter != TEXT("Spark") && Emitter != TEXT("Spark001"))
		|| ExpectedBefore.ContainsNaN() || Target.ContainsNaN() || !Target.Equals(ExpectedBefore * .2, .0001)) return false;
	const FString Prefix = TEXT("Constants.") + Emitter + TEXT(".");
	if (Parameter != Prefix + TEXT("InitializeParticle.Sprite Size Min")
		&& Parameter != Prefix + TEXT("InitializeParticle.Sprite Size Max")
		&& Parameter != Prefix + TEXT("ScaleSpriteSize.Initial Sprite Size")) return false;
	UNiagaraSystem* System = MiningSystem();
	if (!System) return false;
	for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		if (Handle.GetName().ToString() != Emitter) continue;
		FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
		if (!Data) return false;
		UNiagaraScript* Script = Stage == TEXT("ParticleSpawn") ? Data->SpawnScriptProps.Script.Get()
			: Stage == TEXT("ParticleUpdate") ? Data->UpdateScriptProps.Script.Get() : nullptr;
		if (!Script) return false;
		const FNiagaraVariable Variable(FNiagaraTypeDefinition::GetVec2Def(), FName(Parameter));
		FNiagaraParameterStore& Store = Script->RapidIterationParameters;
		if (Store.IndexOf(Variable) == INDEX_NONE) return false;
		const FVector2f Current = Store.GetParameterValue<FVector2f>(Variable);
		const FVector2D CurrentDouble(Current.X, Current.Y);
		if (CurrentDouble.Equals(Target, .0001)) return true;
		if (!CurrentDouble.Equals(ExpectedBefore, .0001)) return false;
		System->Modify(); Script->Modify();
		Store.SetParameterValue(FVector2f(Target.X, Target.Y), Variable);
		System->MarkPackageDirty();
		return true;
	}
	return false;
}
