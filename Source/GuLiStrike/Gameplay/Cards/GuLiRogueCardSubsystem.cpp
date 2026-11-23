#include "Gameplay/Cards/GuLiRogueCardSubsystem.h"
#include "Gameplay/Cards/GuLiRogueCardSettings.h"
#include "Gameplay/Cards/GuLiRogueCardEffect.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Gameplay/Skills/GuLiArmySkillSubsystem.h"
#include "Gameplay/CommanderSkills/GuLiPointSkill.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "GameFramework/PlayerController.h"
#include "Internationalization/StringTable.h"
#include "Internationalization/StringTableCore.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "Gameplay/Cards/GuLiRogueUpgradeTypes.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectReplicationComponent.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Gameplay/Skills/GuLiHybridArmy.h"
#include "Network/GuLiNetworkSettings.h"
#include "Network/GuLiHybridManifest.h"
#include "Gameplay/GroundMech/Rogue/GuLiGroundRogueRuntime.h"
#include "GuLiStrike.h"
#include "HAL/IConsoleManager.h"

namespace
{
    FGuid RequestForPlayer(const AGuLiBattlePlayerState& PS,EGuLiHybridArmyOperation Op,FGuid Event,FGuid Session={},FString Choice={},uint32 TargetLevel=0)
    {
        if(PS.IsWushuang() && PS.GetActiveControlRole()!=EGuLiCommanderRole::Commander)return GuLiGroundRogue::Request(PS,Op,Event,Session,MoveTemp(Choice),TargetLevel);
        return PS.IsCommander()?GuLiHybridArmy::Request(*PS.GetWorld(),PS.GetTeam(),Op,Event,Session,MoveTemp(Choice),0,0,NAME_None,TargetLevel):FGuid{};
    }
#if !UE_BUILD_SHIPPING
    FAutoConsoleCommandWithWorldAndArgs SetRogueCardLevelCommand(
        TEXT("gs.GM.Card.Set"),TEXT("Set a card for the active seat: gs.GM.Card.Set <card ID or title> <level|max>. Level 0 removes a card."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* World)
        {
            auto* Cards=World?World->GetSubsystem<UGuLiRogueCardSubsystem>():nullptr;
            if (Cards) Cards->SetCardLevelFromConsole(Args);
            else UE_LOG(LogGuLiStrike,Warning,TEXT("gs.GM.Card.Set: enter a running game first."));
        }));
#endif
	bool ValidateCardRelations(const TMap<FString,FGuLiStrikeRogueCardsCardsRow>& Cards, FString& Error)
	{
		TMap<FString,TSet<FString>> Excluded, Closures;
		for (const auto& Pair : Cards)
		{
			const auto& Card=Pair.Value;
			if (Card.MaxAcquisitions<0) { Error=TEXT("Negative MaxAcquisitions: ")+Pair.Key; return false; }
			for (const TArray<FString>* Refs : {&Card.RequiredCardIds,&Card.ExcludedCardIds})
			{
				TSet<FString> Seen;
				for (const FString& Id : *Refs)
				{
					if (Id==Pair.Key || !Cards.Contains(Id) || Seen.Contains(Id))
					{ Error=TEXT("Self, unknown or duplicate card reference: ")+Pair.Key+TEXT(" -> ")+Id; return false; }
					Seen.Add(Id);
					if(Cards.FindChecked(Id).Type!=Card.Type){Error=TEXT("Card relation crosses owner domains: ")+Pair.Key;return false;}
				}
			}
            if(!Card.RequiredCardLevels.IsEmpty() && Card.RequiredCardLevels.Num()!=Card.RequiredCardIds.Num())
            {Error=TEXT("Mismatched prerequisite levels: ")+Pair.Key;return false;}
            for(int32 I=0;I<Card.RequiredCardLevels.Num();++I)
            {
                int32 Level;const int32 Cap=Cards.FindChecked(Card.RequiredCardIds[I]).MaxAcquisitions;
                if(!LexTryParseString(Level,*Card.RequiredCardLevels[I]) || Level<1 || (Cap && Level>Cap))
                {Error=TEXT("Invalid prerequisite level: ")+Pair.Key;return false;}
            }
			for (const FString& Id : Card.ExcludedCardIds)
			{ Excluded.FindOrAdd(Pair.Key).Add(Id); Excluded.FindOrAdd(Id).Add(Pair.Key); }
		}
		TSet<FString> Visiting;
		TFunction<bool(const FString&)> Visit;
		Visit=[&](const FString& Id)
		{
			if (Closures.Contains(Id)) return true;
			if (Visiting.Contains(Id)) { Error=TEXT("Card dependency cycle: ")+Id; return false; }
			Visiting.Add(Id); TSet<FString> Closure; Closure.Add(Id);
			for (const FString& Required : Cards.FindChecked(Id).RequiredCardIds)
			{
				if (!Visit(Required)) return false;
				Closure.Append(Closures.FindChecked(Required));
			}
			Visiting.Remove(Id);
			for (const FString& Member : Closure)
				if (const auto* Conflicts=Excluded.Find(Member))
					for (const FString& Conflict : *Conflicts)
						if (Closure.Contains(Conflict))
						{ Error=TEXT("Conflicting prerequisites: ")+Id+TEXT(" requires ")+Member+TEXT(" and ")+Conflict; return false; }
			Closures.Add(Id,MoveTemp(Closure)); return true;
		};
		for (const auto& Pair : Cards) if (!Visit(Pair.Key)) return false;
		return true;
	}
}

