#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GuLiCombatEffectAuthoringLibrary.generated.h"

class UNiagaraSystem;
class UNiagaraScript;
class UNiagaraDataChannelAsset;
class UStaticMesh;

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
	/** Fixes VibeUE dynamic pin order for the project's supported VFX folders, including Construction. Does not save. */
	UFUNCTION(BlueprintCallable, Category="Combat Effects|Editor")
	static bool FinalizeScratchPins(UNiagaraSystem* System);
	/** Read the native Niagara compiler messages omitted by VibeUE's validity-only report. */
	UFUNCTION(BlueprintCallable, Category="Combat Effects|Editor")
	static FString GetConstructionCompileDiagnostics(UNiagaraSystem* System);
	/** Bind project-owned laser particle slots to User arrays. Editor-only; does not save. */
	UFUNCTION(BlueprintCallable, Category="Combat Effects|Editor")
	static bool WireLaserPoolReader(UNiagaraSystem* System, UNiagaraScript* ParticleUpdateScript, bool bMuzzle, FString& Error);
	/** Project explosion refraction meshes already multiply Engine.Owner.Scale. Prevent LocalSpace applying it twice.
	 * Editor-only, idempotent, no saving; refuses unrelated assets and only changes the refr_mesh emitter. */
	UFUNCTION(BlueprintCallable, Category="Combat Effects|Editor")
	static bool NormalizeExplosionRefractionSpace(UNiagaraSystem* System, FString& Error);
#endif
};
