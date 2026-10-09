#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Gameplay/Data/Generated/GuLiStrikeModelsTableRows.h"
#include "Gameplay/Data/Generated/GuLiModelIds.h"
#include "GuLiModelRegistrySubsystem.generated.h"

class AActor;
class UMeshComponent;
class UStaticMesh;
class USkeletalMesh;

UCLASS(Config=Game, DefaultConfig)
class GULISTRIKE_API UGuLiModelRegistrySettings : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(Config, EditAnywhere, Category="Models") TSoftObjectPtr<UDataTable> ModelsTable;
	UPROPERTY(Config, EditAnywhere, Category="Models") TSoftObjectPtr<UDataTable> PartsTable;
	UPROPERTY(Config, EditAnywhere, Category="Models") TSoftObjectPtr<UDataTable> ParametersTable;
	UPROPERTY(Config, EditAnywhere, Category="Models") TSoftObjectPtr<UDataTable> RegionsTable;
};

/** Excel is the single authoring source. Gameplay socket/pose resources also load on servers. */
UCLASS()
class GULISTRIKE_API UGuLiModelRegistrySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	/** Editor pipeline reload after reimport; runtime actors keep their own resource references. */
	UFUNCTION(BlueprintCallable, Category="Models") bool ReloadCatalog();
	UFUNCTION(BlueprintCallable, Category="Models") bool GetModelDefinition(int32 ModelId, FGuLiStrikeModelsModelsRow& Definition);
	UFUNCTION(BlueprintCallable, Category="Models") TArray<FGuLiStrikeModelsPartsRow> GetModelParts(int32 ModelId);
	UFUNCTION(BlueprintCallable, Category="Models") TArray<FGuLiStrikeModelsColorRegionsRow> GetMutableColorRegions(int32 ModelId, bool ReviewCandidate = false);
	UFUNCTION(BlueprintCallable, Category="Models") TArray<FGuLiStrikeModelsMaterialParametersRow> GetMaterialParameters(int32 ModelId, bool ReviewCandidate = false);
	UFUNCTION(BlueprintCallable, Category="Models", meta=(DeterminesOutputType="ExpectedClass"))
	UObject* LoadModelResource(int32 ModelId, TSubclassOf<UObject> ExpectedClass, bool ReviewCandidate = false);
	UFUNCTION(BlueprintCallable, Category="Models") UStaticMesh* LoadStaticModel(int32 ModelId, bool ReviewCandidate = false);
	UFUNCTION(BlueprintCallable, Category="Models") USkeletalMesh* LoadSkeletalModel(int32 ModelId, bool ReviewCandidate = false);
	UFUNCTION(BlueprintCallable, Category="Models") TSubclassOf<AActor> LoadPresentationClass(int32 ModelId, bool ReviewCandidate = false);
	UFUNCTION(BlueprintCallable, Category="Models") bool ApplyModelParts(AActor* Target, int32 ModelId);
	int32 FindModelIdForResource(const UObject* Resource);
	/** Only registered, writable parameters can be changed; team keys are owned by ApplyLocalTeamColors. */
	UFUNCTION(BlueprintCallable, Category="Models|Parameters")
	bool SetVectorParameter(UMeshComponent* Target, int32 ModelId, const FString& PartKey, const FString& Slot, const FString& Key, FLinearColor Value, bool ReviewCandidate = false);
	UFUNCTION(BlueprintCallable, Category="Models|Parameters")
	bool SetScalarParameter(UMeshComponent* Target, int32 ModelId, const FString& PartKey, const FString& Slot, const FString& Key, float Value, bool ReviewCandidate = false);
	UFUNCTION(BlueprintCallable, Category="Models|Parameters")
	bool GetVectorParameter(UMeshComponent* Target, int32 ModelId, const FString& PartKey, const FString& Slot, const FString& Key, FLinearColor& Value, bool ReviewCandidate = false);
	UFUNCTION(BlueprintCallable, Category="Models|Parameters")
	bool GetScalarParameter(UMeshComponent* Target, int32 ModelId, const FString& PartKey, const FString& Slot, const FString& Key, float& Value, bool ReviewCandidate = false);
	static bool Query(const UObject* Context, int32 ModelId, FGuLiStrikeModelsModelsRow& Definition);
	static UObject* Resolve(const UObject* Context, int32 ModelId, UClass* ExpectedClass);
	static bool ParseColor(const FString& Hex, FLinearColor& Color);
	static int32 FindMaterialSlot(const UMeshComponent* Mesh, const FString& Slot);
	static UMeshComponent* FindPart(AActor* Actor, const FString& Path);
	static bool ReadVector(UMeshComponent* Target, const FGuLiStrikeModelsMaterialParametersRow& Binding, FLinearColor& Value);
	static bool ReadScalar(UMeshComponent* Target, const FGuLiStrikeModelsMaterialParametersRow& Binding, float& Value);
	static bool WriteVector(UMeshComponent* Target, const FGuLiStrikeModelsMaterialParametersRow& Binding, FLinearColor Value, bool TeamDriver = false);
	static bool WriteScalar(UMeshComponent* Target, const FGuLiStrikeModelsMaterialParametersRow& Binding, float Value, bool TeamDriver = false);
private:
	bool EnsureCatalog();
	bool FindBinding(int32 ModelId, const FString& Part, const FString& Slot, const FString& Key, bool Candidate, FGuLiStrikeModelsMaterialParametersRow& Out);
	bool bAttempted = false;
	UPROPERTY(Transient) TArray<TObjectPtr<UDataTable>> Tables;
	UPROPERTY(Transient) TMap<int32, FGuLiStrikeModelsModelsRow> Models;
	UPROPERTY(Transient) TArray<FGuLiStrikeModelsPartsRow> Parts;
	UPROPERTY(Transient) TArray<FGuLiStrikeModelsMaterialParametersRow> Parameters;
	UPROPERTY(Transient) TArray<FGuLiStrikeModelsColorRegionsRow> Regions;
	UPROPERTY(Transient) TMap<FSoftObjectPath, TObjectPtr<UObject>> Resources;
};

namespace GuLiModels
{
	template<class T> T* Load(const UObject* Context, int32 ModelId)
	{ return Cast<T>(UGuLiModelRegistrySubsystem::Resolve(Context, ModelId, T::StaticClass())); }
	template<class T> UClass* LoadClass(const UObject* Context, int32 ModelId)
	{
		auto* Class = Load<UClass>(Context, ModelId);
		return Class && Class->IsChildOf(T::StaticClass()) ? Class : nullptr;
	}
	GULISTRIKE_API FSoftObjectPath Path(const UObject* Context, int32 ModelId);
}
