#include "Gameplay/Stronghold/GuLiStrongholdTransportNetwork.h"

bool FGuLiTransportNode::operator==(const FGuLiTransportNode& Other) const
{
	return TerritoryIndex == Other.TerritoryIndex && TerritoryId == Other.TerritoryId
		&& Team == Other.Team && GroundLocation == Other.GroundLocation && TransitFieldId == Other.TransitFieldId;
}
const FGuLiTransportNode* FGuLiTransportNetworkSnapshot::FindNode(int32 Index) const
{
	return Nodes.FindByPredicate([Index](const auto& Node) { return Node.TerritoryIndex == Index; });
}
bool FGuLiStrongholdTransportNetwork::Rebuild(TArray<FGuLiTransportNode> Nodes)
{
	Nodes.Sort([](const auto& A, const auto& B) { return A.TerritoryId.LexicalLess(B.TerritoryId); });
	if (Snapshot.Revision != 0 && Snapshot.Nodes == Nodes) return false;
	Snapshot.Nodes = MoveTemp(Nodes);
	Snapshot.Edges.Reset();
	struct FCandidate { int32 A; int32 B; double Distance; };
	TArray<FCandidate> Candidates;
	for (int32 A = 0; A < Snapshot.Nodes.Num(); ++A)
		for (int32 B = A+1; B < Snapshot.Nodes.Num(); ++B)
			if (Snapshot.Nodes[A].Team == Snapshot.Nodes[B].Team)
				Candidates.Add({A,B,FVector::DistSquared2D(Snapshot.Nodes[A].GroundLocation,Snapshot.Nodes[B].GroundLocation)});
	Candidates.Sort([](const auto& A, const auto& B)
	{
		if (A.Distance != B.Distance) return A.Distance < B.Distance;
		return A.A != B.A ? A.A < B.A : A.B < B.B;
	});
	TArray<int32> Groups;
	for (int32 I = 0; I < Snapshot.Nodes.Num(); ++I) Groups.Add(I);
	for (const auto& Edge : Candidates)
	{
		const int32 From = Groups[Edge.A], To = Groups[Edge.B];
		if (From == To) continue;
		Snapshot.Edges.Add({Snapshot.Nodes[Edge.A].TerritoryIndex,Snapshot.Nodes[Edge.B].TerritoryIndex,Snapshot.Nodes[Edge.A].Team});
		for (int32& Group : Groups) if (Group == To) Group = From;
	}
	++Snapshot.Revision;
	return true;
}
bool FGuLiStrongholdTransportNetwork::HasEdge(int32 A, int32 B, EGuLiTeam Team) const
{
	return Snapshot.Edges.ContainsByPredicate([=](const auto& Edge)
	{ return Edge.Team == Team && ((Edge.A == A && Edge.B == B) || (Edge.A == B && Edge.B == A)); });
}
TArray<int32> FGuLiStrongholdTransportNetwork::FindRoute(int32 Start, int32 Goal, EGuLiTeam Team) const
{
	const auto* Source = Snapshot.FindNode(Start);
	const auto* Target = Snapshot.FindNode(Goal);
	if (!Source || !Target || Source->Team != Team || Target->Team != Team) return {};
	TMap<int32,int32> Parent; Parent.Add(Start,Start);
	TArray<int32> Queue{Start};
	for (int32 I = 0; I < Queue.Num() && !Parent.Contains(Goal); ++I)
		for (const auto& Edge : Snapshot.Edges)
		{
			if (Edge.Team != Team) continue;
			const int32 Next = Edge.A == Queue[I] ? Edge.B : (Edge.B == Queue[I] ? Edge.A : INDEX_NONE);
			if (Next != INDEX_NONE && !Parent.Contains(Next)) { Parent.Add(Next,Queue[I]); Queue.Add(Next); }
		}
	if (!Parent.Contains(Goal)) return {};
	TArray<int32> Result;
	for (int32 Node = Goal; Node != Start; Node = Parent[Node]) Result.Insert(Node,0);
	Result.Insert(Start,0);
	return Result;
}
