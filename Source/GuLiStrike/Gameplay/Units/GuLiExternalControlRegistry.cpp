#include "Gameplay/Units/GuLiExternalControlRegistry.h"
bool UGuLiExternalControlRegistry::Bind(FGuLiHybridObjectKey K,UObject& O,TFunction<bool(FGuLiExternalBody&)> Q,TFunction<bool(const FGuLiExternalControl&)> A,
    TFunction<bool(const FTransform&,TConstArrayView<FGuLiHybridObjectKey>,FTransform&)> P)
{
    if (!K.IsValid() || !Q || !A) return false;
    if (const auto* E=Entries.Find(K);E && E->Owner.IsValid() && E->Owner.Get()!=&O) return false;
    Entries.Add(K,{&O,MoveTemp(Q),MoveTemp(A),MoveTemp(P)});return true;
}
void UGuLiExternalControlRegistry::Unbind(FGuLiHybridObjectKey K,const UObject& O)
{if (const auto* E=Entries.Find(K);E && E->Owner.Get()==&O) Entries.Remove(K);}
bool UGuLiExternalControlRegistry::Query(FGuLiHybridObjectKey K,FGuLiExternalBody& Out) const
{const auto* E=Entries.Find(K);return E && E->Owner.IsValid() && E->Query(Out) && !Out.Transform.ContainsNaN() && Out.Radius>0 && Out.HalfHeight>0;}
bool UGuLiExternalControlRegistry::Apply(FGuLiHybridObjectKey K,const FGuLiExternalControl& S) const
{const auto* E=Entries.Find(K);return E && E->Owner.IsValid() && S.Token.IsValid() && S.Step && !S.Transform.ContainsNaN() && E->Apply(S);}
TOptional<bool> UGuLiExternalControlRegistry::PlanAttached(FGuLiHybridObjectKey K,const FTransform& P,TConstArrayView<FGuLiHybridObjectKey> S,FTransform& Out) const
{const auto* E=Entries.Find(K);if (!E||!E->Owner.IsValid()) return false;if (!E->PlanAttached) return {};return E->PlanAttached(P,S,Out)&&!Out.ContainsNaN();}
