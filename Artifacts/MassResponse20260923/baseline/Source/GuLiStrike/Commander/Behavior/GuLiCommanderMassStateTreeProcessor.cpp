#include "Commander/Behavior/GuLiCommanderMassStateTreeProcessor.h"
#include "Commander/Mass/GuLiCommanderMassFragments.h"
#include "Commander/Orders/GuLiUnitTaskSubsystem.h"
#include "Gameplay/Stronghold/GuLiArmyAdvanceSubsystem.h"
#include "Gameplay/Data/GuLiUnitDataSubsystem.h"
#include "MassStateTreeExecutionContext.h"
#include "MassStateTreeSubsystem.h"
#include "MassEntityManager.h"
#include "MassExecutionContext.h"
#include "MassSubsystemBase.h"
#include "StateTree.h"

namespace
{
	template<typename T>
	void RegisterSubsystemTraits(const TSharedRef<FMassEntityManager>& EntityManager)
	{
		// Query initialization can be repeated; Mass does not allow overriding registered runtime types.
		if (!EntityManager->GetTypeManager().GetTypeInfo(T::StaticClass()))
		{
			UE::Mass::Subsystems::RegisterSubsystemType(EntityManager, T::StaticClass(),
				UE::Mass::FSubsystemTypeTraits::Make<T>());
		}
	}

	void ReleaseInstance(FMassExecutionContext& Context, UMassStateTreeSubsystem& Storage, int32 Index, FGuLiCommanderStateTreeFragment& Fragment)
	{
		if (auto* Data = Storage.GetInstanceData(Fragment.Instance))
		{
			if (Fragment.Tree)
			{
				FMassStateTreeExecutionContext TreeContext(Storage, *Fragment.Tree, *Data, Context);
				TreeContext.SetEntity(Context.GetEntity(Index));
				TreeContext.Stop();
			}
			Storage.FreeInstanceData(Fragment.Instance);
		}
		Fragment.Instance = {};
	}
}

UGuLiCommanderMassStateTreeProcessor::UGuLiCommanderMassStateTreeProcessor() : Query(*this)
{
	ExecutionFlags = static_cast<uint8>(UE::MassStateTree::ExecutionFlags);
	bAutoRegisterWithProcessingPhases = false;
	bRequiresGameThreadExecution = true;
}

void UGuLiCommanderMassStateTreeProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	// UE 5.7 resolves compiled StateTree subsystem dependencies through the per-world type manager.
	// Typed query requirements alone do not register ordinary UWorldSubsystem classes there.
	// Register all three before AllocateInstanceData can create the native dynamic processor.
	RegisterSubsystemTraits<UGuLiUnitTaskSubsystem>(EntityManager);
	RegisterSubsystemTraits<UGuLiArmyAdvanceSubsystem>(EntityManager);
	RegisterSubsystemTraits<UGuLiUnitDataSubsystem>(EntityManager);

	Query.AddRequirement<FGuLiMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddRequirement<FGuLiCommanderStateTreeFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddSubsystemRequirement<UMassStateTreeSubsystem>(EMassFragmentAccess::ReadWrite);
	Query.AddSubsystemRequirement<UGuLiUnitTaskSubsystem>(EMassFragmentAccess::ReadWrite);
	Query.AddSubsystemRequirement<UGuLiArmyAdvanceSubsystem>(EMassFragmentAccess::ReadOnly);
	Query.AddSubsystemRequirement<UGuLiUnitDataSubsystem>(EMassFragmentAccess::ReadOnly);
}

void UGuLiCommanderMassStateTreeProcessor::Execute(FMassEntityManager&, FMassExecutionContext& Context)
{
	Query.ForEachEntityChunk(Context, [](FMassExecutionContext& Chunk)
	{
		auto& Storage = Chunk.GetMutableSubsystemChecked<UMassStateTreeSubsystem>();
		auto& Tasks = Chunk.GetMutableSubsystemChecked<UGuLiUnitTaskSubsystem>();
		const auto Identities = Chunk.GetFragmentView<FGuLiMassIdentityFragment>();
		auto Trees = Chunk.GetMutableFragmentView<FGuLiCommanderStateTreeFragment>();
		for (auto EntityIt = Chunk.CreateEntityIterator(); EntityIt; ++EntityIt)
		{
			const int32 Index = EntityIt;
			auto& Fragment = Trees[Index];
			const auto Unit = FGuLiTaskUnitId::Soldier(Identities[Index].SoldierId);
			if (!Tasks.HasState(Unit)) { ReleaseInstance(Chunk, Storage, Index, Fragment); continue; }
			if (!Tasks.NeedsBehaviorStep(Unit)) continue;
			if (!Fragment.Tree || !Fragment.Tree->IsReadyToRun())
			{ Tasks.ReportBehaviorError(Unit, TEXT("Mass StateTree asset is missing or uncompiled.")); continue; }
			const bool bStarting = !Fragment.bStartAttempted;
			if (bStarting)
			{
				Fragment.bStartAttempted = true;
				Fragment.Instance = Storage.AllocateInstanceData(Fragment.Tree);
			}
			auto* Data = Storage.GetInstanceData(Fragment.Instance);
			if (!Data) { Tasks.ReportBehaviorError(Unit, TEXT("Mass StateTree instance is unavailable.")); continue; }
			FMassStateTreeExecutionContext TreeContext(Storage, *Fragment.Tree, *Data, Chunk);
			TreeContext.SetEntity(Chunk.GetEntity(Index));
			const auto Status = bStarting ? TreeContext.Start() : TreeContext.Tick(Chunk.GetDeltaTimeSeconds());
			if (Status != EStateTreeRunStatus::Running)
				Tasks.ReportBehaviorError(Unit, TEXT("Mass StateTree stopped unexpectedly."));
		}
	});
}

UGuLiCommanderMassStateTreeDestructor::UGuLiCommanderMassStateTreeDestructor() : Query(*this)
{
	ExecutionFlags = static_cast<uint8>(UE::MassStateTree::ExecutionFlags);
	ObservedType = FGuLiCommanderStateTreeFragment::StaticStruct();
	ObservedOperations = EMassObservedOperationFlags::Remove;
	bRequiresGameThreadExecution = true;
}

void UGuLiCommanderMassStateTreeDestructor::ConfigureQueries(const TSharedRef<FMassEntityManager>&)
{
	Query.AddRequirement<FGuLiCommanderStateTreeFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddSubsystemRequirement<UMassStateTreeSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UGuLiCommanderMassStateTreeDestructor::Execute(FMassEntityManager&, FMassExecutionContext& Context)
{
	Query.ForEachEntityChunk(Context, [](FMassExecutionContext& Chunk)
	{
		auto& Storage = Chunk.GetMutableSubsystemChecked<UMassStateTreeSubsystem>();
		auto Trees = Chunk.GetMutableFragmentView<FGuLiCommanderStateTreeFragment>();
		for (auto EntityIt = Chunk.CreateEntityIterator(); EntityIt; ++EntityIt)
			ReleaseInstance(Chunk, Storage, EntityIt, Trees[EntityIt]);
	});
}