bool UGuLiRogueCardSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{ const auto* W=Cast<UWorld>(Outer); return Super::ShouldCreateSubsystem(Outer) && W && W->IsGameWorld(); }
bool UGuLiRogueCardSubsystem::LoadCatalog(FString& Error)
{
	if (!Catalog.IsEmpty()) return true;
	const auto* Settings=GetDefault<UGuLiRogueCardSettings>();
	Table=Settings->Cards.LoadSynchronous(); StringTable=Settings->Texts.LoadSynchronous();
    GroundEffectTable=Settings->GroundRicochet.LoadSynchronous();
	if (!Table || Table->GetRowStruct()!=FGuLiStrikeRogueCardsCardsRow::StaticStruct() || !StringTable)
	{ Error=TEXT("Card table or StringTable is unavailable"); return false; }
    if(!LoadGroundCatalog(Error))return false;
	TMap<FString,FGuLiStrikeRogueCardsCardsRow> Validated;
	for (const FName Name : Table->GetRowNames())
	{
		const auto& R=*Table->FindRow<FGuLiStrikeRogueCardsCardsRow>(Name,TEXT("RogueCards"));
        if(R.Type==3 && !AvailableGroundCards.Contains(R.Id))continue;
		UClass* Class=R.ImplementationClass.LoadSynchronous();
		const auto* Data=GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>();
		// Card identity is data, not an encoded unit/category number. Match the
		// shared capability's bounded string contract for new content as well.
		const FTCHARToUTF8 EncodedId(*R.Id);
		const bool bIdValid=!R.Id.IsEmpty() && EncodedId.Length()<=192;
		const bool TargetValid=R.Type==1?(R.UnitTypeId>0&&R.UnitTypeId<=MAX_uint16&&Data&&Data->FindSoldierDefinition(static_cast<uint16>(R.UnitTypeId))):(R.Type==3&&R.UnitTypeId==0);
		if (!bIdValid || Validated.Contains(R.Id) || !TargetValid
			|| R.TextIds.Num()!=2 || !Class || !Class->IsChildOf(UGuLiRogueCardEffect::StaticClass())
			|| Class->HasAnyClassFlags(CLASS_Abstract)
			|| !Cast<UMaterialInterface>(R.FrontMaterial.LoadSynchronous()))
		{ Error=TEXT("Invalid card configuration: ")+Name.ToString(); return false; }
		if (!Class->GetDefaultObject<UGuLiRogueCardEffect>()->Validate(R,Error))
		{ Error=R.Id+TEXT(": ")+Error; return false; }
		for (const FString& Key : R.TextIds)
			if (!StringTable->GetStringTable()->FindEntry(Key).IsValid())
			{ Error=TEXT("Missing card text: ")+Key; return false; }
        auto& local=Validated.Add(R.Id,R);
        if(R.Type==3)local.ExcludedCardIds.RemoveAll([&](const FString& id){return !AvailableGroundCards.Contains(id);});
	}
	if (!ValidateCardRelations(Validated,Error)) return false;
	Catalog=MoveTemp(Validated); return true;
}
const FGuLiStrikeRogueCardsCardsRow* UGuLiRogueCardSubsystem::FindCard(const FString& Id)
{ FString Error; return LoadCatalog(Error) ? Catalog.Find(Id) : nullptr; }
FText UGuLiRogueCardSubsystem::GetText(const FString& Key)
{
	if (!StringTable) StringTable=GetDefault<UGuLiRogueCardSettings>()->Texts.LoadSynchronous();
	return StringTable ? FText::FromStringTable(StringTable->GetStringTableId(),Key) : FText::FromString(Key);
}
bool UGuLiRogueCardSubsystem::LoadGroundCatalog(FString& error)
{
    if(bGroundCatalogLoaded)return true;
    GuLiNet::GroundRogue::Catalog catalog;
    GuLiGroundRogue::FBarrageCard barrage;
    if(!GuLiGroundRogue::LoadCatalog(catalog,error,&barrage))return false;
    for(const auto& entry:catalog.Cards)AvailableGroundCards.Add(UTF8_TO_TCHAR(entry.first.c_str()));
    GroundBarrageId=MoveTemp(barrage.CardId);
    bGroundCatalogLoaded=true;
    return true;
}
const FString& UGuLiRogueCardSubsystem::GetBarrageCardId()
{
    FString error;
    LoadGroundCatalog(error);
    return GroundBarrageId;
}
int32 UGuLiRogueCardSubsystem::GetAcquisitionCount(FGuid Player,const FString& Id) const
{const auto* Counts=GroundAcquisitions.Find(Player);return Counts?Counts->FindRef(Id):0;}
#if !UE_BUILD_SHIPPING
void UGuLiRogueCardSubsystem::SetCardLevelFromConsole(const TArray<FString>& Args)
{
    if (Args.Num()!=2)
    {
        UE_LOG(LogGuLiStrike,Warning,TEXT("用法 gs.GM.Card.Set <卡牌ID或名称> <等级|max>，0 为移除。"));
        return;
    }
    auto* PC=Cast<AGuLiCommanderPlayerController>(GetWorld()->GetFirstPlayerController());
    auto* PS=PC?PC->GetPlayerState<AGuLiBattlePlayerState>():nullptr;
    auto* Channel=PC?PC->FindComponentByClass<UGuLiCommanderNetSyncComponent>():nullptr;
    const bool Ground=PS && PS->IsWushuang() && PS->GetActiveControlRole()==EGuLiCommanderRole::Ground;
    if (!PC || !PC->IsLocalController() || !PS || !PS->IsBattleReady() || !Channel
        || (!Ground && (!PS->IsCommanderControlActive() || !PC->CanIssueCommanderOrders())))
    {
        UE_LOG(LogGuLiStrike,Warning,TEXT("gs.GM.Card.Set: 请先进入游骑兵或指挥官席位。"));
        return;
    }
    const int32 CardType=Ground?3:1;
    const auto* Card=FindCard(Args[0]);
    if (!Card)
    {
        for (const auto& Entry:Catalog)
        {
            const auto& Row=Entry.Value;
            if (Row.Type!=CardType || Row.TextIds.IsEmpty() || GetText(Row.TextIds[0]).ToString()!=Args[0]) continue;
            if (Card) {UE_LOG(LogGuLiStrike,Warning,TEXT("gs.GM.Card.Set: 存在同名卡牌，请使用卡牌ID。"));return;}
            Card=&Row;
        }
    }
    if (!Card || Card->Type!=CardType)
    {
        UE_LOG(LogGuLiStrike,Warning,TEXT("gs.GM.Card.Set: 当前席位没有卡牌 %s。"),*Args[0]);
        return;
    }
    int64 Level=Card->MaxAcquisitions;
    if (Args[1].Equals(TEXT("max"),ESearchCase::IgnoreCase))
    {
        if (!Level) {UE_LOG(LogGuLiStrike,Warning,TEXT("gs.GM.Card.Set: %s 没有表内等级上限，请指定数字。"),*Card->Id);return;}
    }
    else
    {
        TCHAR* End=nullptr;
        Level=FCString::Strtoi64(*Args[1],&End,10);
        if (Args[1].IsEmpty() || Args[1][0]<'0' || Args[1][0]>'9' || *End || Level<0 || Level>MAX_int32)
        {UE_LOG(LogGuLiStrike,Warning,TEXT("gs.GM.Card.Set: 等级应为非负整数或 max。"));return;}
    }
    if (Card->MaxAcquisitions && Level>Card->MaxAcquisitions)
    {
        UE_LOG(LogGuLiStrike,Warning,TEXT("gs.GM.Card.Set: %s 最高 %d 级。"),*Card->Id,Card->MaxAcquisitions);
        return;
    }
    const FGuid Event=FGuid::NewGuid();
    if (WatchHybridRequest(Event,{},false,*Channel)!=EHybridWatch::Added)
    {UE_LOG(LogGuLiStrike,Warning,TEXT("gs.GM.Card.Set: 肉鸽请求队列暂不可用。"));return;}
    auto& Pending=HybridRequests.FindChecked(Event);
    Pending.GMCard=Card->Id;
    Pending.GMLevel=int32(Level);
    if (!RequestForPlayer(*PS,EGuLiHybridArmyOperation::SetCardLevel,Event,{},Card->Id,uint32(Level)).IsValid())
        RejectHybridRequest(Event,TEXT("当前席位的肉鸽权威尚未就绪"));
}
#endif
FText UGuLiRogueCardSubsystem::GetCardText(const FString& Id, int32 Index,FGuid Player,bool bPreviewAcquisition)
{
	const auto* R=FindCard(Id); if (!R || !R->TextIds.IsValidIndex(Index)) return FText::GetEmpty();
	FText Result=GetText(R->TextIds[Index]);
	if (Index==1)
		if (UClass* Class=R->ImplementationClass.LoadSynchronous())
			if (const auto* Effect=Cast<UGuLiRogueCardEffect>(Class->GetDefaultObject()))
            {
                if(!Player.IsValid())if(const auto* PC=GetWorld()->GetFirstPlayerController())if(const auto* PS=PC->GetPlayerState<AGuLiBattlePlayerState>())Player=PS->GetPlayerGuid();
                Result=Effect->FormatForAcquisition(*R,Result,FMath::Max(1,GetAcquisitionCount(Player,Id)+(bPreviewAcquisition?1:0)));
            }
	return Result;
}
void UGuLiRogueCardSubsystem::RequestOffer(UGuLiCommanderNetSyncComponent& Channel, FGuid Request)
{

        const auto* PC=Cast<APlayerController>(Channel.GetOwner());const auto* PS=PC?PC->GetPlayerState<AGuLiBattlePlayerState>():nullptr;
        const auto Watch=WatchHybridRequest(Request,{},false,Channel);
        if (Watch==EHybridWatch::Pending) return;
        if (Watch==EHybridWatch::Rejected)
        {Channel.ClientRogueCardOffer(Request,{},Epoch,{},TEXT("Army request queue is unavailable"));return;}
        if (!PS || !RequestForPlayer(*PS,EGuLiHybridArmyOperation::Offer,Request).IsValid())
        {
            HybridRequests.Remove(Request);
            Channel.ClientRogueCardOffer(Request,{},Epoch,{},TEXT("Army capability is not ready"));
        }
        return;

}
void UGuLiRogueCardSubsystem::RerollOffer(UGuLiCommanderNetSyncComponent& Channel, FGuid Request, FGuid Session)
{

        const auto* PC=Cast<APlayerController>(Channel.GetOwner());const auto* PS=PC?PC->GetPlayerState<AGuLiBattlePlayerState>():nullptr;
        const auto Watch=WatchHybridRequest(Request,Session,false,Channel);
        if (Watch==EHybridWatch::Pending) return;
        if (Watch==EHybridWatch::Rejected)
        {Channel.ClientRogueCardOffer(Request,Session,Epoch,{},TEXT("UI.RogueCards.RerollBusy"));return;}
        if (!PS || !RequestForPlayer(*PS,EGuLiHybridArmyOperation::Reroll,Request,Session).IsValid())
        {
            HybridRequests.Remove(Request);
            Channel.ClientRogueCardOffer(Request,Session,Epoch,{},TEXT("UI.RogueCards.RerollBusy"));
        }
        return;

}
void UGuLiRogueCardSubsystem::Confirm(UGuLiCommanderNetSyncComponent& Channel, FGuid Session, const FString& CardId)
{

        const auto* PC=Cast<APlayerController>(Channel.GetOwner());const auto* PS=PC?PC->GetPlayerState<AGuLiBattlePlayerState>():nullptr;
        const auto Event=FGuLiHybridManifest::MakeStableKey(TEXT("/Game/GuLiStrike/ArmyRequests"),EGuLiHybridObjectKind::BusinessWork,Session.ToString()+TEXT("/choose/")+CardId).StableId;
        const auto Watch=WatchHybridRequest(Event,Session,true,Channel);
        if (Watch==EHybridWatch::Pending) return;
        if (Watch==EHybridWatch::Rejected)
        {Channel.ClientRogueCardResult(Session,false,TEXT("Army request queue is unavailable"));return;}
        if (!PS || !RequestForPlayer(*PS,EGuLiHybridArmyOperation::Choose,Event,Session,CardId).IsValid())
        {
            HybridRequests.Remove(Event);
            Channel.ClientRogueCardResult(Session,false,TEXT("Army capability is not ready"));
        }
        return;

}

