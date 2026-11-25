#include "Gameplay/CombatEffects/GuLiUnitFeedbackSubsystem.h"
#include "Gameplay/Cards/GuLiRogueCardSettings.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectDefinition.h"
#include "Gameplay/Data/Generated/GuLiStrikeRogueCardsTableRows.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "Engine/AssetManager.h"
#include "Engine/DataTable.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "NiagaraSystem.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

DEFINE_LOG_CATEGORY_STATIC(LogGuLiExplosionPreload, Log, All);

namespace
{
TAutoConsoleVariable<int32> PrecompileDestruction(TEXT("gs.CombatEffects.PrecompileDestruction"), 1,
	TEXT("Prepare editor Niagara compilation for startup explosions. 0 keeps on-demand compilation."));

void AddVisual(TArray<int32>& Ids, int32 Id)
{
	if (Id > 0) Ids.AddUnique(Id);
}
}

void UGuLiUnitFeedbackSubsystem::StartExplosionPreload()
{
	ExplosionPreloadStage = EExplosionPreloadStage::Definitions;
	TArray<FSoftObjectPath> Paths;
	for (const auto& Field : GetDefault<UGuLiCombatEffectSettings>()->StartupExplosionFields)
		if (!Field.IsNull()) Paths.AddUnique(Field.ToSoftObjectPath());
	const auto& WeaponTable = GetDefault<UGuLiRogueCardSettings>()->GroundWeaponEffects;
	if (!WeaponTable.IsNull()) Paths.AddUnique(WeaponTable.ToSoftObjectPath());

	if (!Paths.IsEmpty())
		ExplosionDefinitionsLoad = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths,
			FStreamableDelegate::CreateUObject(this, &ThisClass::RequestExplosionSystems));
	if (!ExplosionDefinitionsLoad) RequestExplosionSystems();
}

void UGuLiUnitFeedbackSubsystem::RequestExplosionSystems()
{
	if (ExplosionPreloadStage != EExplosionPreloadStage::Definitions) return;
	ExplosionPreloadStage = EExplosionPreloadStage::Systems;
	const auto* Settings = GetDefault<UGuLiUnitFeedbackSettings>();
	for (int32 Id : Settings->ExplosionVfxIds) AddVisual(StartupExplosionVfxIds, Id);
	for (int32 Id : Settings->WingmanExplosionVfxIds) AddVisual(StartupExplosionVfxIds, Id);
	for (const auto& Reference : GetDefault<UGuLiCombatEffectSettings>()->StartupExplosionFields)
	{
		UGuLiSpellFieldDefinition* Field = Reference.Get();
		if (!Field)
		{
			UE_LOG(LogGuLiExplosionPreload, Error, TEXT("Could not preload explosion field %s."), *Reference.ToString());
			continue;
		}
		LoadedExplosionFields.AddUnique(Field);
		AddVisual(StartupExplosionVfxIds, Field->WaitingVfxId);
		AddVisual(StartupExplosionVfxIds, Field->ActiveLoopVfxId);
		for (const auto& Variant : Field->ActivationVariants)
		{
			AddVisual(StartupExplosionVfxIds, Variant.VfxId);
			for (const auto& Layer : Variant.AdditionalLayers) AddVisual(StartupExplosionVfxIds, Layer.VfxId);
		}
	}

	const auto& WeaponTable = GetDefault<UGuLiRogueCardSettings>()->GroundWeaponEffects;
	const UDataTable* Table = WeaponTable.Get();
	if (Table && Table->GetRowStruct() == FGuLiStrikeRogueCardsGroundWeaponEffectsRow::StaticStruct())
	{
		Table->ForeachRow<FGuLiStrikeRogueCardsGroundWeaponEffectsRow>(TEXT("Startup explosions"),
			[this](FName, const FGuLiStrikeRogueCardsGroundWeaponEffectsRow& Row)
			{
				AddVisual(StartupExplosionVfxIds, Row.ExplosiveImpactVfxId);
			});
	}
	else
	{
		UE_LOG(LogGuLiExplosionPreload, Error, TEXT("Could not read ground weapon impact table %s."), *WeaponTable.ToString());
	}

	auto* Registry = GetWorld()->GetSubsystem<UGuLiVfxRegistrySubsystem>();
	TArray<FSoftObjectPath> Paths;
	for (int32 Id : StartupExplosionVfxIds)
	{
		const FSoftObjectPath Path = Registry->ReservePreload(Id);
		if (!Path.IsNull()) Paths.AddUnique(Path);
	}
	if (!Paths.IsEmpty())
		ExplosionSystemsLoad = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths,
			FStreamableDelegate::CreateUObject(this, &ThisClass::FinishExplosionPreload));
	if (!ExplosionSystemsLoad) FinishExplosionPreload();
}

