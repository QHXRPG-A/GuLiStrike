#include "Gameplay/Cards/GuLiRogueUpgradeTypes.h"

bool GuLiRogueUpgrade::ParseColor(const FString& Text,FLinearColor& OutColor)
{
	FLinearColor Value;
	if (!Value.InitFromString(Text) || !FMath::IsFinite(Value.R) || !FMath::IsFinite(Value.G)
		|| !FMath::IsFinite(Value.B) || !FMath::IsFinite(Value.A) || Value.R<0 || Value.G<0 || Value.B<0
		|| Value.A<0 || Value.A>1) return false;
	OutColor=Value; return true;
}
