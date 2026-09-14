// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Economy/GuLiTeamEconomySubsystem.h"

#include "Engine/World.h"

bool UGuLiTeamEconomySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld();
}

void UGuLiTeamEconomySubsystem::Deinitialize()
{
	RedBalance = FGuLiResourceAmounts{};
	BlueBalance = FGuLiResourceAmounts{};
	PendingReservations.Reset();
	FinalizedRequests.Reset();
	bMatchActive = false;
	bTransactionsOpen = false;
	Super::Deinitialize();
}

void UGuLiTeamEconomySubsystem::BeginMatch(
	const FGuLiResourceAmounts& RedStartingBalance,
	const FGuLiResourceAmounts& BlueStartingBalance)
{
	CheckAuthority();
	checkf(!bMatchActive, TEXT("The team economy may only be initialized once per match."));
	check(RedStartingBalance.Blue >= 0 && RedStartingBalance.Red >= 0);
	check(BlueStartingBalance.Blue >= 0 && BlueStartingBalance.Red >= 0);
	RedBalance = RedStartingBalance;
	BlueBalance = BlueStartingBalance;
	RedBalance.Revision = 1u;
	BlueBalance.Revision = 1u;
	PendingReservations.Reset();
	FinalizedRequests.Reset();
	bMatchActive = true;
	bTransactionsOpen = false;
}

void UGuLiTeamEconomySubsystem::OpenTransactions()
{
	CheckAuthority();
	checkf(bMatchActive, TEXT("A match must own the economy before transactions can open."));
	bTransactionsOpen = true;
}

void UGuLiTeamEconomySubsystem::EndMatch()
{
	CheckAuthority();
	checkf(bMatchActive, TEXT("Cannot end an economy that has no active match."));
	RedBalance = FGuLiResourceAmounts{};
	BlueBalance = FGuLiResourceAmounts{};
	PendingReservations.Reset();
	FinalizedRequests.Reset();
	bMatchActive = false;
	bTransactionsOpen = false;
}

FGuLiResourceAmounts UGuLiTeamEconomySubsystem::GetTeamBalance(const EGuLiTeam Team) const
{
	return GuLiEconomy::IsPlayableTeam(Team) && bMatchActive
		? Balance(Team) : FGuLiResourceAmounts{};
}

void UGuLiTeamEconomySubsystem::Credit(
	const EGuLiTeam Team,
	const EGuLiResourceType Type,
	const int32 Amount)
{
	CheckAuthority();
	checkf(bMatchActive && GuLiEconomy::IsPlayableTeam(Team) && Amount > 0,
		TEXT("Economy credits require an active match, a playable team and a positive amount."));
	check(Type == EGuLiResourceType::Blue || Type == EGuLiResourceType::Red);
	FGuLiResourceAmounts CreditAmounts;
	if (Type == EGuLiResourceType::Blue) CreditAmounts.Blue = Amount;
	else CreditAmounts.Red = Amount;
	MutableBalance(Team).AddChecked(CreditAmounts);
}

bool UGuLiTeamEconomySubsystem::Reserve(
	const EGuLiTeam Team,
	const FGuid& AccountId,
	const uint32 RequestId,
	const FGuLiResourceAmounts& Cost,
	FGuLiEconomyReservation& OutReservation)
{
	CheckAuthority();
	OutReservation.Reset();
	if (!bTransactionsOpen || !GuLiEconomy::IsPlayableTeam(Team)
		|| !AccountId.IsValid() || RequestId == 0u)
	{
		return false;
	}
	checkf(Cost.Blue >= 0 && Cost.Red >= 0 && Cost.HasPositiveAmount(),
		TEXT("Gameplay consumers must submit a positive economy cost."));

	const FGuLiEconomyRequestKey Key{AccountId, RequestId};
	if (FinalizedRequests.Contains(Key))
	{
		return false;
	}
	if (const FGuLiEconomyReservation* Existing = PendingReservations.Find(Key))
	{
		if (Existing->Team != Team || Existing->Cost.Blue != Cost.Blue
			|| Existing->Cost.Red != Cost.Red)
		{
			return false;
		}
		OutReservation = *Existing;
		return true;
	}

	FGuLiResourceAmounts& TeamBalance = MutableBalance(Team);
	if (!TeamBalance.CanAfford(Cost))
	{
		return false;
	}
	TeamBalance.RemoveChecked(Cost);
	OutReservation.Token = FGuid::NewGuid();
	OutReservation.RequestKey = Key;
	OutReservation.Team = Team;
	OutReservation.Cost = Cost;
	PendingReservations.Add(Key, OutReservation);
	return true;
}

void UGuLiTeamEconomySubsystem::Commit(FGuLiEconomyReservation& Reservation)
{
	CheckAuthority();
	check(Reservation.IsValid());
	const FGuLiEconomyReservation& Existing = PendingReservations.FindChecked(Reservation.RequestKey);
	check(Existing.Token == Reservation.Token);
	FinalizedRequests.Add(Reservation.RequestKey);
	PendingReservations.Remove(Reservation.RequestKey);
	Reservation.Reset();
}

void UGuLiTeamEconomySubsystem::Refund(FGuLiEconomyReservation& Reservation)
{
	CheckAuthority();
	check(Reservation.IsValid());
	const FGuLiEconomyReservation& Existing = PendingReservations.FindChecked(Reservation.RequestKey);
	check(Existing.Token == Reservation.Token);
	MutableBalance(Reservation.Team).AddChecked(Reservation.Cost);
	FinalizedRequests.Add(Reservation.RequestKey);
	PendingReservations.Remove(Reservation.RequestKey);
	Reservation.Reset();
}

FGuLiResourceAmounts& UGuLiTeamEconomySubsystem::MutableBalance(const EGuLiTeam Team)
{
	check(GuLiEconomy::IsPlayableTeam(Team));
	return Team == EGuLiTeam::Red ? RedBalance : BlueBalance;
}

const FGuLiResourceAmounts& UGuLiTeamEconomySubsystem::Balance(const EGuLiTeam Team) const
{
	check(GuLiEconomy::IsPlayableTeam(Team));
	return Team == EGuLiTeam::Red ? RedBalance : BlueBalance;
}

void UGuLiTeamEconomySubsystem::CheckAuthority() const
{
	check(GetWorld());
	checkf(GetWorld()->GetNetMode() != NM_Client,
		TEXT("The team economy is mutated only by the authoritative world."));
}
