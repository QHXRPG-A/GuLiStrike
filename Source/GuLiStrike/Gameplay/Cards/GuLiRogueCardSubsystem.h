#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Gameplay/Data/Generated/GuLiStrikeRogueCardsTableRows.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "GuLiRogueCardSubsystem.generated.h"
class UGuLiCommanderNetSyncComponent;
class AGuLiBattlePlayerState;
class UDataTable;
class UStringTable;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGuLiGroundRogueBuildChanged,FGuid,Player,int64,Revision);

/** Local catalog and UI view of the team-owned army capability. */
UCLASS()
class GULISTRIKE_API UGuLiRogueCardSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	const FGuLiStrikeRogueCardsCardsRow* FindCard(const FString& Id);
	FText GetText(const FString& Key);
	FText GetCardText(const FString& Id, int32 Index,FGuid Player={},bool bPreviewAcquisition=true);
    UFUNCTION(BlueprintPure,Category="Cards|Ground") int32 GetAcquisitionCount(FGuid Player,const FString& CardId) const;
    UFUNCTION(BlueprintPure,Category="Cards|Ground") bool HasCard(FGuid Player,const FString& CardId) const {return GetAcquisitionCount(Player,CardId)>0;}
    UPROPERTY(BlueprintAssignable,Category="Cards|Ground") FGuLiGroundRogueBuildChanged OnGroundBuildChanged;
	void RequestOffer(UGuLiCommanderNetSyncComponent& Channel, FGuid Request);
	void RerollOffer(UGuLiCommanderNetSyncComponent& Channel, FGuid Request, FGuid Session);
	void Confirm(UGuLiCommanderNetSyncComponent& Channel, FGuid Session, const FString& CardId);
	void Cancel(UGuLiCommanderNetSyncComponent& Channel, FGuid Session);
	void PresentationClosed(UGuLiCommanderNetSyncComponent& Channel, FGuid Session);

    virtual void Deinitialize() override;
private:
    friend class FGuLiHybridArmyBridge;
    friend class FGuLiHybridGroundRogueBridge;
    TMap<FGuid,TMap<FString,int32>> GroundAcquisitions;
    TMap<FGuid,uint64> GroundRevisions;
    uint32 GroundEpoch=0;
	struct FOffer
	{
		FGuid Request, Session, RerolledFrom;
		TWeakObjectPtr<AGuLiBattlePlayerState> Owner;
		EGuLiTeam Team=EGuLiTeam::Unassigned;
		TArray<FString> Candidates;
		FString Selected, Error;
		bool bSubmitted=false, bApplied=false, bFinished=false;
		bool bUpgradePublished=false;
		uint32 HybridPresentationEpoch=0;
		float CommittedTime=0.f;
		TArray<FGuLiSoldierId> UpgradeTargets;
	};
	bool LoadCatalog(FString& Error);

	UPROPERTY(Transient) TObjectPtr<UDataTable> Table;
    UPROPERTY(Transient) TObjectPtr<UDataTable> GroundEffectTable;
	UPROPERTY(Transient) TObjectPtr<UStringTable> StringTable;
	TMap<FString, FGuLiStrikeRogueCardsCardsRow> Catalog;
	TMap<TWeakObjectPtr<UGuLiCommanderNetSyncComponent>, FOffer> Offers;
	TMap<uint32, TMap<FString, int32>> Acquisitions;
	uint32 Epoch=0;
    TMap<uint32,FGuid> HybridClosedSessions;
    enum class EHybridWatch : uint8 { Rejected, Added, Pending };
    EHybridWatch WatchHybridRequest(FGuid Event,FGuid Session,bool bSelection,UGuLiCommanderNetSyncComponent& Channel);
    struct FPendingHybridUI {FGuid Session;bool Selection=false;TWeakObjectPtr<UGuLiCommanderNetSyncComponent> Channel;};
    TMap<FGuid,FPendingHybridUI> HybridRequests;
    FDelegateHandle HybridCommandReceiptHandle;
    void RejectHybridRequest(FGuid Event,const FString& Error);
    void HybridCommandFinished(const FGuLiHybridCommandResult& Result);
    UFUNCTION() void HybridTransactionFinished(const FGuLiHybridTransactionResult& Result);
};
