#pragma once

#include "CoreMinimal.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectDefinition.h"
#include "GuLiMissileClusterPresentation.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;
class UGuLiCommanderLODSubsystem;

/** Cosmetic GPU pools, separate from collision/damage and the wingman flight asset. */
UCLASS()
class GULISTRIKE_API UGuLiMissileClusterPresentation : public UObject
{
	GENERATED_BODY()
public:
	virtual UWorld* GetWorld() const override;
	void BeginFrame(double Now, bool bEnabled);
	/** False keeps the legacy visual alive while assets/the component are unavailable. */
	bool Submit(const FGuid& Id, const FVector& Position, const FVector& Direction, const UGuLiProjectileEffectDefinition* Definition);
	bool IsReady() const;
	void Finish(const FGuid& Id, const FVector& ImpactPosition, bool bImmediate);
	void EndFrame();
	void Reset();
	int32 GetSystemCount() const { return SystemCount; }
	int32 GetParticleCapacity() const { return ParticleCapacity; }
	int32 GetActiveMissiles() const { return ActiveMissiles; }
	int32 GetFullTrails() const { return FullTrails; }
	double GetUpdateMilliseconds() const { return UpdateMilliseconds; }
	static constexpr int32 MissilesPerBatch = 64;
	static constexpr float TrailLifetime = 2.4f;
	static constexpr float HandoffSeconds = 0.15f;

private:
	struct FSlot
	{
		FGuid Id;
		FVector Position = FVector::ZeroVector, Direction = FVector::ForwardVector;
		uint32 Generation = 1;
		double LastSeen = -1000, FinishedAt = -1;
		bool bAllocated = false, bSubmitted = false;
	};
	struct FBoundsSample { FBox Bounds = FBox(ForceInit); double Time = 0; };
	struct FRetiringBatch
	{
		TWeakObjectPtr<UNiagaraComponent> Component;
		FBox Bounds = FBox(ForceInit);
		double ReleaseTime = 0;
		int32 Capacity = 0, FullTrails = 0;
	};
	struct FBatch
	{
		TWeakObjectPtr<UNiagaraComponent> Component;
		TWeakObjectPtr<const UGuLiProjectileEffectDefinition> Definition;
		FGuLiProjectileVisualSettings Visual;
		FIntVector Cell = FIntVector::ZeroValue;
		TArray<FSlot> Slots;
		TArray<FVector> Positions, Directions;
		TArray<FVector2D> Sizes;
		TArray<FLinearColor> Meta;
		TArray<FBoundsSample> RecentBounds;
		FBox Bounds = FBox(ForceInit);
		FRetiringBatch Retiring;
		// Freeze the initial occupied prefix until this batch is empty. Later shots
		// use free slots inside it or a new batch, never restart another shot's tail.
		int32 Capacity = 0, Occupied = 0, Quality = -1;
		double QualitySince = -1000, RetryAt = 0;
	};
	TArray<FBatch> Batches;
	TMap<FGuid, int32> SlotById;
	TMap<TWeakObjectPtr<const UGuLiProjectileEffectDefinition>, FGuLiProjectileVisualSettings> VisualProfiles;
	double FrameTime = 0;
	bool bFrameEnabled = false, bResourcesReady = false;
	UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraSystem>> Systems;
	TWeakObjectPtr<UGuLiCommanderLODSubsystem> CommanderLOD;
	double NextResourceCheck = 0;
	int32 SystemCount = 0, ParticleCapacity = 0, ActiveMissiles = 0, FullTrails = 0;
	double UpdateMilliseconds = 0;
	int32 Acquire(const FGuid& Id, const FVector& Position, const UGuLiProjectileEffectDefinition* Definition, const FGuLiProjectileVisualSettings& Visual);
	bool StartBatch(FBatch& Batch, int32 Quality);
	void Upload(FBatch& Batch);
	void RetireHistory(FBatch& Batch);
	static void ReleaseComponent(TWeakObjectPtr<UNiagaraComponent>& Component);
	static void Release(FBatch& Batch);
};
