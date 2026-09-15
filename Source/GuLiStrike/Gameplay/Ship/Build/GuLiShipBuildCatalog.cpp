#include "Gameplay/Ship/Build/GuLiShipBuildCatalog.h"
#include "Gameplay/Ship/Capabilities/GuLiShipCapabilityComponent.h"

namespace GuLiShipBuildRules
{
	bool Reject(FString& Error, const FString& Message) { Error = Message; return false; }

	bool ValidateExpression(const FGuLiShipRuleExpression& Expression, const TSet<FName>& Nodes,
		const TSet<FName>& Routes, const TSet<FName>& Capabilities, const bool bAcquisition,
		TArray<FName>& Dependencies, FString& Error)
	{
		if (Expression.Nodes.IsEmpty())
			return Expression.Root == INDEX_NONE || Reject(Error, TEXT("Empty expression must have no root."));
		TArray<uint8> Colors;
		Colors.Init(0, Expression.Nodes.Num());
		TFunction<bool(int32)> Visit = [&](int32 Index)
		{
			if (!Expression.Nodes.IsValidIndex(Index)) return Reject(Error, TEXT("Rule references an invalid child index."));
			if (Colors[Index] == 1) return Reject(Error, TEXT("Rule expression contains a cycle."));
			if (Colors[Index] == 2) return true;
			Colors[Index] = 1;
			const auto& Node = Expression.Nodes[Index];
			const bool bOperator = Node.Op == EGuLiShipRuleOp::All || Node.Op == EGuLiShipRuleOp::Any
				|| Node.Op == EGuLiShipRuleOp::AtLeast;
			if (bOperator)
			{
				if (Node.Children.IsEmpty()) return Reject(Error, TEXT("Rule operator requires children; use an empty expression for true."));
				if (Node.Op == EGuLiShipRuleOp::AtLeast && (Node.Required < 1 || Node.Required > Node.Children.Num()))
					return Reject(Error, TEXT("AtLeast count is outside the number of children."));
				TSet<int32> Seen;
				for (const int32 Child : Node.Children)
				{
					if (Seen.Contains(Child)) return Reject(Error, TEXT("Duplicate rule child would count the same condition twice."));
					Seen.Add(Child);
					if (!Visit(Child)) return false;
				}
			}
			else
			{
				if (!Node.Children.IsEmpty() || Node.SubjectId.IsNone()) return Reject(Error, TEXT("Rule operand requires an identity and no children."));
				switch (Node.Op)
				{
				case EGuLiShipRuleOp::ChosenNode:
					if (!Nodes.Contains(Node.SubjectId)) return Reject(Error, FString::Printf(TEXT("Unknown prerequisite %s."), *Node.SubjectId.ToString()));
					Dependencies.AddUnique(Node.SubjectId);
					break;
				case EGuLiShipRuleOp::RouteLevel:
					if (!Routes.Contains(Node.SubjectId) || Node.Required < 1) return Reject(Error, TEXT("Invalid route-level operand."));
					break;
				case EGuLiShipRuleOp::InstalledCapability:
					if (bAcquisition) return Reject(Error, TEXT("Acquisition prerequisites must use persistent choices or route levels."));
					if (!Capabilities.Contains(Node.SubjectId)) return Reject(Error, TEXT("Unknown installed capability operand."));
					break;
				default: return Reject(Error, TEXT("Unknown rule operator."));
				}
			}
			Colors[Index] = 2;
			return true;
		};
		if (!Visit(Expression.Root)) return false;
		for (const uint8 Color : Colors) if (Color != 2) return Reject(Error, TEXT("Expression contains unreachable nodes."));
		return true;
	}

