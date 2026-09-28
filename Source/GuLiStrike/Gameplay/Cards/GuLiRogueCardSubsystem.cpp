#include "Gameplay/Cards/GuLiRogueCardSubsystem.h"
#include "Gameplay/Cards/GuLiRogueCardSettings.h"
#include "Gameplay/Cards/GuLiRogueCardEffect.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Gameplay/Skills/GuLiArmySkillSubsystem.h"
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
			|| Class->HasAnyClassFlags(CLASS_Abstract) || !FMath::IsFinite(R.BonusPercent) || R.BonusPercent<=0
			|| !Cast<UMaterialInterface>(R.FrontMaterial.LoadSynchronous()))
		{ Error=TEXT("Invalid card configuration: ")+Name.ToString(); return false; }
		for (const FString& Key : R.TextIds)
			if (!StringTable->GetStringTable()->FindEntry(Key).IsValid())
			{ Error=TEXT("Missing card text: ")+Key; return false; }
		Validated.Add(R.Id,R);
	}
	if (Settings->Candidates.Num()!=3) { Error=TEXT("Exactly three candidates are required"); return false; }
	TSet<FString> Unique;
	for (const FString& Id : Settings->Candidates)
	{
		if (!Validated.Contains(Id) || Unique.Contains(Id)) { Error=TEXT("Invalid candidate list"); return false; }
		Unique.Add(Id);
	}
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
	if (Index==1) { FNumberFormattingOptions Options; Options.SetMaximumFractionalDigits(2);
		Result=FText::Format(Result,FText::AsNumber(R->BonusPercent*100.f,&Options)); }
	return Result;
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
	Offer.Candidates=GetDefault<UGuLiRogueCardSettings>()->Candidates;
	Offers.Add(&Channel,Offer);
	Channel.ClientRogueCardOffer(Request,Offer.Session,Epoch,Offer.Candidates,TEXT(""));
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
	Offer->Selected=CardId; Offer->bSubmitted=true;
}
void UGuLiRogueCardSubsystem::Cancel(UGuLiCommanderNetSyncComponent& Channel, FGuid Session)
{
	if (const auto* Offer=Offers.Find(&Channel); Offer && Offer->Session==Session && !Offer->bSubmitted) Offers.Remove(&Channel);
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
		if (ValidateOwner(*Channel,Owner,Offer.Error) && Offer.Owner==Owner && Offer.Team==Owner->GetTeam() && Card)
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
