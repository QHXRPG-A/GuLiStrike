#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Gameplay/Data/Generated/GuLiStrikeRogueCardsTableRows.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "GuLiRogueCardSubsystem.generated.h"
class UGuLiCommanderNetSyncComponent;
class AGuLiBattlePlayerState;
class UDataTable;
class UStringTable;

/** Catalog on both peers; offers/acquisitions exist only on the server and belong to a match/team. */
UCLASS()
class GULISTRIKE_API UGuLiRogueCardSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	const FGuLiStrikeRogueCardsCardsRow* FindCard(const FString& Id);
	FText GetText(const FString& Key);
	FText GetCardText(const FString& Id, int32 Index);
	void RequestOffer(UGuLiCommanderNetSyncComponent& Channel, FGuid Request);
	void Confirm(UGuLiCommanderNetSyncComponent& Channel, FGuid Session, const FString& CardId);
	void Cancel(UGuLiCommanderNetSyncComponent& Channel, FGuid Session);
	void PresentationClosed(UGuLiCommanderNetSyncComponent& Channel, FGuid Session);
	/** Only called by the authority simulation, before/after profile commit. */
	void BeginFixedStep();
	void EndFixedStep();
private:
	struct FOffer
	{
		FGuid Request, Session;
		TWeakObjectPtr<AGuLiBattlePlayerState> Owner;
		EGuLiTeam Team=EGuLiTeam::Unassigned;
		TArray<FString> Candidates;
		FString Selected, Error;
		bool bSubmitted=false, bApplied=false, bFinished=false;
		bool bUpgradePublished=false;
		float CommittedTime=0.f;
		TArray<FGuLiSoldierId> UpgradeTargets;
	};
	bool LoadCatalog(FString& Error);
	bool ValidateOwner(UGuLiCommanderNetSyncComponent& Channel, AGuLiBattlePlayerState*& Owner, FString& Error) const;
	void SynchronizeEpoch();
	UPROPERTY(Transient) TObjectPtr<UDataTable> Table;
	UPROPERTY(Transient) TObjectPtr<UStringTable> StringTable;
	TMap<FString, FGuLiStrikeRogueCardsCardsRow> Catalog;
	TMap<TWeakObjectPtr<UGuLiCommanderNetSyncComponent>, FOffer> Offers;
	TMap<uint32, TMap<FString, int32>> Acquisitions;
	uint32 Epoch=0;
};
