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

namespace
{
	void SampleCards(TArray<FString>& Cards, int32 Count)
	{
		Count=FMath::Min(Count,Cards.Num());
		for (int32 I=0; I<Count; ++I) Cards.Swap(I,FMath::RandRange(I,Cards.Num()-1));
		Cards.SetNum(Count);
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
	if (!Table || Table->GetRowStruct()!=FGuLiStrikeRogueCardsCardsRow::StaticStruct() || !StringTable)
	{ Error=TEXT("Card table or StringTable is unavailable"); return false; }
	TMap<FString,FGuLiStrikeRogueCardsCardsRow> Validated;
	for (const FName Name : Table->GetRowNames())
	{
		const auto& R=*Table->FindRow<FGuLiStrikeRogueCardsCardsRow>(Name,TEXT("RogueCards"));
		UClass* Class=R.ImplementationClass.LoadSynchronous();
		const auto* Data=GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>();
		const bool bIdValid=R.Id.Len()==5 && R.Id[2]=='.' && R.Id.Left(2).IsNumeric() && R.Id.Right(2).IsNumeric() && R.Id.Right(2)!=TEXT("00");
		if (!bIdValid || Validated.Contains(R.Id) || R.Type!=1 || R.UnitTypeId<=0 || R.UnitTypeId>MAX_uint16
			|| !Data || !Data->FindSoldierDefinition(static_cast<uint16>(R.UnitTypeId))
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
FText UGuLiRogueCardSubsystem::GetCardText(const FString& Id, int32 Index)
{
	const auto* R=FindCard(Id); if (!R || !R->TextIds.IsValidIndex(Index)) return FText::GetEmpty();
	FText Result=GetText(R->TextIds[Index]);
	if (Index==1)
		if (UClass* Class=R->ImplementationClass.LoadSynchronous())
			if (const auto* Effect=Cast<UGuLiRogueCardEffect>(Class->GetDefaultObject()))
				Result=Effect->FormatDescription(*R,Result);
	return Result;
}
bool UGuLiRogueCardSubsystem::IsEligible(const FGuLiStrikeRogueCardsCardsRow& Card, EGuLiTeam Team, FString& Error)
{
	TMap<FString,int32> Counts;
	int64 PendingMissileBonus = 0;
	if (const auto* Existing=Acquisitions.Find(static_cast<uint32>(Team))) Counts=*Existing;
	// A source already accepted earlier in this fixed step reserves its acquisition
	// until EndFixedStep. Concurrent commanders cannot bypass once-only/exclusion rules.
	for (const auto& Pair : Offers)
		if (Pair.Value.Team==Team && Pair.Value.bApplied && !Pair.Value.bFinished)
		{
			++Counts.FindOrAdd(Pair.Value.Selected);
			const auto* PendingCard=FindCard(Pair.Value.Selected);
			if (PendingCard && PendingCard->UnitTypeId==2
				&& PendingCard->ImplementationClass.Get()==UGuLiRogueCardMissileCountEffect::StaticClass())
				PendingMissileBonus+=PendingCard->BonusCount;
		}
	const int32 Count=Counts.FindRef(Card.Id);
	if (Count==MAX_int32 || (Card.MaxAcquisitions>0 && Count>=Card.MaxAcquisitions))
	{ Error=GetText(TEXT("UI.RogueCards.AcquisitionLimit")).ToString(); return false; }
	for (const FString& Id : Card.RequiredCardIds)
		if (Counts.FindRef(Id)<1)
		{ Error=GetText(TEXT("UI.RogueCards.RequirementNotMet")).ToString(); return false; }
	for (const auto& Pair : Counts)
		if (Pair.Value>0)
		{
			const auto* Other=Catalog.Find(Pair.Key);
			if (Card.ExcludedCardIds.Contains(Pair.Key) || (Other && Other->ExcludedCardIds.Contains(Card.Id)))
			{ Error=GetText(TEXT("UI.RogueCards.Excluded")).ToString(); return false; }
		}
	if (Card.UnitTypeId==2 && Card.ImplementationClass.Get()==UGuLiRogueCardMissileCountEffect::StaticClass())
	{
		const auto* Army=GetWorld()->GetSubsystem<UGuLiArmySkillSubsystem>();
		const auto* Weapon=Army ? Army->FindResolvedSkill(Team,2,TEXT("MissileLauncher")) : nullptr;
		const auto* Skills=GetDefault<UGuLiCommanderSkillSettings>()->Catalog.LoadSynchronous();
		const auto* Definition=Skills ? Skills->FindUnitSkill(2) : nullptr;
		const auto* Config=Definition ? Cast<UGuLiPointSkillConfiguration>(Definition->Configuration) : nullptr;
		// Acquisition limits handle ordinary cards; the resolved count also covers
		// other integer sources and the player-invoked guidance preparation cases.
		if (!Weapon || !Config || Config->MaxProjectilesPerActivation<1
			|| int64(Weapon->ProjectileCount)+PendingMissileBonus+Card.BonusCount>Config->MaxProjectilesPerActivation)
		{ Error=GetText(TEXT("UI.RogueCards.AcquisitionLimit")).ToString(); return false; }
	}
	Error.Reset(); return true;
}
bool UGuLiRogueCardSubsystem::ValidateOwner(UGuLiCommanderNetSyncComponent& Channel, AGuLiBattlePlayerState*& Owner, FString& Error) const
{
	const auto* PC=Cast<APlayerController>(Channel.GetOwner());
	Owner=PC ? PC->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
	const auto* State=GetWorld()->GetGameState<AGuLiBattleGameState>();
	if (GetWorld()->GetNetMode()==NM_Client || !PC || !PC->HasAuthority() || !Owner || !State
		|| !State->GetMatchEpoch() || !Owner->IsCommander() || !Owner->IsBattleReady() || !Owner->IsSoldierStreamReady())
	{ Error=TEXT("Commander is not ready for this match"); return false; }
	for (const auto& Slot : State->GetRoleSlots())
		if (Slot.SlotIndex==Owner->GetBattleSlotIndex() && Slot.bOccupied && Slot.bBattleReady && Slot.bSyncReady
			&& Slot.Role==EGuLiCommanderRole::Commander && Slot.Team==Owner->GetTeam()
			&& Slot.PlayerGuid.IsValid() && Slot.PlayerGuid==Owner->GetPlayerGuid()) return true;
	Error=TEXT("Commander no longer owns the team seat"); return false;
}
void UGuLiRogueCardSubsystem::SynchronizeEpoch()
{
	const auto* State=GetWorld()->GetGameState<AGuLiBattleGameState>();
	const uint32 Current=State ? State->GetMatchEpoch() : 0;
	if (Current!=Epoch) { Offers.Reset(); Acquisitions.Reset(); Epoch=Current; }
}
void UGuLiRogueCardSubsystem::RequestOffer(UGuLiCommanderNetSyncComponent& Channel, FGuid Request)
{
	SynchronizeEpoch(); FString Error; AGuLiBattlePlayerState* Owner=nullptr;
	if (!Request.IsValid() || !ValidateOwner(Channel,Owner,Error) || !LoadCatalog(Error))
	{ Channel.ClientRogueCardOffer(Request,{},Epoch,{},Error); return; }
	if (auto* Old=Offers.Find(&Channel); Old && !Old->bFinished && Old->Owner==Owner && Old->Team==Owner->GetTeam())
	{
		Channel.ClientRogueCardOffer(Request,Old->Session,Epoch,Old->Candidates,TEXT("")); return;
	}
	FOffer Offer; Offer.Request=Request; Offer.Session=FGuid::NewGuid(); Offer.Owner=Owner; Offer.Team=Owner->GetTeam();
	for (const auto& Pair : Catalog)
		if (IsEligible(Pair.Value,Offer.Team,Error)) Offer.Candidates.Add(Pair.Key);
	// Partial Fisher-Yates: every eligible card has the same probability and no duplicates.
	SampleCards(Offer.Candidates,3);
	Offer.bFinished=Offer.Candidates.IsEmpty(); // No selection exists to preserve; retry eligibility on the next open.
	Offers.Add(&Channel,Offer);
	Channel.ClientRogueCardOffer(Request,Offer.Session,Epoch,Offer.Candidates,TEXT(""));
}
void UGuLiRogueCardSubsystem::RerollOffer(UGuLiCommanderNetSyncComponent& Channel, FGuid Request, FGuid Session)
{
	SynchronizeEpoch(); FString Error; AGuLiBattlePlayerState* Owner=nullptr;
	FOffer* Current=Offers.Find(&Channel);
	if (!Request.IsValid() || !Session.IsValid() || !ValidateOwner(Channel,Owner,Error) || !LoadCatalog(Error)
		|| !Current || Current->Owner!=Owner || Current->Team!=Owner->GetTeam()
		|| Current->bSubmitted || Current->bApplied || Current->bFinished)
	{ Channel.ClientRogueCardOffer(Request,{},Epoch,{},TEXT("UI.RogueCards.RerollUnavailable")); return; }
	// Replaying the same request returns the same replacement; it cannot consume another draw.
	if (Current->Request==Request && Current->RerolledFrom==Session)
	{ Channel.ClientRogueCardOffer(Request,Current->Session,Epoch,Current->Candidates,TEXT("")); return; }
	if (Current->Session!=Session)
	{ Channel.ClientRogueCardOffer(Request,{},Epoch,{},TEXT("UI.RogueCards.RerollUnavailable")); return; }
	TArray<FString> Alternatives, Retained;
	for (const auto& Pair : Catalog)
		if (IsEligible(Pair.Value,Current->Team,Error))
			(Current->Candidates.Contains(Pair.Key) ? Retained : Alternatives).Add(Pair.Key);
	if (Alternatives.IsEmpty() && Retained.Num()==Current->Candidates.Num())
	{ Channel.ClientRogueCardOffer(Request,Current->Session,Epoch,{},TEXT("UI.RogueCards.NoAlternatives")); return; }
	// Replace as many cards as the eligible pool allows, then fill remaining slots without duplicates.
	SampleCards(Alternatives,3); SampleCards(Retained,3-Alternatives.Num());
	Alternatives.Append(Retained); SampleCards(Alternatives,3);
	FOffer Replacement;
	Replacement.Request=Request; Replacement.Session=FGuid::NewGuid(); Replacement.RerolledFrom=Session;
	Replacement.Owner=Owner; Replacement.Team=Current->Team; Replacement.Candidates=MoveTemp(Alternatives);
	Replacement.bFinished=Replacement.Candidates.IsEmpty();
	*Current=MoveTemp(Replacement); // Invalidates the old session before any late confirmation can settle.
	Channel.ClientRogueCardOffer(Request,Current->Session,Epoch,Current->Candidates,TEXT(""));
}
void UGuLiRogueCardSubsystem::Confirm(UGuLiCommanderNetSyncComponent& Channel, FGuid Session, const FString& CardId)
{
	SynchronizeEpoch(); FString Error; AGuLiBattlePlayerState* Owner=nullptr;
	FOffer* Offer=Offers.Find(&Channel);
	if (!ValidateOwner(Channel,Owner,Error) || !Offer || Offer->Session!=Session || Offer->Owner!=Owner
		|| Offer->Team!=Owner->GetTeam() || CardId.Len()>32 || !Offer->Candidates.Contains(CardId))
	{ Channel.ClientRogueCardResult(Session,false,Error.IsEmpty()?TEXT("Invalid or expired card session"):Error); return; }
	if (Offer->bFinished)
	{ Channel.ClientRogueCardResult(Session,Offer->bApplied && Offer->Selected==CardId,Offer->Error); return; }
	if (Offer->bSubmitted) return;
	const auto* Card=FindCard(CardId);
	if (!Card || !IsEligible(*Card,Offer->Team,Error))
	{
		Offer->bFinished=true; Offer->Error=Error.IsEmpty()?TEXT("Card configuration changed"):Error;
		Channel.ClientRogueCardResult(Session,false,Offer->Error); return;
	}
	Offer->Selected=CardId; Offer->bSubmitted=true;
}
void UGuLiRogueCardSubsystem::Cancel(UGuLiCommanderNetSyncComponent& Channel, FGuid Session)
{
	// Closing the presentation leaves an unconfirmed offer intact. Reopening is
	// never a reroll; epoch/owner changes invalidate the stored offer instead.
	SynchronizeEpoch();
}
void UGuLiRogueCardSubsystem::BeginFixedStep()
{
	SynchronizeEpoch();
	for (auto It=Offers.CreateIterator(); It; ++It)
	{
		auto* Channel=It.Key().Get(); auto& Offer=It.Value();
		if (!Channel) { It.RemoveCurrent(); continue; }
		if (!Offer.bSubmitted || Offer.bFinished || Offer.bApplied) continue;
		AGuLiBattlePlayerState* Owner=nullptr;
		const auto* Card=FindCard(Offer.Selected);
		if (ValidateOwner(*Channel,Owner,Offer.Error) && Offer.Owner==Owner && Offer.Team==Owner->GetTeam() && Card
			&& IsEligible(*Card,Offer.Team,Offer.Error))
		{
			const auto* Effect=Cast<UGuLiRogueCardEffect>(Card->ImplementationClass.LoadSynchronous()->GetDefaultObject());
			Offer.bApplied=Effect && Effect->Apply(*Owner,*Card,Offer.Session,Offer.Error);
		}
		if (!Offer.bApplied)
		{
			Offer.bFinished=true; if (Offer.Error.IsEmpty()) Offer.Error=TEXT("Card configuration changed");
			Channel->ClientRogueCardResult(Offer.Session,false,Offer.Error);
		}
	}
}
void UGuLiRogueCardSubsystem::EndFixedStep()
{
	for (auto& Pair : Offers)
	{
		auto& Offer=Pair.Value;
		if (!Offer.bApplied || Offer.bFinished) continue;
		Offer.bFinished=true; ++Acquisitions.FindOrAdd(static_cast<uint32>(Offer.Team)).FindOrAdd(Offer.Selected);
		Offer.CommittedTime=GetWorld()->GetTimeSeconds();
		if (const auto* Card=FindCard(Offer.Selected))
			if (const auto* Authority=GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>())
			{
				TArray<FGuLiSoldierStateItem> States; Authority->BuildSoldierStateSnapshot(States);
				for (const auto& State : States)
					if (State.IsAlive() && State.Team==Offer.Team && State.UnitTypeId==Card->UnitTypeId)
						Offer.UpgradeTargets.Add(State.SoldierId);
			}
		if (auto* Channel=Pair.Key.Get()) Channel->ClientRogueCardResult(Offer.Session,true,TEXT(""));
	}
}

void UGuLiRogueCardSubsystem::PresentationClosed(UGuLiCommanderNetSyncComponent& Channel,FGuid Session)
{
	SynchronizeEpoch(); FString Error; AGuLiBattlePlayerState* Owner=nullptr;
	auto* Offer=Offers.Find(&Channel);
	if (!Offer || Offer->Session!=Session || !Offer->bFinished || !Offer->bApplied || Offer->bUpgradePublished
		|| !ValidateOwner(Channel,Owner,Error) || Offer->Owner!=Owner || Offer->Team!=Owner->GetTeam()) return;
	Offer->bUpgradePublished=true;
	TArray<FGuLiSoldierId> Targets=MoveTemp(Offer->UpgradeTargets);
	if (GetWorld()->GetTimeSeconds()-Offer->CommittedTime>10.f || Targets.IsEmpty()) return;
	const auto* Card=FindCard(Offer->Selected);
	FGuLiRogueUpgradeCue Cue;
	if (!Card || Card->UpgradeVfx.IsNull() || !FMath::IsFinite(Card->UpgradeVfxScale) || Card->UpgradeVfxScale<=0
		|| !GuLiRogueUpgrade::ParseColor(Card->UpgradeVfxColor,Cue.Color))
	{ UE_LOG(LogTemp,Warning,TEXT("Rogue upgrade visual skipped: invalid VFX configuration for %s"),*Offer->Selected); return; }
	Cue.Session=Session; Cue.MatchEpoch=Epoch; Cue.Team=Offer->Team; Cue.UnitTypeId=static_cast<uint16>(Card->UnitTypeId);
	Cue.System=TSoftObjectPtr<UNiagaraSystem>(Card->UpgradeVfx.ToSoftObjectPath()); Cue.Scale=Card->UpgradeVfxScale;
	Cue.Soldiers=MoveTemp(Targets);
	UGuLiCombatEffectReplicationComponent::PublishRogueUpgrade(GetWorld(),Cue);
}
