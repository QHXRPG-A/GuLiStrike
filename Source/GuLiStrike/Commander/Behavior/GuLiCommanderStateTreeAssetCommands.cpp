#include "GuLiStrike.h"

#if WITH_EDITOR
#include "Commander/Behavior/GuLiCommanderBehaviorSchema.h"
#include "Commander/Behavior/GuLiCommanderStateTreeNodes.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "Serialization/JsonSerializer.h"
#include "StateTree.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorModule.h"
#include "StateTreeEditorSchema.h"
#include "StateTreePropertyBindings.h"
#include "StateTreeState.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace GuLiCommanderStateTreeAssets
{
	void WriteReport(const TCHAR* Name, const TSharedRef<FJsonObject>& Report)
	{
		const FString Directory = FPaths::ProjectDir() / TEXT("Artifacts/CommanderStateTree");
		IFileManager::Get().MakeDirectory(*Directory, true);
		FString Json;
		FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
		if (!FFileHelper::SaveStringToFile(Json, *(Directory / Name), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			UE_LOG(LogGuLiStrike, Error, TEXT("Failed to save Commander StateTree authoring report."));
	}

	struct FTreeSpec
	{
		const TCHAR* Name;
		EGuLiCommanderBehavior Behavior;
		const TCHAR* DisplayName;
		bool bMass;
		EGuLiTaskLifetime Lifetime;
	};
	const FTreeSpec Specs[] = {
		{TEXT("ST_CommanderMass"), EGuLiCommanderBehavior::StrongholdAdvance, TEXT("据点推进"), true, EGuLiTaskLifetime::InitialOnce},
		{TEXT("ST_CommanderMiner"), EGuLiCommanderBehavior::Mining, TEXT("采矿"), false, EGuLiTaskLifetime::Persistent},
		{TEXT("ST_CommanderBuilder"), EGuLiCommanderBehavior::Construction, TEXT("建造"), false, EGuLiTaskLifetime::Persistent}
	};

	bool Build(const FTreeSpec& Spec)
	{
		const FString PackageName = FString(TEXT("/Game/GuLiStrike/Commander/Behavior/")) + Spec.Name;
		const FString ObjectPath = PackageName + TEXT(".") + Spec.Name;
        UStateTree* Tree = LoadObject<UStateTree>(nullptr,*ObjectPath);
        const bool bExisting = Tree != nullptr;
        if (Tree)
        {
            const auto* Policy = GuLiCommanderBehavior::GetPolicy(Tree);
            if (!Policy || Policy->Behavior!=Spec.Behavior || Policy->Lifetime!=Spec.Lifetime) return false;
            if (Spec.bMass) return true;
            Tree->Modify(); // Preserve asset identity and references when upgrading the generated engineering graph.
        }
        else if (FPackageName::DoesPackageExist(PackageName)) return false;
        UPackage* Package = Tree ? Tree->GetOutermost() : CreatePackage(*PackageName);
        if (!Tree) Tree = NewObject<UStateTree>(Package,Spec.Name,RF_Public|RF_Standalone);
		const TSubclassOf<UStateTreeSchema> SchemaClass = Spec.bMass
			? UGuLiCommanderMassStateTreeSchema::StaticClass() : UGuLiCommanderActorStateTreeSchema::StaticClass();
		auto& EditorModule = FStateTreeEditorModule::GetModule();
		auto* Data = NewObject<UStateTreeEditorData>(Tree, EditorModule.GetEditorDataClass(SchemaClass.Get()), MakeUniqueObjectName(Tree, EditorModule.GetEditorDataClass(SchemaClass.Get()), TEXT("CommanderBehaviorEditorData")), RF_Transactional);
		Tree->EditorData = Data;
		Data->Schema = NewObject<UStateTreeSchema>(Data, SchemaClass, TEXT("CommanderSchema"), RF_Transactional);
		// Both Commander schemas use the standard editor layout. GetEditorSchemaClass is not DLL-exported in UE 5.7.
		Data->EditorSchema = NewObject<UStateTreeEditorSchema>(Data, TEXT("CommanderEditorSchema"), RF_Transactional);
		auto& Policy = Spec.bMass ? CastChecked<UGuLiCommanderMassStateTreeSchema>(Data->Schema)->Policy
			: CastChecked<UGuLiCommanderActorStateTreeSchema>(Data->Schema)->Policy;
		Policy.Behavior = Spec.Behavior; Policy.DisplayName = Spec.DisplayName;
		Policy.bAutoActivate = true; Policy.Lifetime = Spec.Lifetime;
		auto& Root = Data->AddSubTree(TEXT("CommanderOrders"));
		Root.Description = TEXT("Authority task selection; clocked by the existing 10 Hz dispatcher. Business components execute requests outside Mass iteration.");
		Root.SelectionBehavior = EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder;
		auto Add = [&](const TCHAR* Name, uint32 Required, uint32 Forbidden, EGuLiCommanderBehaviorStep Step,
            EGuLiCommanderWorkPhase Phase = EGuLiCommanderWorkPhase::Any, EGuLiCommanderWorkResult Result = EGuLiCommanderWorkResult::Any)
		{
			auto& State = Root.AddChildState(FName(Name));
			State.SelectionBehavior = EStateTreeStateSelectionBehavior::TryEnterState;
			if (Spec.bMass)
			{
				State.AddEnterCondition<FGuLiCommanderMassBehaviorCondition>(Required, Forbidden, Phase, Result);
				State.AddTask<FGuLiCommanderMassBehaviorTask>(Step);
			}
			else
			{
				State.AddEnterCondition<FGuLiCommanderActorBehaviorCondition>(Required, Forbidden, Phase, Result);
				State.AddTask<FGuLiCommanderActorBehaviorTask>(Step);
			}
			State.AddTransition(EStateTreeTransitionTrigger::OnStateSucceeded, EStateTreeTransitionType::GotoState, &Root);
		};

        using namespace GuLiCommanderBehaviorFacts;
        using Step = EGuLiCommanderBehaviorStep;
        using Phase = EGuLiCommanderWorkPhase;
        using Result = EGuLiCommanderWorkResult;
        const uint32 Blocked = CancelPending | Stopped | PendingMove;
        Add(TEXT("WaitingSafeExit"), CancelPending, 0, Step::CancelPending);
        Add(TEXT("Stopped"), Stopped, CancelPending, Step::Wait);
        Add(TEXT("PreparingReplacementMove"), PendingMove, CancelPending | Stopped, Step::ReplaceMove);
        Add(TEXT("SuspendedByExternalControl"), Active | Suspended, Blocked, Step::RunTask);
        Add(TEXT("StartTask"), Active, Blocked | Started, Step::RunTask);
        Add(TEXT("WorkUnitFinished"), Active | Started | WorkComplete, Blocked, Step::RunTask);
        Add(TEXT("ManualMove"), Active | GuLiCommanderBehaviorFacts::Move, Blocked | Automatic, Step::RunTask);
        if (!Spec.bMass) Add(TEXT("ManualTransit"), Active | Transit, Blocked | Automatic, Step::RunTask);
        if (Spec.Behavior == EGuLiCommanderBehavior::Mining)
        {
            for (uint32 Kind : {Mining, Return})
            {
                const uint32 Required = Active | Started | Kind;
                auto MineState = [&](const TCHAR* Label, Phase P, Result R, Step Operation, uint32 Extra = 0, uint32 Exclude = 0)
                {
                    const FString Name = FString(Kind == Mining ? TEXT("Mining_") : TEXT("Return_")) + Label;
                    Add(*Name, Required | Extra, Blocked | Exclude, Operation, P, R);
                };
                MineState(TEXT("SelectFactoryWithCargo"), Phase::MiningIdle, Result::None, Step::MiningFactory, ReturnFirst | RetryReady);
                if (Kind == Mining) MineState(TEXT("SelectMineTarget"), Phase::MiningIdle, Result::None, Step::MiningSelect, RetryReady, ReturnFirst);
                if (Kind == Mining) MineState(TEXT("WaitForMiningPosition"), Phase::MiningWaitingPosition, Result::WaitingPosition, Step::MiningSelect, RetryReady);
                MineState(TEXT("WaitForBudgetedMiningPath"), Phase::MiningWaitingPath, Result::Running, Step::RunTask);
                MineState(TEXT("MoveToMine"), Phase::MiningReserved, Result::TargetReady, Step::MiningMove);
                MineState(TEXT("BeginExtraction"), Phase::MiningMoving, Result::CanMine, Step::MiningExtract);
                MineState(TEXT("RepositionForExtraction"), Phase::Any, Result::OutOfRange, Step::MiningReposition);
                MineState(TEXT("RejectUnreachableMiningPosition"), Phase::MiningMoving, Result::Failed, Step::MiningReposition);
                MineState(TEXT("CargoFull_SelectFactory"), Phase::MiningExtracting, Result::Full, Step::MiningFactory);
                MineState(TEXT("MineDepleted_SelectFactory"), Phase::MiningExtracting, Result::Depleted, Step::MiningFactory);
                MineState(TEXT("LostMine_ReturnCargo"), Phase::Any, Result::TargetLost, Step::MiningFactory, Cargo);
                MineState(TEXT("LostMine_Retry"), Phase::Any, Result::TargetLost, Step::MiningRetry, Automatic, Cargo);
                MineState(TEXT("LostMine_ManualFailure"), Phase::Any, Result::TargetLost, Step::MiningFail, 0, Cargo | Automatic);
                MineState(TEXT("NoTarget_Retry"), Phase::Any, Result::NoTarget, Step::MiningRetry, Automatic);
                MineState(TEXT("NoTarget_ManualFailure"), Phase::Any, Result::NoTarget, Step::MiningFail, 0, Automatic);
                MineState(TEXT("TryNextUnloadPoint"), Phase::MiningReturning, Result::Failed, Step::MiningReturn);
                MineState(TEXT("UnreachableFactory_SelectNext"), Phase::MiningReturning, Result::FactoryUnreachable, Step::MiningFactory);
                MineState(TEXT("FailedAction_Retry"), Phase::Any, Result::Failed, Step::MiningRetry, Automatic);
                MineState(TEXT("FailedAction_ManualFailure"), Phase::Any, Result::Failed, Step::MiningFail, 0, Automatic);
                MineState(TEXT("ReturnToFactory"), Phase::MiningIdle, Result::FactoryReady, Step::MiningReturn);
                MineState(TEXT("FactoryLost_SelectAgain"), Phase::Any, Result::FactoryLost, Step::MiningFactory);
                MineState(TEXT("Unload"), Phase::MiningReturning, Result::AtFactory, Step::MiningUnload);
                MineState(TEXT("FinishCycle"), Phase::MiningUnloading, Result::Unloaded, Step::MiningFinish);
                MineState(TEXT("WaitForActionOrRetry"), Phase::Any, Result::Any, Step::RunTask);
            }
        }
        else if (Spec.Behavior == EGuLiCommanderBehavior::Construction)
        {
            Add(TEXT("SelectConstructionDestination"), Active | Started | Construction, Blocked, Step::ConstructionReserve, Phase::ConstructionPrepared, Result::None);
            Add(TEXT("ReturnCancelledConstructionOrder"), Active | Started | Construction, Blocked, Step::RunTask, Phase::ConstructionCancelled, Result::Any);
            Add(TEXT("MoveToConstructionSite"), Active | Started | Construction, Blocked, Step::ConstructionMove, Phase::ConstructionIntended, Result::TargetReady);
            Add(TEXT("WaitForBudgetedConstructionPath"), Active | Started | Construction, Blocked, Step::RunTask, Phase::ConstructionWaitingPath, Result::Running);
            Add(TEXT("RetryConstructionPosition"), Active | Started | Construction, Blocked, Step::ConstructionRetry, Phase::Any, Result::OutOfRange);
            Add(TEXT("ClaimPositionOnArrivalAndConstruct"), Active | Started | Construction, Blocked, Step::ConstructionWork, Phase::ConstructionMoving, Result::Arrived);
            Add(TEXT("WorkingOrWaitingForArrival"), Active | Started | Construction, Blocked, Step::RunTask);
        }
        else
        {
            const uint32 Required = Active | Started | Advance;
            Add(TEXT("SelectStronghold"), Required, Blocked, Step::AdvanceSelect, Phase::AdvanceSelecting, Result::None);
            Add(TEXT("MoveToStronghold"), Required, Blocked, Step::AdvanceMove, Phase::AdvanceSelecting, Result::TargetReady);
            Add(TEXT("NoStronghold_Wait"), Required, Blocked, Step::AdvanceWait, Phase::AdvanceSelecting, Result::NoTarget);
            Add(TEXT("WaitForCapture"), Required, Blocked, Step::AdvanceCapture, Phase::AdvanceMoving, Result::Arrived);
            Add(TEXT("RejectUnreachableStronghold"), Required, Blocked, Step::AdvanceReject, Phase::Any, Result::Failed);
            Add(TEXT("StrongholdCaptured"), Required, Blocked, Step::AdvanceComplete, Phase::Any, Result::Complete);
            Add(TEXT("RetryStrongholdSelection"), Required, Blocked, Step::AdvanceSelect, Phase::AdvanceWaiting, Result::NoTarget);
            Add(TEXT("AdvancingOrCapturing"), Required, Blocked, Step::RunTask);
        }
        Add(TEXT("SelectManualTask"), Queued, Blocked | Active, Step::TakeManual);
        Add(Spec.Behavior == EGuLiCommanderBehavior::Construction ? TEXT("SelectAutomaticConstructionSite") : TEXT("SelectAutomaticTask"),
            AutomaticReady, Blocked | Active | Queued, Step::TakeAutomatic);
        Add(TEXT("Idle"), 0, 0, Step::Wait);
		UStateTreeEditingSubsystem::ValidateStateTree(Tree);
		FStateTreeCompilerLog CompileLog;
		if (!UStateTreeEditingSubsystem::CompileStateTree(Tree, CompileLog))
		{
			CompileLog.DumpToLog(LogGuLiStrike);
			return false;
		}
		if (!bExisting) FAssetRegistryModule::AssetCreated(Tree);
		Tree->MarkPackageDirty();
		if (!UEditorLoadingAndSavingUtils::SavePackages({Package}, false)) return false;
		UE_LOG(LogGuLiStrike, Display, TEXT("Authored Commander StateTree: %s; states=%d; schema=%s"), *ObjectPath, Root.Children.Num(), *GetNameSafe(Tree->GetSchema()));
		return true;
	}

	void BuildAll()
	{
		if (!GEditor || GEditor->PlayWorld)
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Commander StateTree authoring requires an idle editor."));
			return;
		}
		const auto Report = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Assets;
		bool bSuccess = true;
		for (const auto& Spec : Specs)
		{
			const bool bBuilt = Build(Spec);
			bSuccess &= bBuilt;
			const auto Entry = MakeShared<FJsonObject>();
			const FString Path = FString(TEXT("/Game/GuLiStrike/Commander/Behavior/")) + Spec.Name + TEXT(".") + Spec.Name;
			Entry->SetStringField(TEXT("asset"), Path);
			Entry->SetBoolField(TEXT("saved"), bBuilt);
			if (const auto* Tree = LoadObject<UStateTree>(nullptr, *Path))
			{
				Entry->SetBoolField(TEXT("ready"), Tree->IsReadyToRun());
				Entry->SetStringField(TEXT("schema"), Tree->GetSchema() ? Tree->GetSchema()->GetClass()->GetPathName() : FString());
				TArray<TSharedPtr<FJsonValue>> States;
				if (const auto* Data = Cast<UStateTreeEditorData>(Tree->EditorData); Data && !Data->SubTrees.IsEmpty())
					for (const auto& State : Data->SubTrees[0]->Children) States.Add(MakeShared<FJsonValueString>(State->Name.ToString()));
				Entry->SetArrayField(TEXT("states"), States);
			}
			Assets.Add(MakeShared<FJsonValueObject>(Entry));
			if (!bBuilt) break;
		}
		Report->SetBoolField(TEXT("success"), bSuccess);
		Report->SetArrayField(TEXT("assets"), Assets);
		WriteReport(TEXT("tree-assets.json"), Report);
	}

    void BuildBuilder()
    {
        if (!GEditor || GEditor->PlayWorld) return;
        const bool bSuccess = Build(Specs[2]);
        const auto Report = MakeShared<FJsonObject>();
        Report->SetBoolField(TEXT("success"), bSuccess);
        Report->SetStringField(TEXT("asset"), TEXT("/Game/GuLiStrike/Commander/Behavior/ST_CommanderBuilder"));
        WriteReport(TEXT("builder-tree.json"), Report);
    }

	// Refresh native property metadata and compiled data without regenerating authored graphs.
	void RefreshExisting()
	{
		if (!GEditor || GEditor->PlayWorld) return;
		const auto Report = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Properties;
		bool bSuccess = true;
		for (UScriptStruct* Type : {FGuLiCommanderActorBehaviorCondition::StaticStruct(), FGuLiCommanderMassBehaviorCondition::StaticStruct()})
		{
			for (const TCHAR* Name : {TEXT("Required"), TEXT("Forbidden"), TEXT("Phase"), TEXT("Result")})
			{
				const FProperty* Property = FindFProperty<FProperty>(Type, Name);
				const bool bParameter = UE::StateTree::GetUsageFromMetaData(Property) == EStateTreePropertyUsage::Parameter;
				const auto Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("node_type"), Type->GetPathName());
				Entry->SetStringField(TEXT("property"), Name);
				Entry->SetStringField(TEXT("category"), Property ? Property->GetMetaData(TEXT("Category")) : FString());
				Entry->SetBoolField(TEXT("parameter"), bParameter);
				Properties.Add(MakeShared<FJsonValueObject>(Entry));
				bSuccess &= bParameter;
			}
		}
		Report->SetArrayField(TEXT("condition_properties"), Properties);
		TArray<TSharedPtr<FJsonValue>> Assets;
		for (const auto& Spec : Specs)
		{
			if (!bSuccess) break;
			const FString Path = FString(TEXT("/Game/GuLiStrike/Commander/Behavior/")) + Spec.Name + TEXT(".") + Spec.Name;
			UStateTree* Tree = LoadObject<UStateTree>(nullptr, *Path);
			const auto Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("asset"), Path);
			bool bSaved = false;
			if (Tree && Cast<UStateTreeEditorData>(Tree->EditorData))
			{
				const uint32 BeforeHash = UStateTreeEditingSubsystem::CalculateStateTreeHash(Tree);
				FStateTreeCompilerLog CompileLog;
				const bool bCompiled = UStateTreeEditingSubsystem::CompileStateTree(Tree, CompileLog);
				const bool bGraphUnchanged = BeforeHash == UStateTreeEditingSubsystem::CalculateStateTreeHash(Tree);
				Entry->SetBoolField(TEXT("compiled"), bCompiled);
				Entry->SetBoolField(TEXT("ready"), Tree->IsReadyToRun());
				Entry->SetBoolField(TEXT("editor_graph_unchanged"), bGraphUnchanged);
				if (!bCompiled) CompileLog.DumpToLog(LogGuLiStrike);
				if (bCompiled && Tree->IsReadyToRun() && bGraphUnchanged)
				{
					Tree->MarkPackageDirty();
					bSaved = UEditorLoadingAndSavingUtils::SavePackages({Tree->GetOutermost()}, false);
				}
			}
			Entry->SetBoolField(TEXT("saved"), bSaved);
			Assets.Add(MakeShared<FJsonValueObject>(Entry));
			bSuccess &= bSaved;
		}
		Report->SetArrayField(TEXT("assets"), Assets);
		Report->SetBoolField(TEXT("success"), bSuccess);
		WriteReport(TEXT("tree-refresh.json"), Report);
	}

	void RetireSpecialTaskTable()
	{
		if (!GEditor || GEditor->PlayWorld) return;
		auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		const FName Package(TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeSpecialTasks_Tasks"));
		Registry.ScanPathsSynchronous({TEXT("/Game/GuLiStrike/Data")});
		TArray<FName> Referencers;
		Registry.GetReferencers(Package, Referencers);
		Referencers.Remove(Package);
		const auto Report = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> References;
		for (FName Ref : Referencers) References.Add(MakeShared<FJsonValueString>(Ref.ToString()));
		Report->SetArrayField(TEXT("remaining_references"), References);
		const FAssetData Asset = Registry.GetAssetByObjectPath(FSoftObjectPath(Package.ToString() + TEXT(".DT_GuLiStrikeSpecialTasks_Tasks")));
		const bool bDeleted = Referencers.IsEmpty() && (!Asset.IsValid() || ObjectTools::DeleteAssets({Asset}, false) == 1);
		Report->SetBoolField(TEXT("deleted_or_absent"), bDeleted);
		WriteReport(TEXT("retired-table.json"), Report);
		if (!bDeleted) UE_LOG(LogGuLiStrike, Error, TEXT("Old task table still has references or could not be deleted; see retired-table.json."));
	}
	FAutoConsoleCommand BuildCommand(TEXT("gs.Commander.BuildStateTrees"),
		TEXT("Update engineering StateTrees in place; preserve the existing Mass tree."), FConsoleCommandDelegate::CreateStatic(&BuildAll));
	FAutoConsoleCommand RefreshCommand(TEXT("gs.Commander.RefreshStateTrees"),
		TEXT("Recompile and save existing Commander trees without changing their authored graphs."), FConsoleCommandDelegate::CreateStatic(&RefreshExisting));
	FAutoConsoleCommand RetireTableCommand(TEXT("gs.Commander.RetireSpecialTaskTable"),
		TEXT("Delete the retired task DataTable only when no assets reference it."), FConsoleCommandDelegate::CreateStatic(&RetireSpecialTaskTable));
}
static FAutoConsoleCommand GGuLiBuildBuilderTree(
    TEXT("gs.Commander.BuildBuilderStateTree"), TEXT("Update only the autonomous construction StateTree."),
    FConsoleCommandDelegate::CreateStatic(&GuLiCommanderStateTreeAssets::BuildBuilder));

#endif
