// Copyright Epic Games, Inc. All Rights Reserved.

#include "GuLiStrike.h"

#if WITH_EDITOR

#include "Gameplay/Wingman/Behavior/GuLiWingmanMemberBehavior.h"
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
	constexpr TCHAR PackagePath[] = TEXT("/Game/GuLiStrike/Wingman/ST_WingmanMemberBehavior");
	constexpr TCHAR ObjectPath[] =
		TEXT("/Game/GuLiStrike/Wingman/ST_WingmanMemberBehavior.ST_WingmanMemberBehavior");
	constexpr TCHAR AssetName[] = TEXT("ST_WingmanMemberBehavior");
	constexpr TCHAR RootStateName[] = TEXT("WingmanBehavior");
	constexpr float BehaviorTickSeconds = 0.2f;
	constexpr TCHAR AuditRelativePath[] =
		TEXT("TestResults/WingmanPlan/WingmanStateTreeAsset/state_tree_asset_audit.json");

	struct FBehaviorDescription
	{
		EGuLiWingmanMemberBehavior Behavior;
		const TCHAR* Description;
	};

	constexpr FBehaviorDescription Behaviors[] = {
		{EGuLiWingmanMemberBehavior::Dead, TEXT("This member is dead; no movement or attack request is produced.")},
		{EGuLiWingmanMemberBehavior::EmergencyAvoid, TEXT("Avoidance is active; request local recovery guidance without waiting for authority.")},
		{EGuLiWingmanMemberBehavior::GroundAttack, TEXT("Execute this member's frozen ground-attack movement request.")},
		{EGuLiWingmanMemberBehavior::AirAttack, TEXT("Execute this member's current air-combat movement request.")},
		{EGuLiWingmanMemberBehavior::Rejoin, TEXT("This member follows its Flight-level path back to formation.")},
		{EGuLiWingmanMemberBehavior::EscortOrbit, TEXT("Stable double-ring escort/orbit behavior.")}
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
			|| Root->Children.Num() != static_cast<int32>(UE_ARRAY_COUNT(Behaviors))
			|| Root->Transitions.Num() != static_cast<int32>(UE_ARRAY_COUNT(Behaviors)))
		{
			OutError = TEXT("root behavior children or event transitions do not match the live behavior contract");
			return false;
		}

		TSet<FName> FoundStates;
		for (const UStateTreeState* State : Root->Children)
		{
			if (!State || State->EnterConditions.Num() != 1 || State->Tasks.Num() != 1
				|| State->Transitions.Num() != 1
				|| State->EnterConditions[0].Node.GetScriptStruct()
					!= FGuLiWingmanMemberBehaviorCondition::StaticStruct()
				|| State->Tasks[0].Node.GetScriptStruct()
					!= FGuLiWingmanMemberBehaviorTask::StaticStruct())
			{
				OutError = TEXT("each behavior state must have exactly one behavior condition, task, and recovery transition");
				return false;
			}
			FoundStates.Add(State->Name);
		}
		for (const FBehaviorDescription& Behavior : Behaviors)
		{
			if (!FoundStates.Contains(GuLiWingmanMemberBehaviorName(Behavior.Behavior)))
			{
				OutError = FString::Printf(TEXT("missing behavior state %s"),
					*GuLiWingmanMemberBehaviorName(Behavior.Behavior).ToString());
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
							== FGuLiWingmanMemberBehaviorTask::StaticStruct() ? 1 : 0;
					}
					for (const FStateTreeEditorNode& Condition : State->EnterConditions)
					{
						ConditionCount += Condition.Node.GetScriptStruct()
							== FGuLiWingmanMemberBehaviorCondition::StaticStruct() ? 1 : 0;
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
		Report->SetNumberField(TEXT("behavior_task_count"), TaskCount);
		Report->SetNumberField(TEXT("behavior_condition_count"), ConditionCount);
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
			FGuLiWingmanMemberBehaviorTask::WritesTransform());
		Execution->SetBoolField(TEXT("task_writes_velocity"),
			FGuLiWingmanMemberBehaviorTask::WritesVelocity());
		Execution->SetBoolField(TEXT("task_writes_health_or_damage"), false);
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
		Root.Description = TEXT("Client/listen-owner-only member behavior. Tasks never integrate transforms or apply damage.");
		Root.SelectionBehavior = EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder;
		Root.bHasCustomTickRate = true;
		Root.CustomTickRate = BehaviorTickSeconds;

		TMap<EGuLiWingmanMemberBehavior, UStateTreeState*> States;
		for (const FBehaviorDescription& Behavior : Behaviors)
		{
			UStateTreeState& State = Root.AddChildState(GuLiWingmanMemberBehaviorName(Behavior.Behavior));
			State.Description = Behavior.Description;
			State.SelectionBehavior = EStateTreeStateSelectionBehavior::TryEnterState;
			State.Tag = GuLiWingmanMemberBehaviorTags::GetSignalTag(Behavior.Behavior);
			State.AddEnterCondition<FGuLiWingmanMemberBehaviorCondition>(Behavior.Behavior);
			State.AddTask<FGuLiWingmanMemberBehaviorTask>(Behavior.Behavior);
			State.AddTransition(EStateTreeTransitionTrigger::OnStateSucceeded,
				EStateTreeTransitionType::GotoState, &Root);
			States.Add(Behavior.Behavior, &State);
		}

		// Runner sends only on behavior changes. Root transitions keep the signal
		// contract explicit while target enter conditions revalidate current Pawn input.
		for (const FBehaviorDescription& Behavior : Behaviors)
		{
			Root.AddTransition(EStateTreeTransitionTrigger::OnEvent,
				GuLiWingmanMemberBehaviorTags::GetSignalTag(Behavior.Behavior),
				EStateTreeTransitionType::GotoState,
				States.FindChecked(Behavior.Behavior));
		}

		UStateTreeEditingSubsystem::ValidateStateTree(StateTree);
		FStateTreeCompilerLog CompileLog;
		if (!UStateTreeEditingSubsystem::CompileStateTree(StateTree, CompileLog))
		{
			CompileLog.DumpToLog(LogGuLiStrike);
			OutError = TEXT("StateTree compiler rejected the authored behavior graph");
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
			TEXT("Built authored Wingman StateTree %s with %d conditioned, event-driven behavior states."),
			ObjectPath, static_cast<int32>(UE_ARRAY_COUNT(Behaviors)));
	}

	FAutoConsoleCommand BuildCommand(
		TEXT("gs.Wingman.BuildStateTree"),
		TEXT("Build/validate /Game/GuLiStrike/Wingman/ST_WingmanMemberBehavior."),
		FConsoleCommandDelegate::CreateStatic(&BuildStateTree));
}

#endif // WITH_EDITOR
