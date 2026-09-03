#pragma once

#include "CoreMinimal.h"

/** Package serialization history for GuLiFlightNavigation data assets. */
struct GULIFLIGHTNAVIGATIONRUNTIME_API FGuLiFlightNavigationCustomVersion
{
	enum Type
	{
		BeforeCustomVersionWasAdded = 0,
		ExplicitBulkDataPayload,

		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	static const FGuid GUID;

private:
	FGuLiFlightNavigationCustomVersion() = delete;
};
