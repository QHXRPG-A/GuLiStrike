#include "GuLiStrike.h"

#if WITH_EDITOR
#include "Commander/Behavior/GuLiCommanderBehaviorSchema.h"
#include "Commander/Behavior/GuLiCommanderStateTreeNodes.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "StateTree.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorModule.h"
#include "StateTreeEditorSchema.h"
#include "StateTreeState.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace GuLiCommanderStateTreeHierarchy
{
	using namespace GuLiCommanderBehaviorFacts;
	using Step = EGuLiCommanderBehaviorStep;
	using Phase = EGuLiCommanderWorkPhase;
	using Result = EGuLiCommanderWorkResult;
	using Trigger = EStateTreeTransitionTrigger;
	using Priority = EStateTreeTransitionPriority;
	constexpr uint32 Blocked = CancelPending | Stopped | PendingMove;
	const TCHAR* VersionKey = TEXT("GuLiCommanderHierarchyVersion");
	struct FSpec { const TCHAR* Name; bool bMass; EGuLiCommanderBehavior Behavior; };
	// Migrate engineering behavior first, and preserve all existing asset paths.
	const FSpec Specs[] = {
		{TEXT("ST_CommanderMiner"), false, EGuLiCommanderBehavior::Mining},
		{TEXT("ST_CommanderBuilder"), false, EGuLiCommanderBehavior::Construction},
		{TEXT("ST_CommanderMass"), true, EGuLiCommanderBehavior::StrongholdAdvance}
	};
	struct FGuard
	{
		uint32 Required = 0, Forbidden = 0;
		Phase WorkPhase = Phase::Any;
		Result WorkResult = Result::Any;
	};

	struct FGraph
	{
		bool bMass;
		void Enter(UStateTreeState& State, std::initializer_list<FGuard> Alternatives)
		{
			for (const auto& G : Alternatives)
			{
				FStateTreeEditorNode& Node = bMass
					? static_cast<FStateTreeEditorNode&>(State.AddEnterCondition<FGuLiCommanderMassBehaviorCondition>(G.Required, G.Forbidden, G.WorkPhase, G.WorkResult))
					: static_cast<FStateTreeEditorNode&>(State.AddEnterCondition<FGuLiCommanderActorBehaviorCondition>(G.Required, G.Forbidden, G.WorkPhase, G.WorkResult));
				Node.ExpressionOperand = State.EnterConditions.Num() == 1 ? EStateTreeExpressionOperand::And : EStateTreeExpressionOperand::Or;
			}
		}
		UStateTreeState& Group(UStateTreeState& Parent, const TCHAR* Name, std::initializer_list<FGuard> Guards = {})
		{
			auto& State = Parent.AddChildState(FName(Name));
			State.SelectionBehavior = EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder;
			Enter(State, Guards); return State;
		}
		UStateTreeState& Leaf(UStateTreeState& Parent, const TCHAR* Name, Step Entry, bool bOneShot,
			std::initializer_list<FGuard> Guards = {}, std::initializer_list<Phase> Resume = {},
			EGuLiCommanderControlState Control = EGuLiCommanderControlState::Normal, Step Poll = Step::ObserveOrder)
		{
			auto& State = Group(Parent, Name, Guards);
			State.SelectionBehavior = EStateTreeStateSelectionBehavior::TryEnterState;
			FGuLiCommanderPersistentOperation Op;
			Op.Entry = Entry; Op.WhileRunning = Poll; Op.bCompleteOnReceipt = bOneShot; Op.Control = Control;
			for (auto P : Resume) Op.ResumePhases.Add(P);
			if (bMass) State.AddTask<FGuLiCommanderMassPersistentTask>(Op);
			else State.AddTask<FGuLiCommanderActorPersistentTask>(Op);
			return State;
		}
		void Go(UStateTreeState& From, UStateTreeState& To, Trigger Event = Trigger::OnStateCompleted,
			std::initializer_list<FGuard> Guards = {}, Priority Rank = Priority::Normal)
		{
			auto& Transition = From.AddTransition(Event, EStateTreeTransitionType::GotoState, &To);
			Transition.Priority = Rank;
			for (const auto& G : Guards)
			{
				FStateTreeEditorNode& Node = bMass
					? static_cast<FStateTreeEditorNode&>(Transition.AddCondition<FGuLiCommanderMassBehaviorCondition>(G.Required, G.Forbidden, G.WorkPhase, G.WorkResult))
					: static_cast<FStateTreeEditorNode&>(Transition.AddCondition<FGuLiCommanderActorBehaviorCondition>(G.Required, G.Forbidden, G.WorkPhase, G.WorkResult));
				Node.ExpressionOperand = Transition.Conditions.Num() == 1 ? EStateTreeExpressionOperand::And : EStateTreeExpressionOperand::Or;
			}
		}
	};

	void MiningGraph(FGraph& G, UStateTreeState& Executing, UStateTreeState& Finish)
	{
		auto& Job = G.Group(Executing, TEXT("ResourceJob"), {{Mining}, {Return}});
		auto& Mine = G.Group(Job, TEXT("AcquireResource"), {{Mining}});
		auto& Reposition = G.Leaf(Mine, TEXT("RecoverMiningPosition"), Step::MiningReposition, true,
			{{0, 0, Phase::Any, Result::OutOfRange}, {0, 0, Phase::MiningMoving, Result::Failed}});
		auto& Select = G.Leaf(Mine, TEXT("SelectMiningPosition"), Step::MiningSelect, true,
			{{RetryReady, ReturnFirst, Phase::MiningIdle, Result::None}, {0, 0, Phase::MiningWaitingPosition, Result::WaitingPosition}});
		auto& Travel = G.Leaf(Mine, TEXT("TravelToMine"), Step::MiningMove, false,
			{{0, 0, Phase::MiningReserved, Result::TargetReady}, {0, 0, Phase::MiningWaitingPath}, {0, 0, Phase::MiningMoving}},
			{Phase::MiningMoving, Phase::MiningWaitingPath});
		auto& Extract = G.Leaf(Mine, TEXT("ExtractResource"), Step::MiningExtract, false,
			{{0, 0, Phase::MiningMoving, Result::CanMine}, {0, 0, Phase::MiningExtracting}}, {Phase::MiningExtracting});

		auto& CargoFlow = G.Group(Job, TEXT("ReturnCargo"));
		auto& Factory = G.Leaf(CargoFlow, TEXT("SelectFactory"), Step::MiningFactory, true,
			{{ReturnFirst | RetryReady, 0, Phase::MiningIdle, Result::None}, {Return | RetryReady, 0, Phase::MiningIdle, Result::None},
			 {0, 0, Phase::MiningExtracting, Result::Full}, {0, 0, Phase::MiningExtracting, Result::Depleted},
			 {Cargo, 0, Phase::Any, Result::TargetLost}, {0, 0, Phase::Any, Result::FactoryLost}, {0, 0, Phase::Any, Result::FactoryUnreachable}});
		auto& ReturnMove = G.Leaf(CargoFlow, TEXT("TravelToFactory"), Step::MiningReturn, false,
			{{0, 0, Phase::MiningIdle, Result::FactoryReady}, {0, 0, Phase::MiningReturning}}, {Phase::MiningReturning});
		auto& Unload = G.Leaf(CargoFlow, TEXT("UnloadCargo"), Step::MiningUnload, false,
			{{0, 0, Phase::MiningReturning, Result::AtFactory}, {0, 0, Phase::MiningUnloading}}, {Phase::MiningUnloading});
		auto& NextPoint = G.Leaf(CargoFlow, TEXT("RecoverUnloadPoint"), Step::Wait, true,
			{{0, 0, Phase::MiningReturning, Result::Failed}});
		auto& Cycle = G.Leaf(Job, TEXT("CycleHandoff"), Step::MiningFinish, true,
			{{0, 0, Phase::MiningUnloading, Result::Unloaded}});
		auto& Retry = G.Leaf(Job, TEXT("PrepareAutomaticRetry"), Step::MiningRetry, true,
			{{Automatic, 0, Phase::Any, Result::NoTarget}, {Automatic, 0, Phase::Any, Result::Failed}, {Automatic, Cargo, Phase::Any, Result::TargetLost}});
		auto& Failed = G.Leaf(Job, TEXT("FailManualResourceOrder"), Step::MiningFail, true,
			{{0, Automatic, Phase::Any, Result::NoTarget}, {0, Automatic, Phase::Any, Result::Failed}, {0, Cargo | Automatic, Phase::Any, Result::TargetLost}});
		auto& RetryWait = G.Leaf(Job, TEXT("WaitForResourceRetry"), Step::Wait, false,
			{{0, RetryReady, Phase::MiningIdle, Result::None}});

		G.Go(Reposition, Job); G.Go(Select, Extract, Trigger::OnStateCompleted, {{0, 0, Phase::Any, Result::CanMine}});
		G.Go(Select, Travel, Trigger::OnStateCompleted, {{0, 0, Phase::Any, Result::TargetReady}});
		G.Go(Travel, Extract, Trigger::OnTick, {{0, 0, Phase::Any, Result::CanMine}});
		for (auto* State : {&Travel, &Extract})
		{
			G.Go(*State, Reposition, Trigger::OnTick, {{0, 0, Phase::Any, Result::OutOfRange}, {0, 0, Phase::MiningMoving, Result::Failed}});
			G.Go(*State, Reposition, Trigger::OnStateFailed, {{0, 0, Phase::Any, Result::OutOfRange}, {0, 0, Phase::MiningMoving, Result::Failed}});
		}
		G.Go(Extract, Factory, Trigger::OnTick, {{0, 0, Phase::Any, Result::Full}, {0, 0, Phase::Any, Result::Depleted}});
		G.Go(Factory, ReturnMove, Trigger::OnStateCompleted, {{0, 0, Phase::Any, Result::FactoryReady}});
		G.Go(ReturnMove, Unload, Trigger::OnTick, {{0, 0, Phase::Any, Result::AtFactory}});
		G.Go(ReturnMove, NextPoint, Trigger::OnTick, {{0, 0, Phase::Any, Result::Failed}});
		G.Go(ReturnMove, NextPoint, Trigger::OnStateFailed, {{0, 0, Phase::Any, Result::Failed}});
		G.Go(NextPoint, ReturnMove);
		G.Go(Unload, Cycle, Trigger::OnTick, {{0, 0, Phase::Any, Result::Unloaded}});
		G.Go(Cycle, Finish); G.Go(Failed, Finish); G.Go(Retry, RetryWait);
		G.Go(RetryWait, Job, Trigger::OnTick, {{RetryReady}});
		for (auto* State : {&Select, &Travel, &Extract, &Factory, &ReturnMove, &Unload})
		{
			for (const auto Event : {Trigger::OnTick, Trigger::OnStateFailed})
			{
				// Specific path/slot recovery was inserted first, ahead of generic action failure.
				G.Go(*State, Factory, Event, {{Cargo, 0, Phase::Any, Result::TargetLost}, {0, 0, Phase::Any, Result::FactoryLost}, {0, 0, Phase::Any, Result::FactoryUnreachable}});
				G.Go(*State, Retry, Event, {{Automatic, Cargo, Phase::Any, Result::TargetLost}, {Automatic, 0, Phase::Any, Result::NoTarget}, {Automatic, 0, Phase::Any, Result::Failed}});
				G.Go(*State, Failed, Event, {{0, Automatic | Cargo, Phase::Any, Result::TargetLost}, {0, Automatic, Phase::Any, Result::NoTarget}, {0, Automatic, Phase::Any, Result::Failed}});
			}
		}
		Job.Description = TEXT("Mining and standalone return share ReturnCargo. Local slot, factory and point recovery preserve cargo; only order completion returns to order selection.");
	}

	UStateTreeState& ConstructionGraph(FGraph& G, UStateTreeState& Executing, UStateTreeState& Finish)
	{
		auto& Job = G.Group(Executing, TEXT("ConstructionJob"), {{Construction}});
		auto& ReturnOrder = G.Leaf(Job, TEXT("ReturnConstructionOrder"), Step::ObserveOrder, false,
			{{0, 0, Phase::ConstructionCancelled}, {0, 0, Phase::Any, Result::Failed}});
		auto& Reject = G.Leaf(Job, TEXT("RejectConstructionPosition"), Step::ConstructionRetry, true, {{0, 0, Phase::Any, Result::OutOfRange}});
		auto& Approach = G.Group(Job, TEXT("ApproachConstructionSite"));
		auto& Select = G.Leaf(Approach, TEXT("SelectConstructionDestination"), Step::ConstructionReserve, true, {{0, 0, Phase::ConstructionPrepared, Result::None}});
		auto& Travel = G.Leaf(Approach, TEXT("TravelToConstructionSite"), Step::ConstructionMove, false,
			{{0, 0, Phase::ConstructionIntended, Result::TargetReady}, {0, 0, Phase::ConstructionWaitingPath}, {0, 0, Phase::ConstructionMoving}},
			{Phase::ConstructionWaitingPath, Phase::ConstructionMoving});
		auto& Claim = G.Leaf(Approach, TEXT("ClaimPositionOnArrival"), Step::ConstructionWork, true, {{0, 0, Phase::ConstructionMoving, Result::Arrived}});
		auto& Work = G.Leaf(Job, TEXT("ConstructBuilding"), Step::ObserveOrder, false, {{0, 0, Phase::ConstructionWorking}});
		G.Go(Select, Travel, Trigger::OnStateCompleted, {{0, 0, Phase::ConstructionIntended}});
		G.Go(Travel, Claim, Trigger::OnTick, {{0, 0, Phase::Any, Result::Arrived}});
		G.Go(Claim, Work, Trigger::OnStateCompleted, {{0, 0, Phase::ConstructionWorking}});
		G.Go(Reject, ReturnOrder); G.Go(ReturnOrder, Finish, Trigger::OnTick, {{OrderTerminal}});
		G.Go(Reject, ReturnOrder, Trigger::OnTick, {{OrderTerminal}}, Priority::Medium);
		for (auto* State : {&Select, &Travel, &Claim, &Work})
		{
			for (const auto Event : {Trigger::OnTick, Trigger::OnStateFailed})
			{
				G.Go(*State, ReturnOrder, Event, {{0, 0, Phase::ConstructionCancelled}, {0, 0, Phase::Any, Result::Failed}}, Priority::Medium);
				G.Go(*State, Reject, Event, {{0, 0, Phase::Any, Result::OutOfRange}});
			}
		}
		Job.Description = TEXT("Intent during travel; atomic occupancy only on arrival. Rejected positions keep existing expiry. Automatic local replacement remains disabled once construction begins.");
		return ReturnOrder;
	}

	void AdvanceGraph(FGraph& G, UStateTreeState& Executing)
	{
		auto& Job = G.Group(Executing, TEXT("StrongholdAdvance"), {{Advance}});
		auto& Complete = G.Leaf(Job, TEXT("StrongholdStageHandoff"), Step::AdvanceComplete, true, {{0, 0, Phase::Any, Result::Complete}});
		auto& Reject = G.Leaf(Job, TEXT("RejectUnreachableStronghold"), Step::AdvanceReject, true, {{0, 0, Phase::Any, Result::Failed}});
		auto& Select = G.Leaf(Job, TEXT("SelectStronghold"), Step::AdvanceSelect, true,
			{{0, 0, Phase::AdvanceSelecting, Result::None}, {0, 0, Phase::AdvanceWaiting, Result::NoTarget}});
		auto& Move = G.Leaf(Job, TEXT("AdvanceToStronghold"), Step::AdvanceMove, false,
			{{0, 0, Phase::AdvanceSelecting, Result::TargetReady}, {0, 0, Phase::AdvanceMoving}}, {Phase::AdvanceMoving});
		auto& Capture = G.Leaf(Job, TEXT("CaptureStronghold"), Step::AdvanceCapture, false,
			{{0, 0, Phase::AdvanceMoving, Result::Arrived}, {0, 0, Phase::AdvanceCapturing}}, {Phase::AdvanceCapturing});
		auto& Wait = G.Leaf(Job, TEXT("WaitForStronghold"), Step::AdvanceWait, false,
			{{0, 0, Phase::AdvanceSelecting, Result::NoTarget}, {0, 0, Phase::AdvanceWaiting}}, {Phase::AdvanceWaiting});
		G.Go(Select, Move, Trigger::OnStateCompleted, {{0, 0, Phase::Any, Result::TargetReady}});
		G.Go(Select, Wait, Trigger::OnStateCompleted, {{0, 0, Phase::Any, Result::NoTarget}});
		G.Go(Select, Job, Trigger::OnTick, {{0, 0, Phase::AdvanceMoving}, {0, 0, Phase::AdvanceCapturing},
			{0, 0, Phase::AdvanceWaiting, Result::Running}});
		G.Go(Move, Capture, Trigger::OnTick, {{0, 0, Phase::Any, Result::Arrived}});
		G.Go(Wait, Select, Trigger::OnTick, {{0, 0, Phase::AdvanceWaiting, Result::NoTarget}, {0, 0, Phase::AdvanceSelecting, Result::None}});
		G.Go(Wait, Job, Trigger::OnTick, {{0, 0, Phase::AdvanceSelecting, Result::TargetReady},
			{0, 0, Phase::AdvanceMoving}, {0, 0, Phase::AdvanceCapturing}});
		G.Go(Complete, Job); G.Go(Reject, Job);
		for (auto* State : {&Move, &Capture})
		{
			G.Go(*State, Complete, Trigger::OnTick, {{0, 0, Phase::Any, Result::Complete}});
			G.Go(*State, Reject, Trigger::OnTick, {{0, 0, Phase::Any, Result::Failed}});
			G.Go(*State, Reject, Trigger::OnStateFailed, {{0, 0, Phase::Any, Result::Failed}});
			// Other group members can commit the shared target's completion/rejection first.
			G.Go(*State, Job, Trigger::OnTick, {{0, 0, Phase::AdvanceSelecting, Result::None}, {0, 0, Phase::AdvanceSelecting, Result::TargetReady},
				{0, 0, Phase::AdvanceWaiting}});
		}
		G.Go(Move, Capture, Trigger::OnTick, {{0, 0, Phase::AdvanceCapturing}});
	}

	UStateTreeEditorData* CreateGraph(UStateTree& Tree, const FSpec& Spec, const FGuLiCommanderBehaviorPolicy& Policy)
	{
		const TSubclassOf<UStateTreeSchema> SchemaClass = Spec.bMass ? UGuLiCommanderMassStateTreeSchema::StaticClass() : UGuLiCommanderActorStateTreeSchema::StaticClass();
		auto& Module = FStateTreeEditorModule::GetModule();
		auto* Data = NewObject<UStateTreeEditorData>(&Tree, Module.GetEditorDataClass(SchemaClass.Get()),
			MakeUniqueObjectName(&Tree, Module.GetEditorDataClass(SchemaClass.Get()), TEXT("CommanderHierarchyEditorData")), RF_Transactional);
		const auto* Previous = CastChecked<UStateTreeEditorData>(Tree.EditorData);
		Data->Schema = DuplicateObject<UStateTreeSchema>(Previous->Schema, Data);
		Data->EditorSchema = Previous->EditorSchema ? DuplicateObject<UStateTreeEditorSchema>(Previous->EditorSchema, Data)
			: NewObject<UStateTreeEditorSchema>(Data, TEXT("CommanderEditorSchema"), RF_Transactional);
		if (Spec.bMass) CastChecked<UGuLiCommanderMassStateTreeSchema>(Data->Schema)->Policy = Policy;
		else CastChecked<UGuLiCommanderActorStateTreeSchema>(Data->Schema)->Policy = Policy;
		FGraph G{Spec.bMass};
		auto& Root = Data->AddSubTree(TEXT("CommanderOrders"));
		Root.Description = TEXT("Asset-authored hierarchy v2. Persistent phases; local recovery. Root reselection is reserved for control or order identity changes.");
		auto& Control = G.Group(Root, TEXT("Control"));
		auto& Safe = G.Leaf(Control, TEXT("WaitForSafeExit"), Step::CancelPending, true, {{CancelPending}}, {}, EGuLiCommanderControlState::SafeExit);
		G.Leaf(Control, TEXT("Stopped"), Step::Wait, false, {{Stopped, CancelPending}}, {}, EGuLiCommanderControlState::Stopped, Step::Wait);
		auto& Replace = G.Leaf(Control, TEXT("PrepareReplacementMove"), Step::ReplaceMove, true, {{PendingMove, CancelPending | Stopped}}, {}, EGuLiCommanderControlState::ReplacementMove);
		auto& Choose = G.Group(Root, TEXT("SelectOrder"), {{0, Active | Blocked}});
		auto& Manual = G.Leaf(Choose, TEXT("TakeManualOrder"), Step::TakeManual, true, {{Queued}});
		auto& AutomaticOrder = G.Leaf(Choose, TEXT("TakeAutomaticOrder"), Step::TakeAutomatic, true, {{AutomaticReady, Queued}});
		auto& Idle = G.Leaf(Choose, TEXT("Idle"), Step::Wait, false, {}, {}, EGuLiCommanderControlState::Normal, Step::Wait);
		auto& Execute = G.Group(Root, TEXT("ExecuteOrder"), {{Active, Blocked}});
		auto& Finish = G.Leaf(Execute, TEXT("FinishOrder"), Step::FinishOrder, true, {{OrderTerminal}});
		auto& Pause = G.Leaf(Execute, TEXT("Suspended"), Step::ObserveOrder, false, {{Suspended, OrderTerminal}});
		auto& Start = G.Leaf(Execute, TEXT("StartOrder"), Step::StartOrder, true, {{0, Started | OrderTerminal}});
		G.Leaf(Execute, TEXT("ManualMove"), Step::ObserveOrder, false, {{GuLiCommanderBehaviorFacts::Move, Automatic}});
		if (!Spec.bMass) G.Leaf(Execute, TEXT("ManualTransit"), Step::ObserveOrder, false, {{Transit, Automatic}});
		if (Spec.Behavior == EGuLiCommanderBehavior::Mining) MiningGraph(G, Execute, Finish);
		else if (Spec.Behavior == EGuLiCommanderBehavior::Construction)
		{
			auto& ReturnOrder = ConstructionGraph(G, Execute, Finish);
			G.Go(Start, ReturnOrder, Trigger::OnTick, {{OrderTerminal, 0, Phase::Any, Result::Failed}}, Priority::Medium);
		}
		else AdvanceGraph(G, Execute);
		G.Go(Root, Root, Trigger::OnTick, {{ControlChanged}}, Priority::Critical);
		G.Go(Root, Root, Trigger::OnTick, {{OperationStale}}, Priority::High);
		G.Go(Execute, Finish, Trigger::OnTick, {{OrderTerminal, Blocked}});
		// Do not reselect the already suspended leaf on every tick.
		for (const auto& Child : Execute.Children)
			if (Child != &Pause && Child != &Finish)
				G.Go(*Child, Pause, Trigger::OnTick, {{Suspended, OrderTerminal | Blocked}}, Priority::Medium);
		G.Go(Pause, Execute, Trigger::OnTick, {{0, Suspended | Blocked}});
		for (auto* State : {&Safe, &Replace, &Manual, &AutomaticOrder, &Finish}) G.Go(*State, Root);
		G.Go(Start, Execute); G.Go(Idle, Choose, Trigger::OnTick, {{Queued}, {AutomaticReady}});
		Root.AddTransition(Trigger::OnStateFailed, EStateTreeTransitionType::Failed);
		return Data;
	}

	FString ObjectPath(const FSpec& Spec)
	{ return FString(TEXT("/Game/GuLiStrike/Commander/Behavior/")) + Spec.Name + TEXT(".") + Spec.Name; }

	bool WriteReport(const FString& RelativePath, const TSharedRef<FJsonObject>& Report)
	{
		const FString Filename = FPaths::ProjectDir() / TEXT("Artifacts/CommanderStateTree") / RelativePath;
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
		FString Json;
		return FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json))
			&& FFileHelper::SaveStringToFile(Json, *Filename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	// Export properties from the actual node templates, including unknown future task/condition types.
	TSharedRef<FJsonObject> Properties(const UStruct* Type, const void* Memory)
	{
		const auto Out = MakeShared<FJsonObject>();
		if (!Type || !Memory) return Out;
		for (TFieldIterator<FProperty> It(Type); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property->HasAnyPropertyFlags(CPF_Edit) || Property->HasAnyPropertyFlags(CPF_Transient)) continue;
			FString Text;
			// Equal data/default pointers force full export, including zero/default fields in nested structs.
			Property->ExportText_InContainer(0, Text, Memory, Memory, nullptr, PPF_None);
			Out->SetStringField(Property->GetName(), Text);
		}
		return Out;
	}

	TArray<TSharedPtr<FJsonValue>> Nodes(const TArray<FStateTreeEditorNode>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const auto& Value : Values)
		{
			const auto Node = MakeShared<FJsonObject>();
			Node->SetStringField(TEXT("id"), Value.ID.ToString());
			Node->SetStringField(TEXT("name"), Value.GetName().ToString());
			Node->SetStringField(TEXT("type"), GetPathNameSafe(Value.Node.GetScriptStruct()));
			Node->SetStringField(TEXT("operand"), StaticEnum<EStateTreeExpressionOperand>()->GetNameStringByValue(int64(Value.ExpressionOperand)));
			Node->SetNumberField(TEXT("indent"), Value.ExpressionIndent);
			Node->SetObjectField(TEXT("properties"), Properties(Value.Node.GetScriptStruct(), Value.Node.GetMemory()));
			Node->SetObjectField(TEXT("instance"), Value.InstanceObject
				? Properties(Value.InstanceObject->GetClass(), Value.InstanceObject.Get())
				: Properties(Value.Instance.GetScriptStruct(), Value.Instance.GetMemory()));
			Out.Add(MakeShared<FJsonValueObject>(Node));
		}
		return Out;
	}

	void Index(const UStateTreeState& State, const FString& Parent, TMap<FGuid, FString>& Paths, TArray<TSharedPtr<FJsonValue>>& Errors)
	{
		const FString Path = Parent.IsEmpty() ? State.Name.ToString() : Parent / State.Name.ToString();
		if (Paths.Contains(State.ID)) Errors.Add(MakeShared<FJsonValueString>(TEXT("Duplicate state ID: ") + Path));
		Paths.Add(State.ID, Path);
		for (const auto& Child : State.Children) if (Child) Index(*Child, Path, Paths, Errors);
	}

	TSharedRef<FJsonObject> ExportState(const UStateTreeState& State, const TMap<FGuid, FString>& Paths,
		TArray<TSharedPtr<FJsonValue>>& Names, TArray<TSharedPtr<FJsonValue>>& Errors, int32 Depth, int32& MaxDepth, int32& PersistentCount)
	{
		const auto Out = MakeShared<FJsonObject>();
		Names.Add(MakeShared<FJsonValueString>(State.Name.ToString()));
		MaxDepth = FMath::Max(MaxDepth, Depth);
		Out->SetStringField(TEXT("id"), State.ID.ToString());
		Out->SetStringField(TEXT("name"), State.Name.ToString());
		Out->SetStringField(TEXT("path"), Paths.FindRef(State.ID));
		Out->SetStringField(TEXT("description"), State.Description);
		Out->SetStringField(TEXT("type"), StaticEnum<EStateTreeStateType>()->GetNameStringByValue(int64(State.Type)));
		Out->SetStringField(TEXT("selection"), StaticEnum<EStateTreeStateSelectionBehavior>()->GetNameStringByValue(int64(State.SelectionBehavior)));
		Out->SetBoolField(TEXT("enabled"), State.bEnabled);
		Out->SetArrayField(TEXT("enter_conditions"), Nodes(State.EnterConditions));
		Out->SetArrayField(TEXT("tasks"), Nodes(State.Tasks));
		for (const auto& Task : State.Tasks)
		{
			const auto* Type = Task.Node.GetScriptStruct();
			if (Type == FGuLiCommanderActorPersistentTask::StaticStruct() || Type == FGuLiCommanderMassPersistentTask::StaticStruct()) ++PersistentCount;
		}
		TArray<TSharedPtr<FJsonValue>> Transitions;
		for (const auto& Transition : State.Transitions)
		{
			const auto Edge = MakeShared<FJsonObject>();
			Edge->SetStringField(TEXT("id"), Transition.ID.ToString());
			Edge->SetStringField(TEXT("trigger"), StaticEnum<Trigger>()->GetNameStringByValue(int64(Transition.Trigger)));
			Edge->SetStringField(TEXT("priority"), StaticEnum<Priority>()->GetNameStringByValue(int64(Transition.Priority)));
			Edge->SetNumberField(TEXT("priority_value"), int64(Transition.Priority));
			Edge->SetStringField(TEXT("target_type"), StaticEnum<EStateTreeTransitionType>()->GetNameStringByValue(int64(Transition.State.LinkType)));
			Edge->SetStringField(TEXT("target_id"), Transition.State.ID.ToString());
			Edge->SetStringField(TEXT("target_name"), Transition.State.Name.ToString());
			Edge->SetStringField(TEXT("target_path"), Paths.FindRef(Transition.State.ID));
			Edge->SetBoolField(TEXT("enabled"), Transition.bTransitionEnabled);
			Edge->SetBoolField(TEXT("delayed"), Transition.bDelayTransition);
			Edge->SetNumberField(TEXT("delay_seconds"), Transition.DelayDuration);
			Edge->SetNumberField(TEXT("delay_variance"), Transition.DelayRandomVariance);
			Edge->SetStringField(TEXT("event_tag"), Transition.RequiredEvent.Tag.ToString());
			Edge->SetStringField(TEXT("event_payload_type"), GetPathNameSafe(Transition.RequiredEvent.PayloadStruct));
			Edge->SetArrayField(TEXT("conditions"), Nodes(Transition.Conditions));
			if (Transition.bTransitionEnabled && Transition.State.LinkType == EStateTreeTransitionType::GotoState && !Paths.Contains(Transition.State.ID))
				Errors.Add(MakeShared<FJsonValueString>(TEXT("Unresolved transition: ") + Paths.FindRef(State.ID) + TEXT(" -> ") + Transition.State.Name.ToString()));
			Transitions.Add(MakeShared<FJsonValueObject>(Edge));
		}
		Out->SetArrayField(TEXT("transitions"), Transitions);
		TArray<TSharedPtr<FJsonValue>> Children;
		for (const auto& Child : State.Children) if (Child)
			Children.Add(MakeShared<FJsonValueObject>(ExportState(*Child, Paths, Names, Errors, Depth + 1, MaxDepth, PersistentCount)));
		Out->SetArrayField(TEXT("children"), Children);
		return Out;
	}

	TSharedRef<FJsonObject> InspectAsset(const FSpec& Spec)
	{
		const auto Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("asset"), ObjectPath(Spec));
		TArray<TSharedPtr<FJsonValue>> Errors;
		auto* Tree = LoadObject<UStateTree>(nullptr, *ObjectPath(Spec));
		const auto* Data = Tree ? Cast<UStateTreeEditorData>(Tree->EditorData) : nullptr;
		if (!Data || !Data->Schema || Data->SubTrees.IsEmpty())
		{
			Out->SetBoolField(TEXT("valid"), false);
			Out->SetStringField(TEXT("error"), TEXT("Existing StateTree or authoring data is missing.")); return Out;
		}
		const uint32 Hash = UStateTreeEditingSubsystem::CalculateStateTreeHash(Tree);
		Out->SetStringField(TEXT("schema"), Data->Schema->GetClass()->GetPathName());
		Out->SetObjectField(TEXT("schema_properties"), Properties(Data->Schema->GetClass(), Data->Schema.Get()));
		Out->SetStringField(TEXT("hierarchy_version"), Tree->GetOutermost()->GetMetaData().GetValue(Tree, VersionKey));
		Out->SetBoolField(TEXT("ready"), Tree->IsReadyToRun());
		Out->SetBoolField(TEXT("dirty"), Tree->GetOutermost()->IsDirty());
		Out->SetBoolField(TEXT("compiled_matches_editor"), Hash == Tree->LastCompiledEditorDataHash);
		Out->SetStringField(TEXT("editor_hash"), FString::Printf(TEXT("%08x"), Hash));
		Out->SetBoolField(TEXT("mutated"), false);
		const UClass* ExpectedSchema = Spec.bMass ? UGuLiCommanderMassStateTreeSchema::StaticClass() : UGuLiCommanderActorStateTreeSchema::StaticClass();
		if (Data->Schema->GetClass() != ExpectedSchema) Errors.Add(MakeShared<FJsonValueString>(TEXT("Actor/Mass schema does not match the unit binding.")));
		if (!Tree->IsReadyToRun() || Hash != Tree->LastCompiledEditorDataHash) Errors.Add(MakeShared<FJsonValueString>(TEXT("Compiled data is missing or out of date; compile the edited asset in UE.")));
		TMap<FGuid, FString> Paths;
		for (const auto& Root : Data->SubTrees) if (Root) Index(*Root, FString(), Paths, Errors);
		TArray<TSharedPtr<FJsonValue>> Roots, Names;
		int32 MaxDepth = 0, PersistentCount = 0;
		for (const auto& Root : Data->SubTrees) if (Root)
			Roots.Add(MakeShared<FJsonValueObject>(ExportState(*Root, Paths, Names, Errors, 0, MaxDepth, PersistentCount)));
		Out->SetArrayField(TEXT("roots"), Roots);
		Out->SetArrayField(TEXT("states"), Names);
		Out->SetArrayField(TEXT("evaluators"), Nodes(Data->Evaluators));
		Out->SetArrayField(TEXT("global_tasks"), Nodes(Data->GlobalTasks));
		Out->SetNumberField(TEXT("state_count"), Names.Num());
		Out->SetNumberField(TEXT("max_depth"), MaxDepth);
		Out->SetNumberField(TEXT("persistent_task_count"), PersistentCount);
		Out->SetArrayField(TEXT("errors"), Errors);
		Out->SetBoolField(TEXT("valid"), Errors.IsEmpty());
		return Out;
	}

	TSharedRef<FJsonObject> InspectReport()
	{
		const auto Report = MakeShared<FJsonObject>();
		Report->SetStringField(TEXT("source"), TEXT("UE UStateTreeEditorData recursive readback"));
		Report->SetStringField(TEXT("exported_utc"), FDateTime::UtcNow().ToIso8601());
		Report->SetBoolField(TEXT("read_only"), true);
		bool bSuccess = true;
		TArray<TSharedPtr<FJsonValue>> Assets;
		for (const auto& Spec : Specs)
		{
			const auto Asset = InspectAsset(Spec);
			bSuccess &= Asset->GetBoolField(TEXT("valid"));
			Assets.Add(MakeShared<FJsonValueObject>(Asset));
		}
		Report->SetBoolField(TEXT("success"), bSuccess);
		Report->SetArrayField(TEXT("assets"), Assets);
		return Report;
	}

	// Daily commands must never compile, save, or regenerate a designer-authored graph.
	void InspectAll()
	{
		const auto Report = InspectReport();
		const bool bWritten = WriteReport(TEXT("hierarchy-readback.json"), Report) && WriteReport(TEXT("tree-assets.json"), Report);
		const auto Builder = InspectAsset(Specs[1]);
		Builder->SetBoolField(TEXT("success"), Builder->GetBoolField(TEXT("valid")));
		WriteReport(TEXT("builder-tree.json"), Builder);
		UE_LOG(LogGuLiStrike, Display, TEXT("Read-only Commander StateTree inspection: valid=%d report=%d"), Report->GetBoolField(TEXT("success")), bWritten);
	}

	bool MigrateAsset(const FSpec& Spec, const TSharedRef<FJsonObject>& Entry)
	{
		Entry->SetStringField(TEXT("asset"), ObjectPath(Spec));
		auto Fail = [&](const TCHAR* Message) { Entry->SetStringField(TEXT("error"), Message); return false; };
		auto* Tree = LoadObject<UStateTree>(nullptr, *ObjectPath(Spec));
		auto* Previous = Tree ? Cast<UStateTreeEditorData>(Tree->EditorData) : nullptr;
		if (!Previous || !Previous->Schema) return Fail(TEXT("Existing asset/schema is missing; migration does not create replacement paths."));
		auto* Package = Tree->GetOutermost();
		const FString Version = Package->GetMetaData().GetValue(Tree, VersionKey);
		if (Version == TEXT("2"))
		{
			Entry->SetBoolField(TEXT("already_migrated"), true);
			return InspectAsset(Spec)->GetBoolField(TEXT("valid"));
		}
		if (!Version.IsEmpty()) return Fail(TEXT("Unknown migration version; existing graph was preserved."));
		if (Package->IsDirty()) return Fail(TEXT("Asset has unsaved edits. Save/review it in UE before the one-time migration."));
		const auto* Policy = GuLiCommanderBehavior::GetPolicy(Tree);
		const UClass* ExpectedSchema = Spec.bMass ? UGuLiCommanderMassStateTreeSchema::StaticClass() : UGuLiCommanderActorStateTreeSchema::StaticClass();
		if (Previous->Schema->GetClass() != ExpectedSchema || !Policy || Policy->Behavior != Spec.Behavior)
			return Fail(TEXT("Existing schema/policy does not match this migration."));
		const FGuLiCommanderBehaviorPolicy SavedPolicy = *Policy;
		const FString Source = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		const FString Backup = FPaths::ProjectDir() / TEXT("Artifacts/CommanderStateTree/HierarchyV2/Backup") / (FString(Spec.Name) + TEXT(".uasset"));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup), true);
		if (!IFileManager::Get().FileExists(*Source)) return Fail(TEXT("Original package is not saved on disk."));
		if (!IFileManager::Get().FileExists(*Backup) && IFileManager::Get().Copy(*Backup, *Source, false) != COPY_OK)
			return Fail(TEXT("Could not preserve the pre-migration package."));
		Entry->SetStringField(TEXT("backup"), FPaths::ConvertRelativePathToFull(Backup));
		Entry->SetObjectField(TEXT("before"), InspectAsset(Spec));
		Tree->EditorData = CreateGraph(*Tree, Spec, SavedPolicy);
		UStateTreeEditingSubsystem::ValidateStateTree(Tree);
		FStateTreeCompilerLog CompileLog;
		if (!UStateTreeEditingSubsystem::CompileStateTree(Tree, CompileLog) || !Tree->IsReadyToRun())
		{
			CompileLog.DumpToLog(LogGuLiStrike);
			Tree->EditorData = Previous;
			FStateTreeCompilerLog RestoreLog;
			const bool bRestored = UStateTreeEditingSubsystem::CompileStateTree(Tree, RestoreLog);
			Entry->SetBoolField(TEXT("restored_in_memory"), bRestored);
			Package->SetDirtyFlag(!bRestored);
			return Fail(TEXT("New graph did not compile. Original disk package and backup were preserved."));
		}
		Package->GetMetaData().SetValue(Tree, VersionKey, TEXT("2"));
		Tree->MarkPackageDirty();
		const bool bSaved = UEditorLoadingAndSavingUtils::SavePackages({Package}, false);
		Entry->SetBoolField(TEXT("saved"), bSaved);
		Entry->SetObjectField(TEXT("after"), InspectAsset(Spec));
		if (!bSaved) return Fail(TEXT("Migrated graph compiled but could not be saved. Original backup is available."));
		return InspectAsset(Spec)->GetBoolField(TEXT("valid"));
	}

	void MigrateV2()
	{
		const auto Report = MakeShared<FJsonObject>();
		Report->SetStringField(TEXT("migration"), TEXT("Commander hierarchy v2; Miner -> Builder -> Mass"));
		Report->SetStringField(TEXT("time_utc"), FDateTime::UtcNow().ToIso8601());
		bool bSuccess = GEditor && !GEditor->PlayWorld;
		TArray<TSharedPtr<FJsonValue>> Assets;
		if (bSuccess)
		{
			const FString BeforeFile = FPaths::ProjectDir() / TEXT("Artifacts/CommanderStateTree/HierarchyV2/before.json");
			if (!IFileManager::Get().FileExists(*BeforeFile)) bSuccess = WriteReport(TEXT("HierarchyV2/before.json"), InspectReport());
			for (const auto& Spec : Specs)
			{
				if (!bSuccess) break;
				const auto Entry = MakeShared<FJsonObject>();
				bSuccess = MigrateAsset(Spec, Entry);
				Entry->SetBoolField(TEXT("success"), bSuccess);
				Assets.Add(MakeShared<FJsonValueObject>(Entry));
			}
		}
		else Report->SetStringField(TEXT("error"), TEXT("Migration requires an idle editor."));
		Report->SetArrayField(TEXT("assets"), Assets);
		Report->SetBoolField(TEXT("success"), bSuccess);
		WriteReport(TEXT("HierarchyV2/migration.json"), Report);
		InspectAll();
		UE_LOG(LogGuLiStrike, Display, TEXT("Commander hierarchy v2 migration complete: success=%d"), bSuccess);
	}

	FAutoConsoleCommand InspectCommand(TEXT("gs.Commander.InspectStateTrees"),
		TEXT("Read and validate existing Commander assets; recursively export hierarchy, conditions, tasks and transitions."), FConsoleCommandDelegate::CreateStatic(&InspectAll));
	FAutoConsoleCommand MigrateCommand(TEXT("gs.Commander.MigrateStateTreesV2"),
		TEXT("One-time Commander hierarchy v2 migration with backup; versioned assets are never regenerated."), FConsoleCommandDelegate::CreateStatic(&MigrateV2));
}
#endif
