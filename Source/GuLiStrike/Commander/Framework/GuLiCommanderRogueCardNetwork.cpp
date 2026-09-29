#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Gameplay/Cards/GuLiRogueCardSubsystem.h"
#include "Gameplay/Cards/GuLiRogueCardPresentation.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

void UGuLiCommanderNetSyncComponent::ServerRequestRogueCards_Implementation(FGuid Request)
{
	if (!ConsumeCommandRateLimit()) { ClientRogueCardOffer(Request,{},0,{},TEXT("Too many requests")); return; }
	GetWorld()->GetSubsystem<UGuLiRogueCardSubsystem>()->RequestOffer(*this,Request);
}
void UGuLiCommanderNetSyncComponent::ServerConfirmRogueCard_Implementation(FGuid Session, const FString& CardId)
{ GetWorld()->GetSubsystem<UGuLiRogueCardSubsystem>()->Confirm(*this,Session,CardId); }
void UGuLiCommanderNetSyncComponent::ServerRerollRogueCards_Implementation(FGuid Request, FGuid Session)
{
	if (!ConsumeCommandRateLimit())
	{ ClientRogueCardOffer(Request,Session,0,{},TEXT("UI.RogueCards.RerollBusy")); return; }
	GetWorld()->GetSubsystem<UGuLiRogueCardSubsystem>()->RerollOffer(*this,Request,Session);
}
void UGuLiCommanderNetSyncComponent::ServerCancelRogueCards_Implementation(FGuid Session)
{ GetWorld()->GetSubsystem<UGuLiRogueCardSubsystem>()->Cancel(*this,Session); }
void UGuLiCommanderNetSyncComponent::ServerRogueCardPresentationClosed_Implementation(FGuid Session)
{ GetWorld()->GetSubsystem<UGuLiRogueCardSubsystem>()->PresentationClosed(*this,Session); }
void UGuLiCommanderNetSyncComponent::ClientRogueCardOffer_Implementation(FGuid Request, FGuid Session, uint32 Epoch, const TArray<FString>& Cards, const FString& Error)
{
	if (auto* Presentation=GetOwner()->FindComponentByClass<UGuLiRogueCardPresentation>()) Presentation->ReceiveOffer(Request,Session,Epoch,Cards,Error);
}
void UGuLiCommanderNetSyncComponent::ClientRogueCardResult_Implementation(FGuid Session, bool bSuccess, const FString& Error)
{
	if (auto* Presentation=GetOwner()->FindComponentByClass<UGuLiRogueCardPresentation>()) Presentation->ReceiveResult(Session,bSuccess,Error);
}
