#pragma once
#include "CoreMinimal.h"

/** Read-only game copy from GuLiStrikeGameTexts.xlsx; never owns gameplay state.
 * Returned characters live with the loaded DataTable. No runtime configuration swaps.
 */
namespace GuLiGameText
{
    GULISTRIKE_API const TCHAR* Text(const TCHAR* TextId);
    inline FText Get(const TCHAR* TextId) { return FText::FromString(Text(TextId)); }
    GULISTRIKE_API FString Format(const TCHAR* TextId, const TArray<FString>& Values);
}