void UGuLiUnitFeedbackSubsystem::FinishExplosionPreload()
{
	if (ExplosionPreloadStage != EExplosionPreloadStage::Systems) return;
	ExplosionPreloadStage = EExplosionPreloadStage::Finished;
	TSet<UNiagaraSystem*> Systems;
	TSet<int32> LoadedIds;
	for (int32 Id : StartupExplosionVfxIds)
	{
		if (auto* System = GuLiVfx::Load<UNiagaraSystem>(this, Id, false))
		{
			LoadedIds.Add(Id);
			Systems.Add(System);
		}
		else
		{
			UE_LOG(LogGuLiExplosionPreload, Error, TEXT("Could not preload Niagara system for explosion VfxId %d."), Id);
		}
	}
	for (UNiagaraSystem* System : Systems) PrepareExplosionCompilation(System);
	const auto* Settings = GetDefault<UGuLiUnitFeedbackSettings>();
	for (int32 Id : Settings->ExplosionVfxIds)
		if (LoadedIds.Contains(Id)) LoadedExplosions.AddUnique(Id);
	for (int32 Id : Settings->WingmanExplosionVfxIds)
		if (LoadedIds.Contains(Id)) LoadedWingmanExplosions.AddUnique(Id);

	UE_LOG(LogGuLiExplosionPreload, Log, TEXT("Prepared %d/%d explosion VFX IDs (%d distinct systems) and %d field definitions."),
		LoadedIds.Num(), StartupExplosionVfxIds.Num(), Systems.Num(), LoadedExplosionFields.Num());
	const float Now = GetWorld()->GetTimeSeconds();
	for (const auto& Pending : PendingExplosions)
		if (Pending.ExpireTime >= Now) SpawnExplosion(Pending.Location, Pending.UnitSize, Pending.bWingman, Pending.ScaleMultiplier);
	PendingExplosions.Reset();
}

void UGuLiUnitFeedbackSubsystem::PrepareExplosionCompilation(UNiagaraSystem* System)
{
#if WITH_EDITORONLY_DATA
	if (PrecompileDestruction.GetValueOnGameThread() == 0) return;
	if (System->NeedsRequestCompile())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GuLiUnitFeedback_PrepareExplosionCompilation);
		++ExplosionCompileRequests;
		System->RequestCompile(false);
	}
	if (System->HasOutstandingCompilationRequests(true)) PreparingExplosionSystems.AddUnique(System);
#endif
}

void UGuLiUnitFeedbackSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiExplosionPreload_BeforePlay);
	// Streamable callbacks may be deferred past the load itself. Complete each phase once,
	// before actors begin play, even if the World opened faster than the async requests.
	if (ExplosionDefinitionsLoad && !ExplosionDefinitionsLoad->HasLoadCompleted())
		ExplosionDefinitionsLoad->WaitUntilComplete();
	RequestExplosionSystems();
	if (ExplosionSystemsLoad && !ExplosionSystemsLoad->HasLoadCompleted())
		ExplosionSystemsLoad->WaitUntilComplete();
	FinishExplosionPreload();
#if WITH_EDITORONLY_DATA
	for (const auto& Weak : PreparingExplosionSystems)
		if (auto* System = Weak.Get()) System->WaitForCompilationComplete(true, false);
	PreparingExplosionSystems.Reset();
#endif
	// The fields and registry now own the lifetime of the assets for this World.
	ExplosionDefinitionsLoad.Reset();
	ExplosionSystemsLoad.Reset();
}
