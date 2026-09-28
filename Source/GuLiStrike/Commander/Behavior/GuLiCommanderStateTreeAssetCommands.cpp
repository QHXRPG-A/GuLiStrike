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

namespace GuLiCommanderStateTreeHierarchy { void InspectAll(); }

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

	// Compatibility aliases are read-only. One-time migration has a separate versioned command.
	void BuildAll() { GuLiCommanderStateTreeHierarchy::InspectAll(); }
	void BuildBuilder() { GuLiCommanderStateTreeHierarchy::InspectAll(); }

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
		TEXT("Read-only alias of gs.Commander.InspectStateTrees; does not regenerate assets."), FConsoleCommandDelegate::CreateStatic(&BuildAll));
	FAutoConsoleCommand RefreshCommand(TEXT("gs.Commander.RefreshStateTrees"),
		TEXT("Recompile and save existing Commander trees without changing their authored graphs."), FConsoleCommandDelegate::CreateStatic(&RefreshExisting));
	FAutoConsoleCommand RetireTableCommand(TEXT("gs.Commander.RetireSpecialTaskTable"),
		TEXT("Delete the retired task DataTable only when no assets reference it."), FConsoleCommandDelegate::CreateStatic(&RetireSpecialTaskTable));
}
static FAutoConsoleCommand GGuLiBuildBuilderTree(
    TEXT("gs.Commander.BuildBuilderStateTree"), TEXT("Read existing Commander StateTrees and export the builder report without editing assets."),
    FConsoleCommandDelegate::CreateStatic(&GuLiCommanderStateTreeAssets::BuildBuilder));

#endif
