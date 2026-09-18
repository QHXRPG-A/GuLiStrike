#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Subsystems/WorldSubsystem.h"
#include "GuLiGroundWarningSubsystem.generated.h"

class UDecalComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/** Reusable circular ground cue. No gameplay, unit or projectile dependencies. */
UCLASS(BlueprintType)
class GULISTRIKE_API UGuLiGroundWarningStyle : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ground Warning") TSoftObjectPtr<UMaterialInterface> Material;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ground Warning", meta=(ClampMin="0.05", Units="s")) float WavePeriod = 0.8f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ground Warning", meta=(ClampMin="0.001", ClampMax="0.2")) float RingWidth = 0.025f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ground Warning", meta=(ClampMin="0", ClampMax="1")) float Opacity = 0.85f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ground Warning", meta=(ClampMin="1", Units="cm")) float ProjectionDepth = 80.0f;
	bool IsValidStyle() const;
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiGroundWarningParams
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ground Warning") FVector Location = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ground Warning", meta=(Units="cm")) float Radius = 160.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ground Warning") FLinearColor Color = FLinearColor(1.0f, 0.025f, 0.015f, 1.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ground Warning") TObjectPtr<UGuLiGroundWarningStyle> Style;
	/** GameState server-world clock, including for local-only previews. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ground Warning") double StartServerSeconds = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ground Warning") double ExpireServerSeconds = 0;
};

USTRUCT()
struct FGuLiGroundWarningCircle
{
	GENERATED_BODY()
	UPROPERTY() FGuLiGroundWarningParams Params;
	UPROPERTY() TObjectPtr<UDecalComponent> Decal;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Material;
	int32 References = 0;
};

/** Render-world service. Callers own stable IDs; matching circles share a pooled decal. */
UCLASS()
class GULISTRIKE_API UGuLiGroundWarningSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;
	/** Idempotent create/update. Invalid or expired updates remove that ID and return false. */
	UFUNCTION(BlueprintCallable, Category="Ground Warning") bool UpsertWarning(FGuid WarningId, const FGuLiGroundWarningParams& Params);
	UFUNCTION(BlueprintCallable, Category="Ground Warning") void RemoveWarning(FGuid WarningId);
	UFUNCTION(BlueprintPure, Category="Ground Warning") int32 GetActiveWarningCount() const { return Entries.Num(); }
	UFUNCTION(BlueprintPure, Category="Ground Warning") int32 GetActiveCircleCount() const;
private:
	struct FEntry { int32 Circle = INDEX_NONE; double ExpireServerSeconds = 0; };
	double ServerTime() const;
	UPROPERTY(Transient) TArray<FGuLiGroundWarningCircle> Circles;
	TMap<FGuid, FEntry> Entries;
};