	bool SortDependencies(const TMap<FName, TArray<FName>>& Graph, TArray<FName>& Order, FString& Error)
	{
		TMap<FName, uint8> Colors;
		TArray<FName> Path;
		TFunction<bool(FName)> Visit = [&](FName Id)
		{
			if (Colors.FindRef(Id) == 2) return true;
			if (Colors.FindRef(Id) == 1)
			{
				FString Chain;
				for (FName Item : Path) Chain += Item.ToString() + TEXT(" -> ");
				return Reject(Error, TEXT("Dependency cycle: ") + Chain + Id.ToString());
			}
			Colors.Add(Id, 1); Path.Add(Id);
			if (const auto* Edges = Graph.Find(Id)) for (FName Dependency : *Edges) if (!Visit(Dependency)) return false;
			Path.Pop(); Colors.Add(Id, 2); Order.Add(Id);
			return true;
		};
		TArray<FName> Keys; Graph.GetKeys(Keys);
		Keys.Sort(FNameLexicalLess());
		for (FName Key : Keys) if (!Visit(Key)) return false;
		return true;
	}

	/** Configuration-time witnesses catch All/AtLeast prerequisites that can never coexist. */
	bool ValidateObtainableHistories(const UGuLiShipBuildCatalog& Catalog, const FGuLiShipCompiledBuildRules& Rules, FString& Error)
	{
		using FHistory = TSet<FName>;
		using FAlternatives = TArray<FHistory>;
		TMap<FName, FAlternatives> Histories;
		auto IsAncestor = [&](FName Ancestor, FName Descendant)
		{
			for (FName Id = Descendant; !Id.IsNone(); Id = Catalog.Upgrades[Rules.NodeIndices.FindChecked(Id)].ReplacesNodeId)
				if (Id == Ancestor) return true;
			return false;
		};
		auto Merge = [&](const FAlternatives& Left, const FAlternatives& Right)
		{
			FAlternatives Result;
			for (const auto& A : Left) for (const auto& B : Right)
			{
				bool bCompatible = true;
				for (FName AId : A) for (FName BId : B)
				{
					const auto& ANode = Catalog.Upgrades[Rules.NodeIndices.FindChecked(AId)];
					const auto& BNode = Catalog.Upgrades[Rules.NodeIndices.FindChecked(BId)];
					const auto* Conflicts = Rules.ConflictsByRoute.Find(ANode.RouteId);
					if ((Conflicts && Conflicts->Contains(BNode.RouteId)) || (ANode.RouteId == BNode.RouteId
						&& !IsAncestor(AId, BId) && !IsAncestor(BId, AId))) bCompatible = false;
				}
				if (!bCompatible) continue;
				FHistory Combined = A; Combined.Append(B);
				// Supersets cannot unlock an otherwise impossible expression; keep minimal witnesses.
				if (Result.ContainsByPredicate([&Combined](const FHistory& Existing) { return Existing.Difference(Combined).IsEmpty(); })) continue;
				Result.RemoveAll([&Combined](const FHistory& Existing) { return Combined.Difference(Existing).IsEmpty(); });
				Result.Add(MoveTemp(Combined));
			}
			return Result;
		};
		for (FName NodeId : Rules.TopologicalOrder)
		{
			const auto& Upgrade = Catalog.Upgrades[Rules.NodeIndices.FindChecked(NodeId)];
			const auto& Expression = Upgrade.AcquisitionRequirements;
			TFunction<FAlternatives(int32)> Visit = [&](int32 Index) -> FAlternatives
			{
				const auto& Node = Expression.Nodes[Index];
				if (Node.Op == EGuLiShipRuleOp::ChosenNode) return Histories.FindChecked(Node.SubjectId);
				if (Node.Op == EGuLiShipRuleOp::RouteLevel)
				{
					int32 FirstLevel = MAX_int32;
					for (const auto& Other : Catalog.Upgrades) if (Other.RouteId == Node.SubjectId && Other.Level >= Node.Required)
						FirstLevel = FMath::Min(FirstLevel, Other.Level);
					FAlternatives Alternatives;
					for (const auto& Other : Catalog.Upgrades) if (Other.RouteId == Node.SubjectId && Other.Level == FirstLevel)
						Alternatives.Append(Histories.FindChecked(Other.NodeId));
					return Alternatives;
				}
				const int32 Required = Node.Op == EGuLiShipRuleOp::All ? Node.Children.Num() : Node.Op == EGuLiShipRuleOp::Any ? 1 : Node.Required;
				TArray<FAlternatives> ByCount; ByCount.SetNum(Required + 1); ByCount[0].Add(FHistory{});
				for (int32 Child : Node.Children)
				{
					const auto Choices = Visit(Child);
					for (int32 Count = Required; Count > 0; --Count) ByCount[Count].Append(Merge(ByCount[Count - 1], Choices));
				}
				return ByCount[Required];
			};
			FAlternatives Alternatives;
			if (Expression.Nodes.IsEmpty()) Alternatives.Add(FHistory{}); else Alternatives = Visit(Expression.Root);
			if (!Upgrade.ReplacesNodeId.IsNone()) Alternatives = Merge(Alternatives, Histories.FindChecked(Upgrade.ReplacesNodeId));
			FHistory Self; Self.Add(NodeId);
			Alternatives = Merge(Alternatives, FAlternatives{Self});
			if (Alternatives.IsEmpty()) return Reject(Error, NodeId.ToString() + TEXT(": mandatory prerequisites conflict with exclusions or incompatible upgrades."));
			Histories.Add(NodeId, MoveTemp(Alternatives));
		}
		return true;
	}
}

