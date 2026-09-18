#pragma once

#include "CoreMinimal.h"

class UWorld;
class AActor;

/** Synchronous gift-spawn transaction. Suppresses collision for the building and spawned children. */
class FGuLiGiftBuildingClearance
{
public:
	explicit FGuLiGiftBuildingClearance(UWorld& World);
	~FGuLiGiftBuildingClearance();
	bool Commit(AActor& Building, const AActor& SupportingActor, FString& OutFailure);
private:
	struct FState;
	TUniquePtr<FState> State;
};
