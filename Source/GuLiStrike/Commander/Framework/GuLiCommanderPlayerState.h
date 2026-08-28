// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "GameFramework/PlayerState.h"
#include "GuLiCommanderPlayerState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGuLiCommanderPlayerStateChangedSignature);

/** Replicated identity and authoritative 5v5 role assignment for one connection. */
UCLASS()
class AGuLiCommanderPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	static constexpr uint8 InvalidSlotIndex = MAX_uint8;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void CopyProperties(APlayerState* PlayerState) override;
	virtual void OverrideWith(APlayerState* PlayerState) override;

	/** Server-only identity creation; preserves a valid reconnect/seamless-travel guid. */
	void EnsureServerPlayerGuid();

	/** Server-only assignment mutation. */
	void SetServerRoleAssignment(EGuLiTeam NewTeam, EGuLiCommanderRole NewRole, uint8 NewSlotIndex);
	void SetServerSyncReady(bool bNewSyncReady);
	void SetServerObserver();

	UFUNCTION(BlueprintPure, Category = "Commander|Player")
	FGuid GetPlayerGuid() const { return PlayerGuid; }

	UFUNCTION(BlueprintPure, Category = "Commander|Player")
	EGuLiTeam GetTeam() const { return Team; }

	UFUNCTION(BlueprintPure, Category = "Commander|Player")
	EGuLiCommanderRole GetCommanderRole() const { return CommanderRole; }

	UFUNCTION(BlueprintPure, Category = "Commander|Player")
	uint8 GetCommanderSlotIndex() const { return SlotIndex; }

	UFUNCTION(BlueprintPure, Category = "Commander|Player")
	bool IsSyncReady() const { return bSyncReady; }

	UFUNCTION(BlueprintPure, Category = "Commander|Player")
	bool IsCommander() const
	{
		return CommanderRole == EGuLiCommanderRole::Commander && Team != EGuLiTeam::Unassigned;
	}

	UPROPERTY(BlueprintAssignable, Category = "Commander|Player")
	FGuLiCommanderPlayerStateChangedSignature OnCommanderPlayerStateChanged;

private:
	void NotifyStateChanged();

	UFUNCTION()
	void OnRep_Assignment();

	UFUNCTION()
	void OnRep_SyncReady();

	UPROPERTY(ReplicatedUsing = OnRep_Assignment)
	FGuid PlayerGuid;

	UPROPERTY(ReplicatedUsing = OnRep_Assignment)
	EGuLiTeam Team = EGuLiTeam::Unassigned;

	UPROPERTY(ReplicatedUsing = OnRep_Assignment)
	EGuLiCommanderRole CommanderRole = EGuLiCommanderRole::Observer;

	UPROPERTY(ReplicatedUsing = OnRep_Assignment)
	uint8 SlotIndex = InvalidSlotIndex;

	UPROPERTY(ReplicatedUsing = OnRep_SyncReady)
	bool bSyncReady = false;
};
