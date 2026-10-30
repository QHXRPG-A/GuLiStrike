#pragma once

#include "CoreMinimal.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectTypes.h"
#include "Gameplay/CombatEffects/GuLiPresentationSlotPool.h"
#include "GuLiBulletTrailPresentation.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;
class UGuLiCombatEffectCatalog;

USTRUCT()
struct FGuLiBulletTrailBlock
{
	GENERATED_BODY()
	UPROPERTY() TObjectPtr<UNiagaraComponent> Component;
	TArray<FVector> Origins, Directions, Facings;
	TArray<float> Speeds;
	TArray<FVector4> Times;
	TArray<FLinearColor> Colors;
	TArray<int32> Generations;
	bool bLaunchDirty=true, bLifeDirty=true, bColorDirty=true;
	FBox Bounds=FBox(ForceInit), UploadedBounds=FBox(ForceInit);
};

/** Render-only persistent GPU strokes and a separate, bounded CPU particle-light system. */
UCLASS()
class GULISTRIKE_API UGuLiBulletTrailPresentation final : public UObject
{
	GENERATED_BODY()
public:
	virtual UWorld* GetWorld() const override;
	void Prepare(UGuLiCombatEffectCatalog* Catalog);
	bool HasLightSystem() const { return LightSystem!=nullptr; }
	float GetMaximumLength() const { return MaximumLength; }
	float GetStrokeEnvelope() const { return 2.f*float(Style.X+Style.Y); }
	void BeginFrame(float Now, bool bEnabled);
	/** Returns true once the last historical point has faded. The caller still owns flight state. */
	bool SubmitMass(const FGuLiCombatEffectState& State, FVector Origin, FVector Facing, FLinearColor Tint, bool bVisible);
	void SubmitLight(FGuid Id, FVector Head, FLinearColor Tint, double DistanceSquared, bool bVisible,
		float NearDistance, float FarDistance);
	void EndFrame();
	void Remove(FGuid Id);
	void Reset();
	int32 GetActiveCount() const { return Entries.Num(); }
	int32 GetComponentCount() const;
	int32 GetLightCount() const { return LightCount; }
	uint64 GetArrayUploads() const { return ArrayUploads; }
private:
	static constexpr int32 BlockSize=256;
	static constexpr int32 MaximumLights=6;
	struct FEntry
	{
		FGuLiPresentationSlotHandle Handle;
		FVector Origin=FVector::ZeroVector, Direction=FVector::ForwardVector;
		float Start=0, End=0, Speed=1, TerminalTime=-1, TerminalDistance=0;
	};
	struct FLight
	{
		FGuid Id;
		FVector Position;
		FLinearColor Color;
		double DistanceSquared;
	};
	void EnsureBlock(int32 Index);
	void ReleaseComponent(TObjectPtr<UNiagaraComponent>& Component);
	UNiagaraComponent* AcquireComponent(UNiagaraSystem* System, int32 Capacity);
	UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> MassSystem;
	UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> LightSystem;
	UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> Lights;
	UPROPERTY(Transient) TArray<FGuLiBulletTrailBlock> Blocks;
	FGuLiPresentationSlotPool Slots{BlockSize,BlockSize};
	TMap<FGuid,FEntry> Entries;
	TSet<FGuid> NearLights, SeenLights;
	TArray<FLight,TInlineAllocator<MaximumLights>> LightCandidates;
	TArray<FVector> LightPositions;
	TArray<FLinearColor> LightColors;
	TArray<float> LightRadii;
	TArray<bool> LightEnabled;
	FVector4 Style=FVector4(6,12,.06,.18);
	float MaximumLength=1200, Emission=24, Radius=800, Brightness=25;
	float Now=0, ClockOrigin=0;
	int32 LightBudget=MaximumLights, LightCount=0;
	bool bClockInitialized=false, bEnabled=false;
	uint64 ArrayUploads=0;
};
