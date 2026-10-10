#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GuLiCombatEffectAuthoringLibrary.generated.h"

class UNiagaraSystem;
class UNiagaraScript;
class UNiagaraDataChannelAsset;
class UStaticMesh;
class UNiagaraComponent;

/** Small editor bridge for NDC graph pins not exposed by the installed VibeUE 4.0 API. No runtime authoring. */
UCLASS()
class GULISTRIKE_API UGuLiCombatEffectAuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
#if WITH_EDITOR
	/** Fresh, non-rendering container. Refuses to overwrite any existing mesh. Caller supplies geometry and saves. */
	UFUNCTION(BlueprintCallable, Category="Combat Effects|Editor")
	static UStaticMesh* CreateMissileMeshContainer();
	/** Only accepts assets inside the CommanderWeapons project directory. Does not save assets. */
	UFUNCTION(BlueprintCallable, Category="Combat Effects|Editor")
	static bool ConfigureGunfireChannel(UNiagaraDataChannelAsset* Channel, FString& Error);
	/** Bind an emitter-owned DI in spawn, then share it between update and particle-spawn modules. */
	UFUNCTION(BlueprintCallable, Category="Combat Effects|Editor")
	static bool WireGunfireReader(UNiagaraSystem* System, UNiagaraDataChannelAsset* Channel,
		UNiagaraScript* EmitterSpawnScript, UNiagaraScript* EmitterUpdateScript, UNiagaraScript* ParticleSpawnScript, FString& Error);
	UFUNCTION(BlueprintCallable, Category="Combat Effects|Editor")
	static bool ConfigureImpactChannel(UNiagaraDataChannelAsset* Channel, FString& Error);
	UFUNCTION(BlueprintCallable, Category="Combat Effects|Editor")
	static bool ConfigureMuzzleChannel(UNiagaraDataChannelAsset* Channel, FString& Error);
	UFUNCTION(BlueprintCallable, Category="Combat Effects|Editor")
	static bool WireMuzzleRender(UNiagaraSystem* System,FName EmitterName,UNiagaraScript* Module,FString& Error);
	UFUNCTION(BlueprintCallable, Category="Combat Effects|Editor")
	static bool WireImpactReader(UNiagaraSystem* System, UNiagaraDataChannelAsset* Channel,
		UNiagaraScript* Bind, UNiagaraScript* Spawn, UNiagaraScript* Init, int32 MinimumCount, int32 MaximumCount, FString& Error);
	UFUNCTION(BlueprintCallable, Category="Combat Effects|Editor")
	static bool WireImpactLifecycle(UNiagaraSystem* System, UNiagaraScript* Update, FString& Error);
	/** Copy particle-module dependencies into the owned batch and substitute per-event inputs; retain the location-event producer/follower. */
	UFUNCTION(BlueprintCallable, Category="Combat Effects|Editor")
	static bool PrepareImpactBatchSystem(UNiagaraSystem* System, FString& Error);
	UFUNCTION(BlueprintCallable, Category="Combat Effects|Editor")
	static bool MoveClientEffectModule(UNiagaraSystem* System, FName EmitterName, UNiagaraScript* Module, FString Usage, int32 Index, FString& Error);
	UFUNCTION(BlueprintCallable, Category="Combat Effects|Editor")
	static bool ConfigureToolLaserEndpointLOD(UNiagaraSystem* System, FString& Error);
	UFUNCTION(BlueprintCallable, Category="Combat Effects|Editor")
	static bool WireWingmanMeshReader(UNiagaraSystem* System, UNiagaraScript* Module, bool bTrail, FString& Error);
	UFUNCTION(BlueprintCallable, Category="Combat Effects|Editor")
	static bool ConfigureWingmanMeshSystem(UNiagaraSystem* System, FString& Error);
	UFUNCTION(BlueprintCallable, Category="Combat Effects|Editor")
	static FString GetWingmanCoreBaseline(UNiagaraSystem* System);
	/** CPU particle/identity readback for owned impact consumers, outside performance capture windows. */
	UFUNCTION(BlueprintCallable, Category="Combat Effects|Editor")
	static FString InspectImpactBatchComponent(UNiagaraComponent* Component);
	UFUNCTION(BlueprintCallable, Category="Combat Effects|Editor")
	static FString GetImpactCompiledSource(UNiagaraSystem* System);
	/** Fixes VibeUE dynamic pin order for the project's supported VFX folders, including Construction. Does not save. */
	UFUNCTION(BlueprintCallable, Category="Combat Effects|Editor")
	static bool FinalizeScratchPins(UNiagaraSystem* System);
	/** Read the native Niagara compiler messages omitted by VibeUE's validity-only report. */
	UFUNCTION(BlueprintCallable, Category="Combat Effects|Editor")
	static FString GetConstructionCompileDiagnostics(UNiagaraSystem* System);
	/** Bind project-owned laser particle slots to User arrays. Editor-only; does not save. */
	UFUNCTION(BlueprintCallable, Category="Combat Effects|Editor")
	static bool WireLaserPoolReader(UNiagaraSystem* System, UNiagaraScript* ParticleUpdateScript, bool bMuzzle, FString& Error);
	/** Only the owned small-batch copy. Binds burst capacity to User.LaserSlotCount; does not save. */
	UFUNCTION(BlueprintCallable, Category="Combat Effects|Editor")
	static bool BindLaserPoolCapacity(UNiagaraSystem* System, FString& Error);
	/** Binds the task-owned upgrade pool to per-unit positions/age/scale/color arrays. */
	UFUNCTION(BlueprintCallable, Category="Combat Effects|Editor")
	static bool WireRogueUpgradePoolReader(UNiagaraSystem* System, UNiagaraScript* ParticleUpdateScript, FString& Error);
	UFUNCTION(BlueprintCallable, Category="Combat Effects|Editor")
	static bool ConfigureRogueUpgradeSystem(UNiagaraSystem* System, FString& Error);
	/** GPU emitters and fixed bounds for the task-owned WarMachine hover array pool only. */
	UFUNCTION(BlueprintCallable, Category="Combat Effects|Editor")
	static bool ConfigureWarMachineHoverSystem(UNiagaraSystem* System, FString& Error);
	/** GPU body/flame/history renderers for dedicated WM01 cluster systems only. */
	UFUNCTION(BlueprintCallable, Category="Combat Effects|Editor")
	static bool ConfigureMissileClusterSystem(UNiagaraSystem* System, FString& Error);
	/** Read rendered LOD UVs, including automatically reduced LODs unavailable to Python mesh descriptions. */
	UFUNCTION(BlueprintCallable, Category="Combat Effects|Editor")
	static FString GetMissilePodMeshDiagnostics(UStaticMesh* Mesh);
	/** Read actual VM and GPU shader diagnostics, beyond the service's readiness-only check. */
	UFUNCTION(BlueprintCallable, Category="Combat Effects|Editor")
	static FString GetWarMachineHoverCompileDiagnostics(UNiagaraSystem* System);
	/** Only frozen tool-laser GPU review copies. Never saves or alters production emitters. */
	UFUNCTION(BlueprintCallable, Category="Combat Effects|Editor")
	static bool ConfigureToolLaserGPUCandidate(UNiagaraSystem* System, FString& Error);
	/** Read-only graph overrides and curve data for the machine-gun source/review copies.
	 * This complements the service's module-default pins when auditing complete pre-spawn bounds. */
	UFUNCTION(BlueprintCallable, Category="Combat Effects|Editor")
	static FString GetMachineGunBoundsInputs(UNiagaraSystem* System);
	/** Authored pre-spawn envelope only; retains the original dynamic simulation bounds. Does not save. */
	UFUNCTION(BlueprintCallable, Category="Combat Effects|Editor")
	static bool ConfigureMachineGunBoundsEnvelope(UNiagaraSystem* System, FVector Extent, FString& Error);
	UFUNCTION(BlueprintCallable, Category="Combat Effects|Editor")
	static FString GetRogueUpgradeCompileDiagnostics(UNiagaraSystem* System);
	/** Project explosion refraction meshes already multiply Engine.Owner.Scale. Prevent LocalSpace applying it twice.
	 * Editor-only, idempotent, no saving; refuses unrelated assets and only changes the refr_mesh emitter. */
	UFUNCTION(BlueprintCallable, Category="Combat Effects|Editor")
	static bool NormalizeExplosionRefractionSpace(UNiagaraSystem* System, FString& Error);
#endif
};
