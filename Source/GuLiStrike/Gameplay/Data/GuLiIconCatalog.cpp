#include "Gameplay/Data/GuLiIconCatalog.h"
#include "Gameplay/Data/Generated/GuLiStrikeIconsTableRows.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"

bool UGuLiIconCatalog::Prepare(const TSet<int32>& Ids, const UWorld* World)
{
    if(IsRunningDedicatedServer() || (World && World->GetNetMode()==NM_DedicatedServer))return false;
    if(!Table)Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeIcons_Icons.DT_GuLiStrikeIcons_Icons"));
    if(!Table || Table->GetRowStruct()!=FGuLiStrikeIconsIconsRow::StaticStruct())
    {UE_LOG(LogTemp,Error,TEXT("Icon catalog missing or schema mismatch"));return false;}
    TMap<int32,const FGuLiStrikeIconsIconsRow*> Rows;
    Table->ForeachRow<FGuLiStrikeIconsIconsRow>(TEXT("Prepare icons"),[&](FName,const auto& Row){Rows.Add(Row.Id,&Row);});
    bool Valid=true;
    for(int32 Id:Ids)
    {
        if(Id<=0 || Textures.Contains(Id))continue;
        const auto* Row=Rows.FindRef(Id);
        UTexture2D* Texture=Row?Cast<UTexture2D>(Row->ResourcePath.LoadSynchronous()):nullptr;
        if(!Texture){UE_LOG(LogTemp,Error,TEXT("Icon %d is missing or is not a Texture2D"),Id);Valid=false;continue;}
        Textures.Add(Id,Texture);
    }
    return Valid;
}
UTexture2D* UGuLiIconCatalog::Find(int32 Id) const {return Textures.FindRef(Id);}
void UGuLiIconCatalog::Release(){Textures.Reset();Table=nullptr;}
