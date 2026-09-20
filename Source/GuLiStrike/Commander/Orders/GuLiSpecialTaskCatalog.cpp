#include "Commander/Orders/GuLiSpecialTaskCatalog.h"
#include "Commander/Orders/GuLiSpecialTaskExecutor.h"
#include "Gameplay/Data/GuLiUnitDataSubsystem.h"
#include "Gameplay/Data/Generated/GuLiStrikeSpecialTasksTableRows.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "NativeGameplayTags.h"
#include "GuLiStrike.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_GuLi_Task_Mining, "Task.Special.Mining");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_GuLi_Task_Construction, "Task.Special.Construction");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_GuLi_Task_StrongholdAdvance, "Task.Special.StrongholdAdvance");

UGuLiUnitTaskSettings::UGuLiUnitTaskSettings()
	: SpecialTaskTable(FSoftObjectPath(TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeSpecialTasks_Tasks.DT_GuLiStrikeSpecialTasks_Tasks"))) {}

bool UGuLiSpecialTaskCatalog::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld();
}
void UGuLiSpecialTaskCatalog::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UGuLiUnitDataSubsystem>();
	if (!Load(GetDefault<UGuLiUnitTaskSettings>()->SpecialTaskTable.LoadSynchronous()))
		UE_LOG(LogGuLiStrike, Error, TEXT("SpecialTasks: %s"), *Error);
}
const FGuLiSpecialTaskDefinition* UGuLiSpecialTaskCatalog::Find(int32 Id) const
{ return Definitions.FindByPredicate([Id](const auto& Definition) { return Definition.Id == Id; }); }
const FGuLiSpecialTaskDefinition* UGuLiSpecialTaskCatalog::Find(FGameplayTag Tag, uint16 UnitTypeId) const
{ return Definitions.FindByPredicate([&](const auto& Definition) { return Definition.Tag == Tag && Definition.UnitTypes.Contains(UnitTypeId); }); }

bool UGuLiSpecialTaskCatalog::Load(UDataTable* Table)
{
	Definitions.Reset(); Executors.Reset(); Error.Reset();
	auto Fail = [&](const FString& Reason) { Error = Reason; Definitions.Reset(); Executors.Reset(); return false; };
	if (!Table || Table->GetRowStruct() != FGuLiStrikeSpecialTasksTasksRow::StaticStruct())
		return Fail(TEXT("Tasks DataTable missing or using an incompatible native row structure."));
	const auto* Units = GetWorld()->GetSubsystem<UGuLiUnitDataSubsystem>();
	if (!Units || !Units->IsCatalogValid()) return Fail(TEXT("Soldiers catalog is invalid."));
	if (Table->GetRowNames().IsEmpty()) return Fail(TEXT("Tasks DataTable has no definitions."));
	TSet<FGameplayTag> SeenTags;
	for (FName RowName : Table->GetRowNames())
	{
		const auto& Row = *Table->FindRow<FGuLiStrikeSpecialTasksTasksRow>(RowName, TEXT("SpecialTasks"));
		auto RowFail = [&](const TCHAR* Field) { return Fail(FString::Printf(TEXT("Tasks/%s: invalid %s"), *RowName.ToString(), Field)); };
		FGuLiSpecialTaskDefinition Definition;
		Definition.Id = Row.Id; Definition.DisplayName = Row.DisplayName;
		Definition.Tag = FGameplayTag::RequestGameplayTag(FName(*Row.TaskTag), false);
		Definition.bAutoActivate = Row.AutoActivate;
		if (Row.Id <= 0 || Find(Row.Id)) return RowFail(TEXT("id"));
		if (Row.DisplayName.TrimStartAndEnd().IsEmpty()) return RowFail(TEXT("DisplayName"));
		if (!Definition.Tag.IsValid() || SeenTags.Contains(Definition.Tag)) return RowFail(TEXT("TaskTag"));
		if (Row.LifetimePolicy != TEXT("InitialOnce") && Row.LifetimePolicy != TEXT("Persistent")) return RowFail(TEXT("LifetimePolicy"));
		Definition.Lifetime = Row.LifetimePolicy == TEXT("InitialOnce") ? EGuLiTaskLifetime::InitialOnce : EGuLiTaskLifetime::Persistent;
		UClass* Class = Row.ExecutorClass.LoadSynchronous();
		if (!Class || !Class->IsChildOf(UGuLiSpecialTaskExecutor::StaticClass())
			|| Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
			|| !Class->HasAnyClassFlags(CLASS_Native)) return RowFail(TEXT("ExecutorClass (concrete native UGuLiSpecialTaskExecutor required)"));
		UGuLiSpecialTaskExecutor* Executor = nullptr;
		for (auto& Existing : Executors) if (Existing->GetClass() == Class) Executor = Existing;
		if (!Executor) { Executor = NewObject<UGuLiSpecialTaskExecutor>(this, Class); Executors.Add(Executor); }
		TArray<FString> Ids; Row.ApplicableUnitIds.ParseIntoArray(Ids, TEXT(","), false);
		for (const FString& Text : Ids)
		{
			int32 Id = 0;
			if (!Text.IsNumeric() || !LexTryParseString(Id, *Text) || Id < 1 || Id > MAX_uint16 || Definition.UnitTypes.Contains(uint16(Id))) return RowFail(TEXT("ApplicableUnitIds"));
			const auto* Unit = Units->FindDefinition(uint16(Id));
			if (!Unit || !Executor->SupportsDefinition(Definition.Tag, *Unit)) return RowFail(TEXT("ApplicableUnitIds/ExecutorClass capability"));
			Definition.UnitTypes.Add(uint16(Id));
		}
		if (Definition.UnitTypes.IsEmpty()) return RowFail(TEXT("ApplicableUnitIds"));
		Definition.Executor = Executor; SeenTags.Add(Definition.Tag); Definitions.Add(MoveTemp(Definition));
	}
	Definitions.Sort([](const auto& A, const auto& B) { return A.Id < B.Id; });
	return true;
}
