#include "Commander/UI/GuLiCommanderUITheme.h"
#include "Engine/Texture2D.h"
UTexture2D* UGuLiCommanderUITheme::FindIcon(FName Key) const
{
	const auto* Value = Icons.Find(Key); return Value ? Value->Get() : nullptr;
}
UTexture2D* UGuLiCommanderUITheme::FindPortrait(int32 UnitType) const
{
	const auto* Value = Portraits.Find(UnitType); return Value ? Value->Get() : FindIcon(TEXT("Unit"));
}
