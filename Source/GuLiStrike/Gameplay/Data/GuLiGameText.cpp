#include "Gameplay/Data/GuLiGameText.h"
#include "Gameplay/Data/Generated/GuLiStrikeGameTextsTableRows.h"
#include "UObject/StrongObjectPtr.h"

const TCHAR* GuLiGameText::Text(const TCHAR* TextId)
{
    // Shared immutable asset, kept alive across world transitions. Retry after an editor import.
    static TStrongObjectPtr<UDataTable> Table;
    if (!Table.IsValid()) Table.Reset(LoadObject<UDataTable>(nullptr,
        TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeGameTexts_Texts.DT_GuLiStrikeGameTexts_Texts")));
    if (Table.IsValid() && Table->GetRowStruct()==FGuLiStrikeGameTextsTextsRow::StaticStruct())
        if (const auto* Row=Table->FindRow<FGuLiStrikeGameTextsTextsRow>(FName(TextId),TEXT("GameText"),false))
            return *Row->Content;
    // Missing configuration stays visible instead of silently falling back to hardcoded copy.
    static TMap<FName,FString> Missing;
    const FName Key(TextId);
    if (!Missing.Contains(Key))
    {
        UE_LOG(LogTemp,Error,TEXT("Missing game text: %s (GuLiStrikeGameTexts.xlsx / Texts)"),TextId);
        Missing.Add(Key,FString::Printf(TEXT("[%s]"),TextId));
    }
    return *Missing.FindChecked(Key);
}

FString GuLiGameText::Format(const TCHAR* TextId,const TArray<FString>& Values)
{
    FFormatOrderedArguments Arguments;
    for(const FString& Value:Values) Arguments.Add(FText::FromString(Value));
    return FText::Format(Get(TextId),Arguments).ToString();
}
