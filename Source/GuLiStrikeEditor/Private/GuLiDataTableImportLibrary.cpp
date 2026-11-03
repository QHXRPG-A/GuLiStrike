#include "GuLiDataTableImportLibrary.h"
#include "Editor.h"
#include "Engine/DataTable.h"
#include "UObject/StrongObjectPtr.h"

TArray<FString> UGuLiDataTableImportLibrary::UpdateFromJson(UDataTable* Table, const FString& Json)
{
    if (!GEditor || GEditor->PlayWorld)
        return {TEXT("Stop PIE before importing configuration tables.")};
    if (!Table || !Table->GetRowStruct())
        return {TEXT("The destination table must have a native row schema.")};

    // The Blueprint FillDataTableFromJSONString helper invokes a factory, which
    // reconstructs the destination UObject and asserts if Python/UI holds it.
    // Parse independently first, then copy the validated rows into the same object.
    TStrongObjectPtr<UDataTable> Staging(NewObject<UDataTable>(GetTransientPackage()));
    Staging->RowStruct = Table->RowStruct;
    Staging->ImportKeyField = Table->ImportKeyField;
    TArray<FString> Problems = Staging->CreateTableFromJSONString(Json);
    if (!Problems.IsEmpty()) return Problems;

    Table->Modify();
    Problems = Table->CreateTableFromOtherTable(Staging.Get());
    if (Problems.IsEmpty()) Table->MarkPackageDirty();
    return Problems;
}
