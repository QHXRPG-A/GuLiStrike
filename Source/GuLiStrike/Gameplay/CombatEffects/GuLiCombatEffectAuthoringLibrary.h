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
	/** VibeUE appends custom-HLSL pins after the '+' pin; UE5.7 requires the '+' pin last. */
	UFUNCTION(BlueprintCallable, Category="Combat Effects|Editor")
	static bool FinalizeScratchPins(UNiagaraSystem* System);
#endif
};
