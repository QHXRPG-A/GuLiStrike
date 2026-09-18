#pragma once
#include "CoreMinimal.h"
class UMeshComponent;

namespace GuLiObjectScale
{
	inline constexpr float GameplayScale = 0.2f;
	/** Scale world-space outline extrusion at the render entry, never the source material. */
	GULISTRIKE_API void ApplyOutlineScale(UMeshComponent* Component);
}
