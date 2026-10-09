#pragma once
#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Tickable.h"
#include "GuLiSceneUISubsystem.generated.h"
class UGuLiSceneUIWidget;
class APlayerController;

/** Shared viewport layer for Commander, Ground and Ship seats. */
UCLASS()
class GULISTRIKE_API UGuLiSceneUISubsystem : public ULocalPlayerSubsystem, public FTickableGameObject
{
	GENERATED_BODY()
public:
	static UGuLiSceneUIWidget* ForController(APlayerController* Controller);
	UGuLiSceneUIWidget* GetOrCreate(APlayerController* Controller);
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual UWorld* GetTickableGameObjectWorld() const override { return GetWorld(); }
	virtual TStatId GetStatId() const override;
private:
	UPROPERTY(Transient) TObjectPtr<UGuLiSceneUIWidget> Widget;
	TWeakObjectPtr<APlayerController> BoundController;
};
