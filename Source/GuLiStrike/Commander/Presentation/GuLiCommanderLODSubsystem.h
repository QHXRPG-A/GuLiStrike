#pragma once

#include "CoreMinimal.h"
#include "Commander/Presentation/GuLiCommanderLODPolicy.h"
#include "ConvexVolume.h"
#include "Subsystems/WorldSubsystem.h"
#include "GuLiCommanderLODSubsystem.generated.h"

/** Client-World policy shared by commander consumers. No actors, effects or gameplay are owned here. */
UCLASS()
class GULISTRIKE_API UGuLiCommanderLODSubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	FGuLiCommanderLODDecision Evaluate(const FGuLiCommanderLODQuery& Query);
	const FGuLiCommanderLODSettings& GetSettings() const { return EffectiveSettings; }
	TArray<FGuLiCommanderLODSettingView> ListSettings(const FString& Prefix = FString()) const;
	bool SetSetting(const FString& Key, const FString& Value, FString& Error);
	bool ResetSetting(const FString& KeyOrAll, FString& Error);

	/** One complete snapshot per consumer, per frame; clear when that consumer resets or retires. */
	void ReportConsumer(FName Consumer, const FGuLiCommanderLODConsumerStats& Stats);
	void ClearConsumer(FName Consumer);
	const TMap<FName, FGuLiCommanderLODConsumerStats>& GetConsumerStats() const { return ConsumerStats; }
	uint64 GetViewFrame() const { return ViewFrame; }
	int32 GetLocalViewCount() const { return Views.Num(); }
	int32 GetQueryCount() const { return QueryCount; }
	double GetViewUpdateMilliseconds() const { return ViewUpdateMilliseconds; }

private:
	struct FLocalView
	{
		FVector Position = FVector::ZeroVector;
		FConvexVolume Frustum;
		float ProjectionScale = 1;
	};
	void EnsureViews();
	FGuLiCommanderLODSettings BaselineSettings;
	FGuLiCommanderLODSettings EffectiveSettings;
	TSet<FString> ConfigKeys;
	TSet<FString> OverrideKeys;
	TArray<FLocalView> Views;
	TMap<FName, FGuLiCommanderLODConsumerStats> ConsumerStats;
	uint64 ViewFrame = MAX_uint64;
	int32 QueryCount = 0;
	double ViewUpdateMilliseconds = 0;
};
