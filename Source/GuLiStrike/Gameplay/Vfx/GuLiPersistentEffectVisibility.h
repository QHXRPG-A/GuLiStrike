#pragma once
#include "CoreMinimal.h"
#include "Commander/Presentation/GuLiCommanderLODPolicy.h"

enum class EGuLiPersistentEffectVisibility : uint8 { Inactive, Visible, Suspended };
struct FGuLiPersistentEffectVisibility
{
	static constexpr double CheckSeconds = .05, InvisibleGraceSeconds = .15;
	EGuLiPersistentEffectVisibility State = EGuLiPersistentEffectVisibility::Inactive;
	double InvisibleSince = -1;
	TOptional<EGuLiCommanderLODLevel> DetailLevel;
	double DetailChangedAt = -1;
	FGuLiCommanderLODQuery DetailQuery(const FBox& Bounds) const
	{
		FGuLiCommanderLODQuery Query; Query.Bounds=Bounds; Query.CurrentLevel=DetailLevel; Query.LastChangeWorldSeconds=DetailChangedAt;
		return Query;
	}
	void ApplyDetail(const FGuLiCommanderLODDecision& Decision, double Now)
	{
		if (DetailLevel.IsSet() && !Decision.bCanTransition) return;
		if (!DetailLevel.IsSet() || DetailLevel.GetValue()!=Decision.TargetLevel) DetailChangedAt=Now;
		DetailLevel=Decision.TargetLevel;
	}
	EGuLiPersistentEffectVisibility Update(bool bActive, bool bVisible, double Now)
	{
		if (!bActive) { State=EGuLiPersistentEffectVisibility::Inactive; InvisibleSince=-1; }
		else if (bVisible) { State=EGuLiPersistentEffectVisibility::Visible; InvisibleSince=-1; }
		else
		{
			if (InvisibleSince<0) InvisibleSince=Now;
			if (State==EGuLiPersistentEffectVisibility::Inactive) State=EGuLiPersistentEffectVisibility::Visible;
			if (Now-InvisibleSince>=InvisibleGraceSeconds) State=EGuLiPersistentEffectVisibility::Suspended;
		}
		return State;
	}
};
