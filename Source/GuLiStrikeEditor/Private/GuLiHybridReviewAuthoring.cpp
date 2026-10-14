#include "GuLiHybridAuthoringLibrary.h"
#include "Development/GuLiHybridNetworkReviewActor.h"
#include "Network/GuLiHybridManifest.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"

namespace
{
TSharedRef<FJsonObject> ReadReviewEntity(UWorld* World)
{
    auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("schema"),TEXT("guli-hybrid-review-entity/v2"));
    J->SetBoolField(TEXT("success"),false);J->SetBoolField(TEXT("gameplay_executed"),false);
    J->SetStringField(TEXT("readback_source"),TEXT("loaded_editor_entity"));
    const FString Map=TEXT("/Game/Maps/LVL_CommanderMassPrototype");J->SetStringField(TEXT("map"),Map);
    if (!World || World->IsGameWorld() || World->GetOutermost()->GetName()!=Map)
    {J->SetStringField(TEXT("error"),TEXT("Original map is not open in an idle editor"));return J;}
    AGuLiHybridNetworkReviewActor* Entry=nullptr;int32 Count=0;
    for (TActorIterator<AActor> It(World);It;++It)
        if (It->ActorHasTag(TEXT("HybridNetworkReview")) || It->GetActorLabel()==TEXT("HybridNetworkReview"))
        {++Count;Entry=Cast<AGuLiHybridNetworkReviewActor>(*It);}
    J->SetNumberField(TEXT("identity_count"),Count);
    if (Count!=1 || !Entry || Entry->GetLevel()!=World->PersistentLevel)
    {J->SetStringField(TEXT("error"),TEXT("Expected exactly one native review entry in the persistent level"));return J;}
    J->SetStringField(TEXT("actor"),Entry->GetPathName());J->SetStringField(TEXT("class"),Entry->GetClass()->GetPathName());
    J->SetStringField(TEXT("label"),Entry->GetActorLabel());J->SetStringField(TEXT("folder"),Entry->GetFolderPath().ToString());
    J->SetBoolField(TEXT("tag_present"),Entry->ActorHasTag(TEXT("HybridNetworkReview")));
    J->SetStringField(TEXT("mode"),StaticEnum<EGuLiNetworkMode>()->GetNameStringByValue(int64(Entry->Mode)));
    J->SetStringField(TEXT("transport"),StaticEnum<EGuLiNetworkTransport>()->GetNameStringByValue(int64(Entry->Transport)));
    J->SetStringField(TEXT("team"),StaticEnum<EGuLiTeam>()->GetNameStringByValue(int64(Entry->Team)));
    J->SetStringField(TEXT("seat"),StaticEnum<EGuLiCommanderRole>()->GetNameStringByValue(int64(Entry->Seat)));
    J->SetStringField(TEXT("run_mode"),StaticEnum<EGuLiClientRunMode>()->GetNameStringByValue(int64(Entry->RunMode)));
    J->SetStringField(TEXT("room"),Entry->Room.ToString());J->SetStringField(TEXT("identity"),Entry->DevelopmentIdentity.ToString());
    J->SetStringField(TEXT("manifest"),Entry->Manifest.FilePath);J->SetStringField(TEXT("coordinator"),Entry->CoordinatorUrl);
    J->SetNumberField(TEXT("udp_port"),Entry->UdpPort);J->SetNumberField(TEXT("units"),Entry->Units);
    J->SetNumberField(TEXT("additional_flights"),Entry->AdditionalFlights);
    J->SetStringField(TEXT("transform"),Entry->GetActorTransform().ToString());
    J->SetStringField(TEXT("launch_arguments"),Entry->LaunchArguments);J->SetStringField(TEXT("preparation_error"),Entry->PreparationError);
    FString ExpectedArguments,ConfigurationError;
    const bool CurrentConfiguration=Entry->BuildLaunchArguments(ExpectedArguments,ConfigurationError) && ExpectedArguments==Entry->LaunchArguments;
    if (!CurrentConfiguration && ConfigurationError.IsEmpty()) ConfigurationError=TEXT("Stored launch arguments differ from the current entity configuration");
    J->SetBoolField(TEXT("arguments_match_current_configuration"),CurrentConfiguration);J->SetStringField(TEXT("configuration_error"),ConfigurationError);
    TArray<TSharedPtr<FJsonValue>> Bindings;bool AllBindings=true;
    for (const FName Name:{FName(TEXT("PrepareLaunchArguments")),FName(TEXT("SelectObjects")),FName(TEXT("GetOwnershipStatus")),FName(TEXT("LeaveRoom"))})
    {
        auto B=MakeShared<FJsonObject>();const auto* F=Entry->FindFunction(Name);B->SetStringField(TEXT("name"),Name.ToString());
        B->SetStringField(TEXT("target"),F?F->GetPathName():TEXT(""));
        const bool Valid=F && F->HasAllFunctionFlags(FUNC_Native|FUNC_BlueprintCallable);
        B->SetBoolField(TEXT("native_callable"),Valid);AllBindings&=Valid;Bindings.Add(MakeShared<FJsonValueObject>(B));
    }
    J->SetArrayField(TEXT("bindings"),Bindings);J->SetBoolField(TEXT("bindings_present"),AllBindings);
    const bool Dirty=World->GetOutermost()->IsDirty() || Entry->GetPackage()->IsDirty();J->SetBoolField(TEXT("package_dirty"),Dirty);
    const FString MapFile=FPackageName::LongPackageNameToFilename(Map,FPackageName::GetMapPackageExtension());
    const bool OnDisk=IFileManager::Get().FileSize(*MapFile)>0;
    J->SetStringField(TEXT("map_file"),MapFile);J->SetBoolField(TEXT("map_file_exists"),OnDisk);
    J->SetStringField(TEXT("map_timestamp_utc"),IFileManager::Get().GetTimeStamp(*MapFile).ToIso8601());
    const bool Success=AllBindings && CurrentConfiguration && Entry->ActorHasTag(TEXT("HybridNetworkReview")) && !Dirty && OnDisk
        && Entry->Mode==EGuLiNetworkMode::Hybrid && Entry->PreparationError.IsEmpty() && !Entry->LaunchArguments.IsEmpty();
    J->SetBoolField(TEXT("success"),Success);J->SetStringField(TEXT("error"),Success?TEXT(""):TEXT("Entry properties, bindings or saved package state are incomplete"));
    return J;
}
}

