#include "Gameplay/Stronghold/GuLiStrongholdTopology.h"

bool FGuLiStrongholdTopology::IdLess(int32 A, int32 B) const
{
	return Nodes[A].Id.LexicalLess(Nodes[B].Id);
}

void FGuLiStrongholdTopology::Initialize(TConstArrayView<FGuLiTerritoryDefinition> Territories)
{
	Nodes.Reset(); Edges.Reset(); Nodes.SetNum(Territories.Num());
	for (int32 A = 0; A < Nodes.Num(); ++A)
	{
		Nodes[A].Id = Territories[A].TerritoryId;
		Nodes[A].Center = Territories[A].Center;
		for (int32 B = 0; B < Nodes.Num(); ++B)
		{
			const int32 X = FMath::Abs(int32(Territories[A].BoardColumn) - int32(Territories[B].BoardColumn));
			const int32 Y = FMath::Abs(int32(Territories[A].BoardRow) - int32(Territories[B].BoardRow));
			if (FMath::Max(X,Y) == 1) Nodes[A].Neighbors.Add(B);
			if (X + Y == 1) Nodes[A].CardinalNeighbors.Add(B);
		}
	}
	TArray<FGuLiStrongholdEdge> Candidates;
	for (int32 A = 0; A < Nodes.Num(); ++A)
		for (int32 B = A + 1; B < Nodes.Num(); ++B)
			Candidates.Add(IdLess(A,B) ? FGuLiStrongholdEdge{A,B} : FGuLiStrongholdEdge{B,A});
	Candidates.Sort([this](const auto& L, const auto& R)
	{
		const double LD = FVector::DistSquared2D(Nodes[L.A].Center, Nodes[L.B].Center);
		const double RD = FVector::DistSquared2D(Nodes[R.A].Center, Nodes[R.B].Center);
		if (LD != RD) return LD < RD;
		return L.A != R.A ? IdLess(L.A, R.A) : IdLess(L.B, R.B);
	});
	TArray<int32> Group;
	for (int32 I = 0; I < Nodes.Num(); ++I) Group.Add(I);
	for (const auto& Edge : Candidates)
	{
		const int32 From = Group[Edge.A], To = Group[Edge.B];
		if (From == To) continue;
		Edges.Add(Edge);
		Nodes[Edge.A].TransportNeighbors.Add(Edge.B);
		Nodes[Edge.B].TransportNeighbors.Add(Edge.A);
		for (int32& G : Group) if (G == To) G = From;
		if (Edges.Num() == Nodes.Num() - 1) break;
	}
}

bool FGuLiStrongholdTopology::IsAdjacent(int32 A, int32 B) const
{
	return Nodes.IsValidIndex(A) && Nodes[A].Neighbors.Contains(B);
}

TArray<int32> FGuLiStrongholdTopology::FindRoute(int32 Start, int32 Goal, TFunctionRef<bool(int32)> CanUse) const
{
	if (!Nodes.IsValidIndex(Start) || !Nodes.IsValidIndex(Goal) || !CanUse(Start) || !CanUse(Goal)) return {};
	TArray<int32> Parent; Parent.Init(INDEX_NONE, Nodes.Num());
	TArray<int32> Queue{Start}; Parent[Start] = Start;
	for (int32 I = 0; I < Queue.Num() && Parent[Goal] == INDEX_NONE; ++I)
		for (int32 Next : Nodes[Queue[I]].TransportNeighbors)
			if (Parent[Next] == INDEX_NONE && CanUse(Next)) { Parent[Next] = Queue[I]; Queue.Add(Next); }
	if (Parent[Goal] == INDEX_NONE) return {};
	TArray<int32> Result;
	for (int32 N = Goal; N != Start; N = Parent[N]) Result.Insert(N, 0);
	Result.Insert(Start, 0);
	return Result;
}

TArray<int32> FGuLiStrongholdTopology::FindAttackCandidates(int32 Start, EGuLiTeam Team,
	TFunctionRef<EGuLiTeam(int32)> Owner) const
{
	if (!Nodes.IsValidIndex(Start)) return {};
	TArray<double> Distance; Distance.Init(TNumericLimits<double>::Max(), Nodes.Num());
	TArray<bool> Visited; Visited.Init(false, Nodes.Num()); Distance[Start] = 0;
	for (int32 I = 0; I < Nodes.Num(); ++I)
	{
		int32 Best = INDEX_NONE;
		for (int32 N = 0; N < Nodes.Num(); ++N)
			if (!Visited[N] && (Best == INDEX_NONE || Distance[N] < Distance[Best]
				|| (Distance[N] == Distance[Best] && IdLess(N,Best)))) Best = N;
		Visited[Best] = true;
		for (int32 Next : Nodes[Best].Neighbors)
			Distance[Next] = FMath::Min(Distance[Next], Distance[Best] + FVector::Dist2D(Nodes[Best].Center, Nodes[Next].Center));
	}
	TArray<int32> Result;
	for (int32 N = 0; N < Nodes.Num(); ++N) if (N != Start && Owner(N) != Team) Result.Add(N);
	Result.Sort([&](int32 A, int32 B)
	{
		const bool AN = IsAdjacent(Start,A), BN = IsAdjacent(Start,B);
		if (AN != BN) return AN;
		return Distance[A] != Distance[B] ? Distance[A] < Distance[B] : IdLess(A,B);
	});
	return Result;
}

TArray<bool> FGuLiStrongholdTopology::FindEncircled(EGuLiTeam Team, TFunctionRef<EGuLiTeam(int32)> Owner) const
{
	const EGuLiTeam Enemy = Team == EGuLiTeam::Red ? EGuLiTeam::Blue : EGuLiTeam::Red;
	TArray<bool> Reachable; Reachable.Init(false, Nodes.Num());
	TArray<int32> Queue;
	for (int32 N = 0; N < Nodes.Num(); ++N)
		if (Nodes[N].CardinalNeighbors.Num() < 4 && Owner(N) != Enemy) { Reachable[N] = true; Queue.Add(N); }
	for (int32 I = 0; I < Queue.Num(); ++I)
		for (int32 Next : Nodes[Queue[I]].CardinalNeighbors)
			if (!Reachable[Next] && Owner(Next) != Enemy) { Reachable[Next] = true; Queue.Add(Next); }
	for (int32 N = 0; N < Nodes.Num(); ++N) Reachable[N] = Owner(N) == Team && !Reachable[N];
	return Reachable;
}
