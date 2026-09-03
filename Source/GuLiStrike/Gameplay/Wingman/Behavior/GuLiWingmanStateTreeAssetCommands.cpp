// Copyright Epic Games, Inc. All Rights Reserved.

#include "GuLiStrike.h"

#if WITH_EDITOR

#include "Gameplay/Wingman/Behavior/GuLiWingmanBehaviorStateTree.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/StateTreeComponentSchema.h"
#include "Dom/JsonObject.h"
#include "FileHelpers.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "StateTree.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorModule.h"
#include "StateTreeEditorSchema.h"
#include "StateTreeState.h"
#include "UObject/Package.h"

namespace GuLiWingmanStateTreeAssetCommands
{
	constexpr TCHAR PackagePath[] = TEXT("/Game/GuLiStrike/Wingman/ST_WingmanGroupBehavior");
	constexpr TCHAR ObjectPath[] =
		TEXT("/Game/GuLiStrike/Wingman/ST_WingmanGroupBehavior.ST_WingmanGroupBehavior");
	constexpr TCHAR AssetName[] = TEXT("ST_WingmanGroupBehavior");
	constexpr TCHAR RootStateName[] = TEXT("WingmanBehavior");
	constexpr float PolicyTickSeconds = 0.2f;
	constexpr TCHAR AuditRelativePath[] =
		TEXT("TestResults/WingmanPlan/WingmanStateTreeAsset/state_tree_asset_audit.json");

	struct FPolicyDescription
	{
		EGuLiWingmanBehaviorPolicy Policy;
		const TCHAR* Description;
	};

	constexpr FPolicyDescription Policies[] = {
		{EGuLiWingmanBehaviorPolicy::Dead, TEXT("No live members; policy cannot move or damage entities.")},
		{EGuLiWingmanBehaviorPolicy::OwnerUnavailable, TEXT("Lease owner/config/carrier source is unavailable; suppress Candidate output.")},
		{EGuLiWingmanBehaviorPolicy::EmergencyAvoid, TEXT("Avoidance is active; request recovery Guidance through flight mode only.")},
		{EGuLiWingmanBehaviorPolicy::JoiningEscort, TEXT("One or more members must catch up to the carrier formation.")},
		{EGuLiWingmanBehaviorPolicy::EscortOrbit, TEXT("Stable double-ring escort/orbit behavior.")}
	};

	bool ValidateEditorContract(const UStateTree* StateTree, FString& OutError)
	{
		if (!StateTree || !StateTree->IsReadyToRun()
			|| !StateTree->GetSchema()
			|| !StateTree->GetSchema()->GetClass()->IsChildOf(UStateTreeComponentSchema::StaticClass()))
		{
			OutError = TEXT("asset is not compiled/runnable with StateTreeComponentSchema");
			return false;
		}
		const UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTree->EditorData);
		if (!EditorData || EditorData->SubTrees.Num() != 1 || !EditorData->SubTrees[0])
		{
			OutError = TEXT("asset must contain one authored root state");
			return false;
		}
		const UStateTreeState* Root = EditorData->SubTrees[0];
		if (Root->Name != RootStateName
			|| Root->Children.Num() != static_cast<int32>(UE_ARRAY_COUNT(Policies))
			|| Root->Transitions.Num() != static_cast<int32>(UE_ARRAY_COUNT(Policies)))
		{
			OutError = TEXT("root, five policy children, or five policy event transitions are missing");
			return false;
		}

