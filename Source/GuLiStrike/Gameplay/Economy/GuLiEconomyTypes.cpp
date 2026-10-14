// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Economy/GuLiEconomyTypes.h"
#include "Network/Private/GuLiNetConversions.h"

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
	return GuLiNet::CanAfford(GuLiNetBridge::ToCore(*this), GuLiNetBridge::ToCore(Cost));
}

bool FGuLiResourceAmounts::Add(const EGuLiResourceType Type, const int32 Amount)
{
	check(Type == EGuLiResourceType::Blue || Type == EGuLiResourceType::Red);
	auto Value = GuLiNetBridge::ToCore(*this); GuLiNet::Resources Delta;
	if (Type == EGuLiResourceType::Blue) Delta.Blue = Amount; else Delta.Red = Amount;
	if (!GuLiNet::Credit(Value, Delta)) return false;
	*this = GuLiNetBridge::ToUE(Value);
	return true;
}

bool FGuLiResourceAmounts::Remove(const EGuLiResourceType Type, const int32 Amount)
{
	check(Type == EGuLiResourceType::Blue || Type == EGuLiResourceType::Red);
	auto Value = GuLiNetBridge::ToCore(*this); GuLiNet::Resources Delta;
	if (Type == EGuLiResourceType::Blue) Delta.Blue = Amount; else Delta.Red = Amount;
	if (!GuLiNet::Debit(Value, Delta)) return false;
	*this = GuLiNetBridge::ToUE(Value);
	return true;
}

void FGuLiResourceAmounts::AddChecked(const FGuLiResourceAmounts& Amounts)
{
	auto Value = GuLiNetBridge::ToCore(*this);
	const bool Added = GuLiNet::Credit(Value, GuLiNetBridge::ToCore(Amounts));
	checkf(Added, TEXT("Resource credit must be positive and fit int32."));
	if (Added) *this = GuLiNetBridge::ToUE(Value);
}

void FGuLiResourceAmounts::RemoveChecked(const FGuLiResourceAmounts& Cost)
{
	auto Value = GuLiNetBridge::ToCore(*this);
	const bool Removed = GuLiNet::Debit(Value, GuLiNetBridge::ToCore(Cost));
	checkf(Removed, TEXT("Resource debit must be positive and affordable."));
	if (Removed) *this = GuLiNetBridge::ToUE(Value);
}

void FGuLiResourceAmounts::Sanitize()
{
	Blue = FMath::Max(0, Blue);
	Red = FMath::Max(0, Red);
}
