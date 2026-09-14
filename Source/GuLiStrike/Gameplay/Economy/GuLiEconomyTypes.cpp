// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Economy/GuLiEconomyTypes.h"

namespace
{
	void AdvanceRevision(uint32& Revision)
	{
		Revision = Revision == MAX_uint32 ? 1u : Revision + 1u;
	}
}

int32 FGuLiResourceAmounts::Get(const EGuLiResourceType Type) const
{
	check(Type == EGuLiResourceType::Blue || Type == EGuLiResourceType::Red);
	return Type == EGuLiResourceType::Blue ? Blue : Red;
}

bool FGuLiResourceAmounts::CanAfford(const EGuLiResourceType Type, const int32 Amount) const
{
	return Amount >= 0 && Get(Type) >= Amount;
}

bool FGuLiResourceAmounts::CanAfford(const FGuLiResourceAmounts& Cost) const
{
	return Cost.Blue >= 0 && Cost.Red >= 0 && Blue >= Cost.Blue && Red >= Cost.Red;
}

bool FGuLiResourceAmounts::Add(const EGuLiResourceType Type, const int32 Amount)
{
	check(Type == EGuLiResourceType::Blue || Type == EGuLiResourceType::Red);
	if (Amount <= 0)
	{
		return false;
	}
	int32& Value = Type == EGuLiResourceType::Blue ? Blue : Red;
	check(Value >= 0);
	if (static_cast<int64>(Value) + Amount > MAX_int32) return false;
	Value += Amount;
	AdvanceRevision(Revision);
	return true;
}

bool FGuLiResourceAmounts::Remove(const EGuLiResourceType Type, const int32 Amount)
{
	if (Amount <= 0 || !CanAfford(Type, Amount))
	{
		return false;
	}
	int32& Value = Type == EGuLiResourceType::Blue ? Blue : Red;
	Value -= Amount;
	AdvanceRevision(Revision);
	return true;
}

void FGuLiResourceAmounts::AddChecked(const FGuLiResourceAmounts& Amounts)
{
	check(Amounts.Blue >= 0 && Amounts.Red >= 0 && Amounts.HasPositiveAmount());
	check(Blue >= 0 && Red >= 0);
	check(static_cast<int64>(Blue) + Amounts.Blue <= MAX_int32);
	check(static_cast<int64>(Red) + Amounts.Red <= MAX_int32);
	Blue += Amounts.Blue;
	Red += Amounts.Red;
	AdvanceRevision(Revision);
}

void FGuLiResourceAmounts::RemoveChecked(const FGuLiResourceAmounts& Cost)
{
	check(Cost.HasPositiveAmount() && CanAfford(Cost));
	Blue -= Cost.Blue;
	Red -= Cost.Red;
	AdvanceRevision(Revision);
}

void FGuLiResourceAmounts::Sanitize()
{
	Blue = FMath::Max(0, Blue);
	Red = FMath::Max(0, Red);
}