		TSet<FName> FoundStates;
		for (const UStateTreeState* State : Root->Children)
		{
			if (!State || State->EnterConditions.Num() != 1 || State->Tasks.Num() != 1
				|| State->Transitions.Num() != 1
				|| State->EnterConditions[0].Node.GetScriptStruct()
					!= FGuLiWingmanBehaviorPolicyCondition::StaticStruct()
				|| State->Tasks[0].Node.GetScriptStruct()
					!= FGuLiWingmanBehaviorPolicyTask::StaticStruct())
			{
				OutError = TEXT("each policy state must have exactly one policy condition, task, and recovery transition");
				return false;
			}
			FoundStates.Add(State->Name);
		}
		for (const FPolicyDescription& Policy : Policies)
		{
			if (!FoundStates.Contains(GuLiWingmanBehaviorPolicyName(Policy.Policy)))
			{
				OutError = FString::Printf(TEXT("missing policy state %s"),
					*GuLiWingmanBehaviorPolicyName(Policy.Policy).ToString());
				return false;
			}
		}
		return true;
	}

	bool WriteAuditReport(const UStateTree* StateTree)
	{
		FString ValidationError;
		const bool bValid = ValidateEditorContract(StateTree, ValidationError);
		TArray<FString> StateNames;
		int32 TaskCount = 0;
		int32 ConditionCount = 0;
		int32 EventTransitionCount = 0;
		if (const UStateTreeEditorData* EditorData =
			StateTree ? Cast<UStateTreeEditorData>(StateTree->EditorData) : nullptr)
		{
			if (EditorData->SubTrees.Num() == 1 && EditorData->SubTrees[0])
			{
				const UStateTreeState* Root = EditorData->SubTrees[0];
				EventTransitionCount = Root->Transitions.Num();
				for (const UStateTreeState* State : Root->Children)
				{
					if (!State) continue;
					StateNames.Add(State->Name.ToString());
					for (const FStateTreeEditorNode& Task : State->Tasks)
					{
						TaskCount += Task.Node.GetScriptStruct()
							== FGuLiWingmanBehaviorPolicyTask::StaticStruct() ? 1 : 0;
					}
					for (const FStateTreeEditorNode& Condition : State->EnterConditions)
					{
						ConditionCount += Condition.Node.GetScriptStruct()
							== FGuLiWingmanBehaviorPolicyCondition::StaticStruct() ? 1 : 0;
					}
				}
			}
		}
		StateNames.Sort();

		TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
		Report->SetBoolField(TEXT("success"), bValid);
		Report->SetStringField(TEXT("asset_path"), ObjectPath);
		Report->SetStringField(TEXT("asset_class"), GetNameSafe(StateTree ? StateTree->GetClass() : nullptr));
		Report->SetNumberField(TEXT("compiled_editor_data_hash"),
			StateTree ? static_cast<double>(StateTree->LastCompiledEditorDataHash) : 0.0);
		Report->SetBoolField(TEXT("ready_to_run"), StateTree && StateTree->IsReadyToRun());
		Report->SetBoolField(TEXT("package_dirty"),
			StateTree && StateTree->GetOutermost()->IsDirty());
		Report->SetNumberField(TEXT("policy_task_count"), TaskCount);
		Report->SetNumberField(TEXT("policy_condition_count"), ConditionCount);
		Report->SetNumberField(TEXT("root_event_transition_count"), EventTransitionCount);
		Report->SetStringField(TEXT("validation_error"), ValidationError);
		TArray<TSharedPtr<FJsonValue>> JsonStates;
		for (const FString& StateName : StateNames)
		{
			JsonStates.Add(MakeShared<FJsonValueString>(StateName));
		}
		Report->SetArrayField(TEXT("states"), JsonStates);
		TSharedRef<FJsonObject> Execution = MakeShared<FJsonObject>();
		Execution->SetStringField(TEXT("runner"), TEXT("UStateTreeComponent"));
		Execution->SetBoolField(TEXT("dedicated_server_runs"), false);
		Execution->SetBoolField(TEXT("remote_mirror_runs"), false);
		Execution->SetBoolField(TEXT("task_writes_transform"),
			FGuLiWingmanBehaviorPolicyTask::WritesMassTransform());
		Execution->SetBoolField(TEXT("task_writes_velocity"),
			FGuLiWingmanBehaviorPolicyTask::WritesFlightVelocity());
		Execution->SetBoolField(TEXT("task_writes_health_or_damage"),
			FGuLiWingmanBehaviorPolicyTask::WritesHealthOrDamage());
		Report->SetObjectField(TEXT("execution_contract"), Execution);

		FString Json;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
		if (!FJsonSerializer::Serialize(Report, Writer))
		{
			return false;
		}
		const FString ReportPath = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), AuditRelativePath));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(ReportPath), true);
		return FFileHelper::SaveStringToFile(Json, *ReportPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	bool AuthorEditorData(UStateTree* StateTree, FString& OutError)
	{
		if (!StateTree)
		{
			OutError = TEXT("null StateTree");
			return false;
		}
		if (UObject* OldEditorData = StateTree->EditorData)
		{
			OldEditorData->Rename(nullptr, GetTransientPackage(),
				REN_DontCreateRedirectors | REN_NonTransactional);
			OldEditorData->MarkAsGarbage();
			StateTree->EditorData = nullptr;
		}

		FStateTreeEditorModule& EditorModule = FStateTreeEditorModule::GetModule();
		const TNonNullSubclassOf<UStateTreeEditorData> EditorDataClass =
			EditorModule.GetEditorDataClass(UStateTreeComponentSchema::StaticClass());
		UStateTreeEditorData* EditorData = NewObject<UStateTreeEditorData>(
			StateTree, EditorDataClass, TEXT("WingmanBehaviorEditorData"), RF_Transactional);
		if (!EditorData)
		{
			OutError = TEXT("failed to allocate StateTree editor data");
			return false;
		}
		StateTree->EditorData = EditorData;
		EditorData->Schema = NewObject<UStateTreeComponentSchema>(
			EditorData, UStateTreeComponentSchema::StaticClass(), TEXT("WingmanComponentSchema"), RF_Transactional);
		EditorData->EditorSchema = NewObject<UStateTreeEditorSchema>(
			EditorData, UStateTreeEditorSchema::StaticClass(), TEXT("WingmanEditorSchema"), RF_Transactional);

		UStateTreeState& Root = EditorData->AddSubTree(RootStateName);
		Root.Description = TEXT("Client/listen-owner-only group policy. Tasks never integrate transforms or apply damage.");
		Root.SelectionBehavior = EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder;
		Root.bHasCustomTickRate = true;
		Root.CustomTickRate = PolicyTickSeconds;

		TMap<EGuLiWingmanBehaviorPolicy, UStateTreeState*> States;
		for (const FPolicyDescription& Policy : Policies)
		{
			UStateTreeState& State = Root.AddChildState(GuLiWingmanBehaviorPolicyName(Policy.Policy));
			State.Description = Policy.Description;
			State.SelectionBehavior = EStateTreeStateSelectionBehavior::TryEnterState;
			State.Tag = GuLiWingmanBehaviorTags::GetSignalTag(Policy.Policy);
			State.AddEnterCondition<FGuLiWingmanBehaviorPolicyCondition>(Policy.Policy);
			State.AddTask<FGuLiWingmanBehaviorPolicyTask>(Policy.Policy);
			State.AddTransition(EStateTreeTransitionTrigger::OnStateSucceeded,
				EStateTreeTransitionType::GotoState, &Root);
			States.Add(Policy.Policy, &State);
		}

		// Runner sends only on policy changes. Root transitions keep the signal
		// contract explicit while target enter conditions revalidate current Mass input.
		for (const FPolicyDescription& Policy : Policies)
		{
			Root.AddTransition(EStateTreeTransitionTrigger::OnEvent,
				GuLiWingmanBehaviorTags::GetSignalTag(Policy.Policy),
				EStateTreeTransitionType::GotoState,
				States.FindChecked(Policy.Policy));
		}

		UStateTreeEditingSubsystem::ValidateStateTree(StateTree);
		FStateTreeCompilerLog CompileLog;
		if (!UStateTreeEditingSubsystem::CompileStateTree(StateTree, CompileLog))
		{
			CompileLog.DumpToLog(LogGuLiStrike);
			OutError = TEXT("StateTree compiler rejected the authored policy graph");
			return false;
		}
		return ValidateEditorContract(StateTree, OutError);
	}

	void BuildStateTree()
	{
		UObject* ExistingObject = LoadObject<UObject>(nullptr, ObjectPath);
		UStateTree* StateTree = Cast<UStateTree>(ExistingObject);
		if ((ExistingObject && (!StateTree
				|| !ExistingObject->GetPathName().Equals(ObjectPath, ESearchCase::CaseSensitive)))
			|| (!ExistingObject && FPackageName::DoesPackageExist(PackagePath)))
		{
			UE_LOG(LogGuLiStrike, Error,
				TEXT("Wingman StateTree build refused an output path/type conflict at %s."), ObjectPath);
			return;
		}

		FString Error;
		if (StateTree && ValidateEditorContract(StateTree, Error))
		{
			if (!WriteAuditReport(StateTree))
			{
				UE_LOG(LogGuLiStrike, Error, TEXT("Wingman StateTree audit report write failed."));
				return;
			}
			UE_LOG(LogGuLiStrike, Display,
				TEXT("Wingman authored StateTree already satisfies contract: %s"), ObjectPath);
			return;
		}
		if (StateTree && StateTree->GetOutermost()->IsDirty())
		{
			UE_LOG(LogGuLiStrike, Error,
				TEXT("Wingman StateTree build refused unsaved edits at %s (%s)."), ObjectPath, *Error);
			return;
		}

		bool bNewAsset = false;
		if (!StateTree)
		{
			UPackage* Package = CreatePackage(PackagePath);
			Package->FullyLoad();
			StateTree = NewObject<UStateTree>(Package, AssetName, RF_Public | RF_Standalone);
			bNewAsset = true;
		}
		StateTree->Modify();
		if (!AuthorEditorData(StateTree, Error))
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Wingman StateTree build failed: %s"), *Error);
			return;
		}
		if (bNewAsset)
		{
			FAssetRegistryModule::AssetCreated(StateTree);
		}
		StateTree->MarkPackageDirty();
		if (!UEditorLoadingAndSavingUtils::SavePackages({StateTree->GetOutermost()}, false))
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Wingman StateTree compiled but package save failed: %s"),
				ObjectPath);
			return;
		}
		Error.Reset();
		if (!ValidateEditorContract(StateTree, Error))
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Saved Wingman StateTree contract failed: %s"), *Error);
			return;
		}
		if (!WriteAuditReport(StateTree))
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Wingman StateTree audit report write failed."));
			return;
		}
		UE_LOG(LogGuLiStrike, Display,
			TEXT("Built authored Wingman StateTree %s with five conditioned, event-driven policy states."),
			ObjectPath);
	}

	FAutoConsoleCommand BuildCommand(
		TEXT("gs.Wingman.BuildStateTree"),
		TEXT("Build/validate /Game/GuLiStrike/Wingman/ST_WingmanGroupBehavior."),
		FConsoleCommandDelegate::CreateStatic(&BuildStateTree));
}

#endif // WITH_EDITOR