const FGuLiShipUpgradeNode* UGuLiShipBuildCatalog::FindUpgrade(FName NodeId) const
{
	return Upgrades.FindByPredicate([NodeId](const auto& Item) { return Item.NodeId == NodeId; });
}

const FGuLiShipPartGroupDefinition* UGuLiShipBuildCatalog::FindGroup(FName ConfigurationId) const
{
	return Groups.FindByPredicate([ConfigurationId](const auto& Item) { return Item.ConfigurationId == ConfigurationId; });
}

bool UGuLiShipBuildCatalog::Compile(FGuLiShipCompiledBuildRules& Out) const
{
	using namespace GuLiShipBuildRules;
	Out = {};
	if (Revision < 1) return Reject(Out.Error, TEXT("Catalogue revision must be positive."));
	TSet<FName> NodeIds, Routes, CapabilityIds;
	for (int32 Index = 0; Index < Upgrades.Num(); ++Index)
	{
		const auto& Node = Upgrades[Index];
		if (Node.NodeId.IsNone() || Node.RouteId.IsNone() || Node.Level < 1 || NodeIds.Contains(Node.NodeId))
			return Reject(Out.Error, TEXT("Upgrade has an empty/duplicate identity or invalid level."));
		NodeIds.Add(Node.NodeId); Routes.Add(Node.RouteId); Out.NodeIndices.Add(Node.NodeId, Index);
	}
	for (int32 Index = 0; Index < Groups.Num(); ++Index)
	{
		const auto& Group = Groups[Index];
		if (Group.ConfigurationId.IsNone() || Group.GroupId.IsNone() || Out.GroupIndices.Contains(Group.ConfigurationId))
			return Reject(Out.Error, TEXT("Group has an empty/duplicate configuration identity."));
		Out.GroupIndices.Add(Group.ConfigurationId, Index);
		TSet<FName> Sockets, LocalCapabilities;
		for (const auto& Mount : Group.Mounts)
		{
			if (Mount.SocketName.IsNone() || Mount.PartClass.IsNull() || Sockets.Contains(Mount.SocketName))
				return Reject(Out.Error, FString::Printf(TEXT("Group %s has an invalid or duplicate mount %s."), *Group.ConfigurationId.ToString(), *Mount.SocketName.ToString()));
			Sockets.Add(Mount.SocketName);
		}
		for (const auto& Capability : Group.Capabilities)
		{
			if (Capability.CapabilityId.IsNone() || LocalCapabilities.Contains(Capability.CapabilityId))
				return Reject(Out.Error, TEXT("Group has an empty or duplicate capability identity."));
			if (Group.bExecutable)
			{
				if (!Capability.ComponentClass || Capability.ComponentClass->HasAnyClassFlags(CLASS_Abstract))
					return Reject(Out.Error, TEXT("Executable group requires concrete capability classes."));
				if (!Capability.ComponentClass->GetDefaultObject<UGuLiShipCapabilityComponent>()->ValidateConfiguration(Capability.Configuration, Out.Error))
					return false;
			}
			LocalCapabilities.Add(Capability.CapabilityId); CapabilityIds.Add(Capability.CapabilityId);
			for (const FGameplayTag Tag : Capability.ProvidedTags) CapabilityIds.Add(Tag.GetTagName());
		}
		if (Group.bExecutable && Group.Capabilities.IsEmpty()) return Reject(Out.Error, TEXT("Executable group must provide an implemented capability."));
	}
	TSet<FName> ExclusionIds;
	for (const auto& Rule : Exclusions)
	{
		if (Rule.RuleId.IsNone() || ExclusionIds.Contains(Rule.RuleId) || Rule.Routes.Num() < 2)
			return Reject(Out.Error, TEXT("Exclusion requires a unique ID and at least two routes."));
		ExclusionIds.Add(Rule.RuleId);
		TSet<FName> Seen;
		for (FName Route : Rule.Routes)
		{
			if (!Routes.Contains(Route) || Seen.Contains(Route)) return Reject(Out.Error, TEXT("Exclusion references an unknown or duplicate route."));
			Seen.Add(Route);
			for (FName Other : Rule.Routes) if (Other != Route) Out.ConflictsByRoute.FindOrAdd(Route).Add(Other);
		}
	}
	TMap<FName, TArray<FName>> UpgradeEdges;
	for (const auto& Node : Upgrades)
		if (!Out.GroupIndices.Contains(Node.GroupConfigurationId))
			return Reject(Out.Error, Node.NodeId.ToString() + TEXT(" references an unknown group configuration."));
	for (const auto& Node : Upgrades)
	{
		auto& Dependencies = Out.Dependencies.FindOrAdd(Node.NodeId);
		if (!ValidateExpression(Node.AcquisitionRequirements, NodeIds, Routes, CapabilityIds, true, Dependencies, Out.Error))
		{ Out.Error = Node.NodeId.ToString() + TEXT(": ") + Out.Error; return false; }
		for (const auto& Operand : Node.AcquisitionRequirements.Nodes)
			if (Operand.Op == EGuLiShipRuleOp::RouteLevel)
			{
				int32 FirstSufficientLevel = MAX_int32;
				for (const auto& Other : Upgrades) if (Other.RouteId == Operand.SubjectId && Other.Level >= Operand.Required)
					FirstSufficientLevel = FMath::Min(FirstSufficientLevel, Other.Level);
				if (FirstSufficientLevel == MAX_int32) return Reject(Out.Error, TEXT("Route-level prerequisite cannot be supplied by this catalogue."));
				for (const auto& Other : Upgrades) if (Other.RouteId == Operand.SubjectId && Other.Level == FirstSufficientLevel)
					Dependencies.AddUnique(Other.NodeId);
			}
		auto& Replacements = UpgradeEdges.FindOrAdd(Node.NodeId);
		if (!Node.ReplacesNodeId.IsNone())
		{
			const auto* Previous = FindUpgrade(Node.ReplacesNodeId);
			if (!Previous || Previous->RouteId != Node.RouteId || Previous->Level >= Node.Level
				|| FindGroup(Previous->GroupConfigurationId)->GroupId != FindGroup(Node.GroupConfigurationId)->GroupId)
				return Reject(Out.Error, TEXT("Upgrade replacement must advance the same route and group."));
			Replacements.Add(Node.ReplacesNodeId); Dependencies.AddUnique(Node.ReplacesNodeId);
		}
		for (FName Dependency : Dependencies) Out.ReverseDependencies.FindOrAdd(Dependency).AddUnique(Node.NodeId);
		// Evaluate the most permissive history compatible with this route. Any/AtLeast
		// alternatives remain valid; only unavoidable conflicting prerequisites fail.
		FGuLiShipRuleContext Compatible;
		const auto* Conflicts = Out.ConflictsByRoute.Find(Node.RouteId);
		for (const auto& Other : Upgrades) if (!Conflicts || !Conflicts->Contains(Other.RouteId))
		{
			Compatible.ChosenNodes.Add(Other.NodeId);
			Compatible.RouteLevels.FindOrAdd(Other.RouteId) = FMath::Max(Compatible.RouteLevels.FindRef(Other.RouteId), Other.Level);
		}
		if (!Evaluate(Node.AcquisitionRequirements, Compatible)) return Reject(Out.Error, Node.NodeId.ToString() + TEXT(" requires an excluded route."));
	}
	TArray<FName> UpgradeOrder;
	if (!SortDependencies(UpgradeEdges, UpgradeOrder, Out.Error)
		|| !SortDependencies(Out.Dependencies, Out.TopologicalOrder, Out.Error)) return false;
	if (!ValidateObtainableHistories(*this, Out, Out.Error)) return false;
	TMap<FName, TArray<FName>> Providers;
	auto CapabilityKey = [](const auto& Group, const auto& Capability)
	{ return FName(Group.ConfigurationId.ToString() + TEXT("/") + Capability.CapabilityId.ToString()); };
	for (const auto& Group : Groups) for (const auto& Capability : Group.Capabilities)
	{
		const FName Key = CapabilityKey(Group, Capability);
		Providers.FindOrAdd(Capability.CapabilityId).AddUnique(Key);
		for (auto Tag : Capability.ProvidedTags) Providers.FindOrAdd(Tag.GetTagName()).AddUnique(Key);
	}
	for (const auto& Group : Groups) for (const auto& Capability : Group.Capabilities)
	{
		TArray<FName> Ignored;
		if (!ValidateExpression(Capability.RuntimeRequirements, NodeIds, Routes, CapabilityIds, false, Ignored, Out.Error)) return false;
		const FName Key = CapabilityKey(Group, Capability);
		auto& Dependencies = Out.RuntimeDependencies.FindOrAdd(Key);
		for (const auto& Operand : Capability.RuntimeRequirements.Nodes) if (Operand.Op == EGuLiShipRuleOp::InstalledCapability)
			for (FName Provider : Providers.FindChecked(Operand.SubjectId))
			{ Dependencies.AddUnique(Provider); Out.RuntimeReverseDependencies.FindOrAdd(Provider).AddUnique(Key); }
	}
	if (!SortDependencies(Out.RuntimeDependencies, Out.RuntimeTopologicalOrder, Out.Error)) return false;
	Out.bValid = true;
	return true;
}

