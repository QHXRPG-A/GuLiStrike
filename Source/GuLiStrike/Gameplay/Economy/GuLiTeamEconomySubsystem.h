// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Economy/GuLiEconomyTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "GuLiTeamEconomySubsystem.generated.h"

struct GULISTRIKE_API FGuLiEconomyRequestKey
{
	FGuid AccountId;
	uint32 RequestId = 0u;

	bool IsValid() const { return AccountId.IsValid() && RequestId != 0u; }

	friend bool operator==(const FGuLiEconomyRequestKey& Lhs, const FGuLiEconomyRequestKey& Rhs)
	{
		return Lhs.AccountId == Rhs.AccountId && Lhs.RequestId == Rhs.RequestId;
	}

	friend uint32 GetTypeHash(const FGuLiEconomyRequestKey& Key)
	{
		return HashCombine(GetTypeHash(Key.AccountId), GetTypeHash(Key.RequestId));
	}
};

/** Opaque reservation returned by the economy and consumed exactly once by commit or refund. */
struct GULISTRIKE_API FGuLiEconomyReservation
{
	FGuid Token;
	FGuLiEconomyRequestKey RequestKey;
	EGuLiTeam Team = EGuLiTeam::Unassigned;
	FGuLiResourceAmounts Cost;

	bool IsValid() const
	{
		return Token.IsValid() && RequestKey.IsValid()
			&& GuLiEconomy::IsPlayableTeam(Team) && Cost.HasPositiveAmount();
	}

	void Reset() { *this = FGuLiEconomyReservation{}; }
};

/** Narrow server-authoritative contract used by resource producers and gameplay consumers. */
class GULISTRIKE_API IGuLiTeamEconomy
{
public:
	virtual ~IGuLiTeamEconomy() = default;
	virtual bool IsMatchActive() const = 0;
	virtual bool AreTransactionsOpen() const = 0;
	virtual FGuLiResourceAmounts GetTeamBalance(EGuLiTeam Team) const = 0;
	virtual void Credit(EGuLiTeam Team, EGuLiResourceType Type, int32 Amount) = 0;
	virtual bool Reserve(
		EGuLiTeam Team,
		const FGuid& AccountId,
		uint32 RequestId,
		const FGuLiResourceAmounts& Cost,
		FGuLiEconomyReservation& OutReservation) = 0;
	virtual void Commit(FGuLiEconomyReservation& Reservation) = 0;
	virtual void Refund(FGuLiEconomyReservation& Reservation) = 0;
};

/** Match-local implementation. Domain systems activate it explicitly and own transaction readiness. */
UCLASS()
class GULISTRIKE_API UGuLiTeamEconomySubsystem final
	: public UWorldSubsystem
	, public IGuLiTeamEconomy
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;

	void BeginMatch(
		const FGuLiResourceAmounts& RedStartingBalance,
		const FGuLiResourceAmounts& BlueStartingBalance);
	void OpenTransactions();
	void EndMatch();

	virtual bool IsMatchActive() const override { return bMatchActive; }
	virtual bool AreTransactionsOpen() const override { return bTransactionsOpen; }
	virtual FGuLiResourceAmounts GetTeamBalance(EGuLiTeam Team) const override;
	virtual void Credit(EGuLiTeam Team, EGuLiResourceType Type, int32 Amount) override;
	virtual bool Reserve(
		EGuLiTeam Team,
		const FGuid& AccountId,
		uint32 RequestId,
		const FGuLiResourceAmounts& Cost,
		FGuLiEconomyReservation& OutReservation) override;
	virtual void Commit(FGuLiEconomyReservation& Reservation) override;
	virtual void Refund(FGuLiEconomyReservation& Reservation) override;

private:
	FGuLiResourceAmounts RedBalance;
	FGuLiResourceAmounts BlueBalance;
	TMap<FGuLiEconomyRequestKey, FGuLiEconomyReservation> PendingReservations;
	TSet<FGuLiEconomyRequestKey> FinalizedRequests;
	bool bMatchActive = false;
	bool bTransactionsOpen = false;

	FGuLiResourceAmounts& MutableBalance(EGuLiTeam Team);
	const FGuLiResourceAmounts& Balance(EGuLiTeam Team) const;
	void CheckAuthority() const;
};