UGuLiRogueCardSubsystem::EHybridWatch UGuLiRogueCardSubsystem::WatchHybridRequest(FGuid Event,FGuid Session,bool Selection,UGuLiCommanderNetSyncComponent& Channel)
{
    auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    if (!H || !Event.IsValid()) return EHybridWatch::Rejected;
    for (auto It=HybridRequests.CreateIterator();It;++It) if (!It.Value().Channel.IsValid()) It.RemoveCurrent();
    if (const auto* Existing=HybridRequests.Find(Event))
        return Existing->Session==Session && Existing->Selection==Selection && Existing->Channel.Get()==&Channel
            ? EHybridWatch::Pending : EHybridWatch::Rejected;
    if (HybridRequests.Num()>=64) return EHybridWatch::Rejected;
    H->OnTransactionCompleted.AddUniqueDynamic(this,&ThisClass::HybridTransactionFinished);
    H->OnOwnershipStateApplied.RemoveAll(this);
    H->OnOwnershipStateApplied.AddWeakLambda(this,[this,H]()
    {
        TArray<FGuid> Events;HybridRequests.GetKeys(Events);
        for (const auto& Event:Events)
        {
            FGuLiHybridTransactionResult R;FGuLiHybridCommandResult Command;
            if (H->FindTransactionResult(Event,R)) HybridTransactionFinished(R);
            else if (H->FindCommandResult(Event,Command)) HybridCommandFinished(Command);
            // No local entry is not a rejection: the owner may have transferred this work.
        }
    });
    if (!HybridCommandReceiptHandle.IsValid()) HybridCommandReceiptHandle=H->OnCommandCompleted.AddUObject(this,&ThisClass::HybridCommandFinished);
    HybridRequests.Add(Event,{Session,Selection,&Channel});return EHybridWatch::Added;
}
void UGuLiRogueCardSubsystem::HybridTransactionFinished(const FGuLiHybridTransactionResult& R)
{
    if (R.Status==EGuLiHybridTransactionStatus::Paused || R.Status==EGuLiHybridTransactionStatus::Deferred) return;
    if (R.IsCommitted()) {CompleteHybridRequest(R.Event);return;}
    RejectHybridRequest(R.Event,FString::Printf(TEXT("Army transaction rejected (%u)"),uint8(R.Status)));
}
void UGuLiRogueCardSubsystem::HybridCommandFinished(const FGuLiHybridCommandResult& R)
{
    if (!R.Members.ContainsByPredicate([](const auto& M){return M.bAccepted;}))
        RejectHybridRequest(R.Event,TEXT("Army command was rejected before settlement"));
}
void UGuLiRogueCardSubsystem::CompleteHybridRequest(FGuid Event)
{
    FPendingHybridUI Pending;
    if (HybridRequests.RemoveAndCopyValue(Event,Pending) && !Pending.GMCard.IsEmpty())
        UE_LOG(LogGuLiStrike,Display,TEXT("gs.GM.Card.Set: %s = %d，肉鸽系统已确认。"),*Pending.GMCard,Pending.GMLevel);
}
FString UGuLiRogueCardSubsystem::ExplainCardLevelRejection(const FPendingHybridUI& Pending) const
{
    const auto* Channel=Pending.Channel.Get();
    const auto* PC=Channel?Cast<APlayerController>(Channel->GetOwner()):nullptr;
    const auto* PS=PC?PC->GetPlayerState<AGuLiBattlePlayerState>():nullptr;
    const auto* Target=Catalog.Find(Pending.GMCard);
    if (!PS || !Target) return {};
    const auto* Counts=Target->Type==3?GroundAcquisitions.Find(PS->GetPlayerGuid()):Acquisitions.Find(uint32(PS->GetTeam()));
    const auto Rank=[&](const FString& Id){return Id==Pending.GMCard?Pending.GMLevel:(Counts?Counts->FindRef(Id):0);};
    for (const auto& Entry:Catalog)
    {
        const auto& Card=Entry.Value;
        if (Card.Type!=Target->Type || !Rank(Card.Id)) continue;
        for (int32 I=0;I<Card.RequiredCardIds.Num();++I)
        {
            const int32 Needed=Card.RequiredCardLevels.IsValidIndex(I)?FCString::Atoi(*Card.RequiredCardLevels[I]):1;
            if (Rank(Card.RequiredCardIds[I])<Needed)
                return FString::Printf(TEXT("%s 需要 %s 至少 %d 级"),*Card.Id,*Card.RequiredCardIds[I],Needed);
        }
        for (const auto& Excluded:Card.ExcludedCardIds)
            if (Rank(Excluded)) return FString::Printf(TEXT("%s 与 %s 互斥"),*Card.Id,*Excluded);
    }
    return TEXT("请核对当前席位、武器数值上限和肉鸽系统状态");
}
void UGuLiRogueCardSubsystem::RejectHybridRequest(FGuid Event,const FString& Error)
{
    FPendingHybridUI Pending;if (!HybridRequests.RemoveAndCopyValue(Event,Pending)) return;
    if (!Pending.GMCard.IsEmpty())
    {
        UE_LOG(LogGuLiStrike,Warning,TEXT("gs.GM.Card.Set: %s = %d 被拒绝。%s (%s)"),
            *Pending.GMCard,Pending.GMLevel,*ExplainCardLevelRejection(Pending),*Error);
        return;
    }
    auto* Channel=Pending.Channel.Get();if (!Channel) return;
    if (Pending.Selection) Channel->ClientRogueCardResult(Pending.Session,false,Error);
    else Channel->ClientRogueCardOffer(Event,Pending.Session,Epoch,{},Error);
}
void UGuLiRogueCardSubsystem::Deinitialize()
{
    if (auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>())
    {H->OnTransactionCompleted.RemoveAll(this);H->OnCommandCompleted.Remove(HybridCommandReceiptHandle);H->OnOwnershipStateApplied.RemoveAll(this);}
    HybridCommandReceiptHandle.Reset();
    HybridRequests.Reset();HybridClosedSessions.Reset();Offers.Reset();Acquisitions.Reset();GroundAcquisitions.Reset();GroundRevisions.Reset();GroundOpenSessions.Reset();Super::Deinitialize();
}
void UGuLiRogueCardSubsystem::Cancel(UGuLiCommanderNetSyncComponent& Channel, FGuid Session)
{
	// Closing the presentation leaves an unconfirmed offer intact. Reopening is
	// never a reroll; epoch/owner changes invalidate the stored offer instead.

}
void UGuLiRogueCardSubsystem::PresentationClosed(UGuLiCommanderNetSyncComponent& Channel,FGuid Session)
{

        const auto* PC=Cast<APlayerController>(Channel.GetOwner());const auto* PS=PC?PC->GetPlayerState<AGuLiBattlePlayerState>():nullptr;
        const auto Event=FGuLiHybridManifest::MakeStableKey(TEXT("/Game/GuLiStrike/ArmyRequests"),EGuLiHybridObjectKind::BusinessWork,Session.ToString()+TEXT("/closed")).StableId;
        if (PS) RequestForPlayer(*PS,EGuLiHybridArmyOperation::ClosePresentation,Event,Session);
        return;

}
