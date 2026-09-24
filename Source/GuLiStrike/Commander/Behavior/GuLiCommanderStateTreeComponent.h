#pragma once
#include "Components/StateTreeComponent.h"
#include "Commander/Orders/GuLiUnitTaskTypes.h"
#include "GuLiCommanderStateTreeComponent.generated.h"

/** Authority-only Actor runner, clocked by the existing task subsystem, never by frame tick. */
UCLASS()
class UGuLiCommanderStateTreeComponent final : public UStateTreeComponent
{
	GENERATED_BODY()
public:
	UGuLiCommanderStateTreeComponent();
	void Configure(FGuLiTaskUnitId InUnit, UStateTree& Tree);
	void AdvanceBehavior(float DeltaSeconds);
	FGuLiTaskUnitId GetUnit() const { return Unit; }
	virtual TSubclassOf<UStateTreeSchema> GetSchema() const override;
private:
	FGuLiTaskUnitId Unit;
	bool bStartAttempted = false;
};
