#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GuLiVfxBindingComponent.generated.h"

USTRUCT(BlueprintType)
struct FGuLiNiagaraVfxBinding
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VFX") FName ComponentName;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VFX") int32 VfxId = 0;
};

/** Resolves existing Blueprint Niagara components without replacing their graph/lifecycle ownership. */
UCLASS(ClassGroup=(GuLiStrike), meta=(BlueprintSpawnableComponent))
class GULISTRIKE_API UGuLiVfxBindingComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VFX") TArray<FGuLiNiagaraVfxBinding> Bindings;
	virtual void BeginPlay() override;
};
