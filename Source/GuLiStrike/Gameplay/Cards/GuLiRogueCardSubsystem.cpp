#include "Gameplay/Cards/GuLiRogueCardSubsystem.h"
#include "Gameplay/Cards/GuLiRogueCardSettings.h"
#include "Gameplay/Cards/GuLiRogueCardEffect.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Gameplay/Skills/GuLiArmySkillSubsystem.h"
#include "Gameplay/CommanderSkills/GuLiPointSkill.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
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

namespace
{
    FGuid RequestForPlayer(const AGuLiBattlePlayerState& PS,EGuLiHybridArmyOperation Op,FGuid Event,FGuid Session={},FString Choice={})
    {
        if(PS.GetBattleRole()==EGuLiCommanderRole::Ground)return GuLiGroundRogue::Request(PS,Op,Event,Session,MoveTemp(Choice));
        return PS.IsCommander()?GuLiHybridArmy::Request(*PS.GetWorld(),PS.GetTeam(),Op,Event,Session,MoveTemp(Choice)):FGuid{};
    }
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
	TMap<FString,FGuLiStrikeRogueCardsCardsRow> Validated;
	for (const FName Name : Table->GetRowNames())
	{
		const auto& R=*Table->FindRow<FGuLiStrikeRogueCardsCardsRow>(Name,TEXT("RogueCards"));
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
		Validated.Add(R.Id,R);
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
int32 UGuLiRogueCardSubsystem::GetAcquisitionCount(FGuid Player,const FString& Id) const
{const auto* Counts=GroundAcquisitions.Find(Player);return Counts?Counts->FindRef(Id):0;}
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
    if (R.IsCommitted()) {HybridRequests.Remove(R.Event);return;}
    RejectHybridRequest(R.Event,FString::Printf(TEXT("Army transaction rejected (%u)"),uint8(R.Status)));
}
void UGuLiRogueCardSubsystem::HybridCommandFinished(const FGuLiHybridCommandResult& R)
{
    if (!R.Members.ContainsByPredicate([](const auto& M){return M.bAccepted;}))
        RejectHybridRequest(R.Event,TEXT("Army command was rejected before settlement"));
}
void UGuLiRogueCardSubsystem::RejectHybridRequest(FGuid Event,const FString& Error)
{
    FPendingHybridUI Pending;if (!HybridRequests.RemoveAndCopyValue(Event,Pending)) return;
    auto* Channel=Pending.Channel.Get();if (!Channel) return;
    if (Pending.Selection) Channel->ClientRogueCardResult(Pending.Session,false,Error);
    else Channel->ClientRogueCardOffer(Event,Pending.Session,Epoch,{},Error);
}
void UGuLiRogueCardSubsystem::Deinitialize()
{
    if (auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>())
    {H->OnTransactionCompleted.RemoveAll(this);H->OnCommandCompleted.Remove(HybridCommandReceiptHandle);H->OnOwnershipStateApplied.RemoveAll(this);}
    HybridCommandReceiptHandle.Reset();
    HybridRequests.Reset();HybridClosedSessions.Reset();Offers.Reset();Acquisitions.Reset();GroundAcquisitions.Reset();GroundRevisions.Reset();Super::Deinitialize();
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
