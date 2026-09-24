#pragma once
#include "MassEntityQuery.h"
#include "MassProcessor.h"
#include "MassObserverProcessor.h"
#include "MassStateTreeTypes.h"
#include "GuLiCommanderMassStateTreeProcessor.generated.h"

class UStateTree;

/** Separate fragment prevents the engine's frame/signal runner from ticking these same instances. */
USTRUCT()
struct FGuLiCommanderStateTreeFragment : public FMassFragment
{
	GENERATED_BODY()
	// Non-owning: the immutable UnitData catalog and Mass StateTree subsystem retain the asset.
	// Keep the per-entity fragment trivially copyable for archetype migration.
	UStateTree* Tree = nullptr;
	FMassStateTreeInstanceHandle Instance;
	bool bStartAttempted = false;
};

/** Uses native Mass StateTree context/storage, synchronously inside the existing 10 Hz task step. */
UCLASS()
class UGuLiCommanderMassStateTreeProcessor final : public UMassProcessor
{
	GENERATED_BODY()
public:
	UGuLiCommanderMassStateTreeProcessor();
protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
	FMassEntityQuery Query;
};

UCLASS()
class UGuLiCommanderMassStateTreeDestructor final : public UMassObserverProcessor
{
	GENERATED_BODY()
public:
	UGuLiCommanderMassStateTreeDestructor();
protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
	FMassEntityQuery Query;
};
