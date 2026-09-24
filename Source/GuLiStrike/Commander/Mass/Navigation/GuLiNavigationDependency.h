#pragma once
#include "CoreMinimal.h"
#include "NavigationData.h"
#include "NavMesh/NavMeshPath.h"
#include "Commander/Mass/Navigation/GuLiNavigationWorkBudget.h"

/** Native paths remain registered for invalidation, with engine automatic repathing disabled. */
struct FGuLiNavigationDependency
{
	enum class ECheck : uint8 { Pending, Valid, Invalid };
	TWeakObjectPtr<const ANavigationData> Data;
	FNavPathSharedPtr Path;
	TArray<NavNodeRef> Nodes;
	uint32 CheckedGeneration = 0, CheckingGeneration = 0;
	int32 Cursor = 0;
	bool bComplete = false;
	void Capture(const ANavigationData& InData,uint32 Generation,const FNavPathSharedPtr& InPath)
	{
		Data=&InData; Path=InPath; Nodes.Reset(); Cursor=0;
		CheckedGeneration=CheckingGeneration=Generation; bComplete=false;
		if (!Path.IsValid()) return;
		Path->EnableRecalculationOnInvalidation(false);
		if (const auto* MeshPath=Path->CastPath<FNavMeshPath>())
		{ Nodes=MeshPath->PathCorridor; bComplete=!Nodes.IsEmpty(); }
	}
	ECheck Check(const ANavigationData& InData,uint32 Generation,FGuLiNavigationWorkBudget& Budget)
	{
		if (Data.Get()!=&InData || (Path.IsValid() && !Path->IsUpToDate())) return ECheck::Invalid;
		if (CheckedGeneration==Generation) return ECheck::Valid;
		if (!bComplete) return ECheck::Invalid;
		if (CheckingGeneration!=Generation) { CheckingGeneration=Generation; Cursor=0; }
		while (Cursor<Nodes.Num() && Budget.TakeProjection())
			if (!InData.IsNodeRefValid(Nodes[Cursor++])) return ECheck::Invalid;
		if (Cursor<Nodes.Num()) return ECheck::Pending;
		CheckedGeneration=Generation; return ECheck::Valid;
	}
};