FString UGuLiHybridAuthoringLibrary::ReadHybridReviewEntry()
{
    auto J=ReadReviewEntity(GEditor && !GEditor->PlayWorld?GEditor->GetEditorWorldContext().World():nullptr);
    FString S;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&S));return S;
}

FString UGuLiHybridAuthoringLibrary::SaveHybridReviewEntry(const FString& ManifestFile,const FString& RoomId,const FString& Identity,int32 Population)
{
    auto J=MakeShared<FJsonObject>();J->SetBoolField(TEXT("saved"),false);J->SetBoolField(TEXT("modified"),false);J->SetBoolField(TEXT("gameplay_executed"),false);
    const auto Finish=[&](const FString& Error)
    {J->SetBoolField(TEXT("success"),Error.IsEmpty());J->SetStringField(TEXT("error"),Error);FString S;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&S));return S;};
    const FString Map=TEXT("/Game/Maps/LVL_CommanderMassPrototype");J->SetStringField(TEXT("map"),Map);
    auto* W=GEditor && !GEditor->PlayWorld?GEditor->GetEditorWorldContext().World():nullptr;
    if (!W || W->IsGameWorld() || W->GetOutermost()->GetName()!=Map) return Finish(TEXT("Open the original prototype map in an idle editor first"));
    FGuid Room,Player;
    if (!FGuid::Parse(RoomId,Room) || !Room.IsValid() || !FGuid::Parse(Identity,Player) || !Player.IsValid() || (Population!=200 && Population!=600))
        return Finish(TEXT("Room, player identity and population are invalid"));
    // Validate configuration before creating/modifying a saved actor.
    FString ConfigHash,MapHash,ManifestMap,Why;FGuLiHybridManifestData ManifestData;
    if (!FGuLiHybridManifest::ValidateFile(ManifestFile,ConfigHash,MapHash,ManifestMap,Why)
        || !FGuLiHybridManifest::Read(ManifestFile,ManifestData,Why)) return Finish(Why);
    int32 ArmySize=0;for (const auto& O:ManifestData.Objects) if (O.Key.Kind==EGuLiHybridObjectKind::Soldier) ++ArmySize;
    if (ManifestMap!=Map || ArmySize!=Population) return Finish(TEXT("Manifest map or population differs from the review configuration"));
    AGuLiHybridNetworkReviewActor* Entry=nullptr;int32 Count=0;
    for (TActorIterator<AActor> It(W);It;++It) if (It->ActorHasTag(TEXT("HybridNetworkReview")) || It->GetActorLabel()==TEXT("HybridNetworkReview"))
    {++Count;Entry=Cast<AGuLiHybridNetworkReviewActor>(*It);}
    if (Count>1 || (Count==1 && !Entry)) return Finish(TEXT("Existing review identity is ambiguous or belongs to another actor class; inspect before migrating it"));
    if (Entry && Entry->GetLevel()!=W->PersistentLevel) return Finish(TEXT("Review entry must belong to the original persistent level"));
    const FScopedTransaction Edit(NSLOCTEXT("GuLiHybridAuthoring","SaveReviewEntry","Configure Hybrid network review entry"));
    W->PersistentLevel->Modify();
    if (!Entry)
    {
        FActorSpawnParameters P;P.OverrideLevel=W->PersistentLevel;P.ObjectFlags|=RF_Transactional;
        Entry=W->SpawnActor<AGuLiHybridNetworkReviewActor>(FVector(0,0,200),FRotator::ZeroRotator,P);
    }
    if (!Entry) return Finish(TEXT("Could not create the saved review entry"));
    J->SetBoolField(TEXT("modified"),true);
    Entry->Modify();Entry->SetActorLabel(TEXT("HybridNetworkReview"));Entry->SetFolderPath(TEXT("Review/HybridNetwork"));
    Entry->Tags.AddUnique(TEXT("HybridNetworkReview"));Entry->Mode=EGuLiNetworkMode::Hybrid;
    Entry->Room=Room;Entry->DevelopmentIdentity=Player;Entry->Units=Population;Entry->AdditionalFlights=Population==600?500:0;
    Entry->Manifest.FilePath=ManifestFile;Entry->PrepareLaunchArguments();Entry->MarkPackageDirty();
    if (!Entry->PreparationError.IsEmpty()) return Finish(Entry->PreparationError);
    if (!FEditorFileUtils::SaveLevel(W->PersistentLevel)) return Finish(TEXT("Map save failed"));
    if (Entry->GetPackage()->IsDirty() && !UEditorLoadingAndSavingUtils::SavePackages({Entry->GetPackage()},true))
        return Finish(TEXT("Review actor package save failed"));
    J->SetBoolField(TEXT("saved"),true);
    auto Read=ReadReviewEntity(W);J->SetObjectField(TEXT("entity"),Read);
    if (!Read->GetBoolField(TEXT("success")) || Read->GetStringField(TEXT("room"))!=Room.ToString()
        || Read->GetStringField(TEXT("identity"))!=Player.ToString() || Read->GetNumberField(TEXT("units"))!=Population
        || Read->GetNumberField(TEXT("additional_flights"))!=(Population==600?500:0) || Read->GetStringField(TEXT("manifest"))!=ManifestFile)
        return Finish(TEXT("Saved review entity readback differs from requested properties or bindings"));
    J->SetStringField(TEXT("config_hash"),ConfigHash);J->SetStringField(TEXT("map_hash"),MapHash);
    J->SetBoolField(TEXT("disk_reload_performed"),false);return Finish(TEXT(""));
}