bool UGuLiShipBuildCatalog::Evaluate(const FGuLiShipRuleExpression& Expression,
	const FGuLiShipRuleContext& Context, FString* Failure)
{
	if (Expression.Nodes.IsEmpty()) return true;
	TFunction<bool(int32)> Visit = [&](int32 Index)
	{
		const auto& Node = Expression.Nodes[Index];
		switch (Node.Op)
		{
		case EGuLiShipRuleOp::ChosenNode: return Context.ChosenNodes.Contains(Node.SubjectId);
		case EGuLiShipRuleOp::RouteLevel: return Context.RouteLevels.FindRef(Node.SubjectId) >= Node.Required;
		case EGuLiShipRuleOp::InstalledCapability: return Context.InstalledCapabilities.Contains(Node.SubjectId);
		default:
			int32 Satisfied = 0;
			for (int32 Child : Node.Children) Satisfied += Visit(Child) ? 1 : 0;
			return Satisfied >= (Node.Op == EGuLiShipRuleOp::All ? Node.Children.Num()
				: Node.Op == EGuLiShipRuleOp::Any ? 1 : Node.Required);
		}
	};
	const bool bSatisfied = Visit(Expression.Root);
	if (!bSatisfied && Failure) *Failure = TEXT("Required choices, route level, or installed capability are unavailable.");
	return bSatisfied;
}

bool UGuLiShipBuildCatalog::Resolve(const FGuLiShipCompiledBuildRules& Rules,
	const FGuLiShipRunBuildState& State, FGuLiShipResolvedBuild& Out, FString& Error) const
{
	using namespace GuLiShipBuildRules;
	Out = {}; Error.Reset();
	if (!Rules.bValid) return Reject(Error, Rules.Error);
	TMap<FName, FName> ActiveByRoute;
	for (FName Id : State.ChosenNodeIds)
	{
		const int32* Index = Rules.NodeIndices.Find(Id);
		if (!Index || Out.Context.ChosenNodes.Contains(Id)) return Reject(Error, TEXT("Build history contains an unknown or duplicate choice."));
		const auto& Node = Upgrades[*Index];
		if (!Evaluate(Node.AcquisitionRequirements, Out.Context, &Error)) return false;
		if (!Node.ReplacesNodeId.IsNone() && ActiveByRoute.FindRef(Node.RouteId) != Node.ReplacesNodeId)
			return Reject(Error, TEXT("Upgrade does not replace the currently selected predecessor."));
		if (Node.ReplacesNodeId.IsNone() && ActiveByRoute.Contains(Node.RouteId))
			return Reject(Error, TEXT("Route already has a choice; an upgrade requires an explicit replacement."));
		if (const auto* Conflicts = Rules.ConflictsByRoute.Find(Node.RouteId))
			for (FName Route : *Conflicts) if (Out.Context.RouteLevels.Contains(Route))
				return Reject(Error, FString::Printf(TEXT("Route %s is locked by %s for this match."), *Node.RouteId.ToString(), *Route.ToString()));
		Out.Context.ChosenNodes.Add(Id); Out.Context.RouteLevels.Add(Node.RouteId, Node.Level);
		ActiveByRoute.Add(Node.RouteId, Id);
	}
	ActiveByRoute.GenerateValueArray(Out.ActiveNodeIds);
	Out.ActiveNodeIds.Sort(FNameLexicalLess());
	TSet<FName> Sockets, ActiveGroups, RuntimeDomains;
	for (FName Id : Out.ActiveNodeIds)
	{
		const auto& Node = Upgrades[Rules.NodeIndices.FindChecked(Id)];
		const auto& Group = Groups[Rules.GroupIndices.FindChecked(Node.GroupConfigurationId)];
		if (ActiveGroups.Contains(Group.GroupId)) return Reject(Error, TEXT("Two active routes occupy the same group identity."));
		ActiveGroups.Add(Group.GroupId); Out.GroupConfigurationIds.Add(Group.ConfigurationId);
		for (const auto& Mount : Group.Mounts)
		{
			if (Sockets.Contains(Mount.SocketName)) return Reject(Error, FString::Printf(TEXT("Active groups both occupy %s."), *Mount.SocketName.ToString()));
			Sockets.Add(Mount.SocketName);
		}
		for (const auto& Capability : Group.Capabilities)
		{
			if (Group.bExecutable)
			{
				const FName Domain = Capability.ComponentClass->GetDefaultObject<UGuLiShipCapabilityComponent>()->GetExclusiveRuntimeDomain();
				if (!Domain.IsNone())
				{
					if (RuntimeDomains.Contains(Domain)) return Reject(Error, TEXT("Runtime domain permits one capability per Ship: ") + Domain.ToString());
					RuntimeDomains.Add(Domain);
				}
			}
			Out.Context.InstalledCapabilities.Add(Capability.CapabilityId);
			for (const FGameplayTag Tag : Capability.ProvidedTags) Out.Context.InstalledCapabilities.Add(Tag.GetTagName());
		}
	}
	return true;
}

bool UGuLiShipBuildCatalog::CanChoose(const FGuLiShipCompiledBuildRules& Rules,
	const FGuLiShipRunBuildState& State, FName NodeId, FString& Error, bool bRequireExecutable) const
{
	using namespace GuLiShipBuildRules;
	if (!Rules.bValid) return Reject(Error, Rules.Error);
	const int32* Index = Rules.NodeIndices.Find(NodeId);
	if (!Index) return Reject(Error, TEXT("Unknown upgrade node."));
	const auto& Node = Upgrades[*Index];
	if (bRequireExecutable && !Groups[Rules.GroupIndices.FindChecked(Node.GroupConfigurationId)].bExecutable)
		return Reject(Error, TEXT("This component's gameplay is not implemented and is outside the reward pool."));
	FGuLiShipRunBuildState Candidate = State;
	Candidate.ChosenNodeIds.Add(NodeId);
	FGuLiShipResolvedBuild Resolved;
	return Resolve(Rules, Candidate, Resolved, Error);
}
