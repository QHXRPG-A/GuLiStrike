// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Data/GuLiCommanderSoldierResolver.h"
#include "Gameplay/Navigation/Grid/GuLiGridTypes.h"
#include "Gameplay/Data/GuLiAvoidanceCatalog.h"
#include "Gameplay/Data/GuLiUnitDataSubsystem.h"
#include "StateTree.h"

#include "Gameplay/Data/Generated/GuLiStrikeCommanderTableRows.h"
#include "GuLiStrike.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Gameplay/Presentation/GuLiVATAnimation.h"
#include "Gameplay/Models/GuLiModelRegistrySubsystem.h"

namespace GuLiCommanderSoldierResolverPrivate
{
	// FMassMoveTargetFragment stores desired speed in FMassInt16Real (1 cm precision).
	constexpr float MaximumReasonableMovementSpeed = static_cast<float>(MAX_int16);
	constexpr float MaximumReasonableCombatValue = 1000000.0f;

	void LogWarningOnce(const FName Key, const FString& Message)
	{
		static TSet<FName> LoggedWarnings;
		if (LoggedWarnings.Contains(Key))
		{
			return;
		}

		LoggedWarnings.Add(Key);
		UE_LOG(LogGuLiStrike, Warning, TEXT("Commander Soldier data: %s"), *Message);
	}

	float ResolveFiniteRange(
		const float Candidate,
		const float Minimum,
		const float Maximum,
		const float Fallback,
		const FName WarningKey,
		const TCHAR* FieldName,
		bool& bInOutEntireDefinitionFromDataTable)
	{
		if (FMath::IsFinite(Candidate) && Candidate >= Minimum && Candidate <= Maximum)
		{
			return Candidate;
		}

		LogWarningOnce(
			WarningKey,
			FString::Printf(
				TEXT("invalid %s value %.9g; using fallback %.9g."),
				FieldName,
				Candidate,
				Fallback));
		bInOutEntireDefinitionFromDataTable = false;
		return Fallback;
	}
}

FGuLiSoldierDefinition FGuLiCommanderSoldierResolver::MakeFallbackDefinition()
{
	using namespace GuLiCommanderSoldierResolverPrivate;

	FGuLiSoldierDefinition Definition;
	Definition.ModelId = GuLiModelIds::DefaultSoldier;
	Definition.Model = GuLiModels::Load<UStaticMesh>(nullptr, Definition.ModelId);
	FGuLiStrikeModelsModelsRow Model;
	if (UGuLiModelRegistrySubsystem::Query(nullptr, Definition.ModelId, Model))
		Definition.VATDefinition = Cast<UGuLiVATDefinition>(Model.VATDefinition.LoadSynchronous());
	if (!Definition.Model)
	{
		LogWarningOnce(
			TEXT("FallbackModelMissing"),
			TEXT("fallback model ID could not be loaded; compile and import the model catalogue."));
	}
	return Definition;
}

FGuLiSoldierDefinition FGuLiCommanderSoldierResolver::Resolve(
	const UDataTable* DataTable,
	const FName RowName,
	const FGuLiSoldierDefinition& Fallback)
{
	bool bIgnoredEntireDefinitionFromDataTable = false;
	return Resolve(DataTable, RowName, Fallback, bIgnoredEntireDefinitionFromDataTable);
}

FGuLiSoldierDefinition FGuLiCommanderSoldierResolver::Resolve(
	const UDataTable* DataTable,
	const FName RowName,
	const FGuLiSoldierDefinition& Fallback,
	bool& bOutEntireDefinitionFromDataTable, const FGuLiUnitAvoidanceProfile* Avoidance)
{
	using namespace GuLiCommanderSoldierResolverPrivate;
	bOutEntireDefinitionFromDataTable = false;

	if (!DataTable)
	{
		LogWarningOnce(TEXT("MissingTable"), TEXT("Soldier DataTable is missing; using C++ fallback."));
		return Fallback;
	}

	if (DataTable->GetRowStruct() != FGuLiStrikeCommanderSoldiersRow::StaticStruct())
	{
		LogWarningOnce(
			TEXT("WrongRowStruct"),
			TEXT("Soldier DataTable has the wrong row struct; using C++ fallback."));
		return Fallback;
	}

	if (RowName.IsNone())
	{
		LogWarningOnce(TEXT("MissingRowName"), TEXT("default Soldier row name is None; using C++ fallback."));
		return Fallback;
	}

	const FGuLiStrikeCommanderSoldiersRow* Row =
		DataTable->FindRow<FGuLiStrikeCommanderSoldiersRow>(RowName, TEXT("CommanderSoldierResolver"), false);
	if (!Row)
	{
		LogWarningOnce(
			FName(*FString::Printf(TEXT("MissingRow_%s"), *RowName.ToString())),
			FString::Printf(TEXT("row '%s' is missing; using C++ fallback."), *RowName.ToString()));
		return Fallback;
	}

	return ResolveRow(Row, Fallback, bOutEntireDefinitionFromDataTable, Avoidance);
}

FGuLiSoldierDefinition FGuLiCommanderSoldierResolver::Resolve(
	const UDataTable* DataTable,
	const FName RowName)
{
	return Resolve(DataTable, RowName, MakeFallbackDefinition());
}

FGuLiSoldierDefinition FGuLiCommanderSoldierResolver::ResolveRow(
	const FGuLiStrikeCommanderSoldiersRow* Row,
	const FGuLiSoldierDefinition& Fallback)
{
	bool bIgnoredEntireDefinitionFromDataTable = false;
	return ResolveRow(Row, Fallback, bIgnoredEntireDefinitionFromDataTable);
}

FGuLiSoldierDefinition FGuLiCommanderSoldierResolver::ResolveRow(
	const FGuLiStrikeCommanderSoldiersRow* Row,
	const FGuLiSoldierDefinition& Fallback,
	bool& bOutEntireDefinitionFromDataTable, const FGuLiUnitAvoidanceProfile* Avoidance)
{
	using namespace GuLiCommanderSoldierResolverPrivate;
	bOutEntireDefinitionFromDataTable = false;

	if (!Row)
	{
		LogWarningOnce(TEXT("NullRow"), TEXT("Soldier row is null; using C++ fallback."));
		return Fallback;
	}

	FGuLiSoldierDefinition Resolved = Fallback;
	Resolved.ModelId = Row->ModelId;
	FGuLiStrikeModelsModelsRow ModelDefinition;
	if (!UGuLiModelRegistrySubsystem::Query(nullptr, Row->ModelId, ModelDefinition)) return Resolved;
	Resolved.DisplayName = FText::FromString(Row->DisplayName);
	if (Row->MovementLayer < 0 || Row->MovementLayer > 1
		|| !GuLiUnitMovement::IsValid(static_cast<EGuLiUnitMovementLayer>(Row->MovementLayer), Row->AltitudeMeters * 100.f))
	{
		LogWarningOnce(FName(*FString::Printf(TEXT("InvalidMovementLayer_%d"), Row->Id)),
			FString::Printf(TEXT("Soldiers id %d: Ground requires 0m; LowAir requires 30..99m."), Row->Id));
		return Resolved;
	}
	Resolved.MovementLayer = static_cast<EGuLiUnitMovementLayer>(Row->MovementLayer);
	Resolved.AltitudeCentimeters = Row->AltitudeMeters * 100.f;
	Resolved.bConstructionOnly = Row->bConstructionOnly;
	Resolved.FacingPolicy = Row->FacingPolicy == 1
		? EGuLiSoldierFacingPolicy::FixedSpawnYaw : EGuLiSoldierFacingPolicy::FaceVelocity;
	bOutEntireDefinitionFromDataTable = true;
	if (Row->Id > 0 && Row->Id <= MAX_uint16)
	{
		Resolved.UnitTypeId = static_cast<uint16>(Row->Id);
	}
	else
	{
		bOutEntireDefinitionFromDataTable = false;
		LogWarningOnce(TEXT("InvalidUnitTypeId"), FString::Printf(
			TEXT("invalid Soldier Id %d; using fallback %u."), Row->Id, Fallback.UnitTypeId));
	}
	Resolved.MovementSpeedCmPerSecond = ResolveFiniteRange(
		Row->MovementSpeedCmPerSecond,
		UE_SMALL_NUMBER,
		MaximumReasonableMovementSpeed,
		Fallback.MovementSpeedCmPerSecond,
		TEXT("InvalidMovementSpeed"),
		TEXT("MovementSpeedCmPerSecond"),
		bOutEntireDefinitionFromDataTable);


    // Runtime callers pass the match cache. Standalone editor/manifest callers
    // resolve authored input here; no tick path calls this row resolver.
    FGuLiAvoidanceCatalog Authored;
    if (!Avoidance)
    {
        const auto* Soldiers=GetDefault<UGuLiUnitDataSettings>()->SoldierDataTable.LoadSynchronous();
        TArray<uint16> Types;FString Error;
        if (Soldiers && Soldiers->GetRowStruct()==FGuLiStrikeCommanderSoldiersRow::StaticStruct())
            for (const auto Name:Soldiers->GetRowNames())
                if (const auto* Unit=Soldiers->FindRow<FGuLiStrikeCommanderSoldiersRow>(Name,TEXT("AvoidanceInput")))
                    Types.Add(uint16(Unit->Id));
        if (!FGuLiAvoidanceCatalog::Load(Types,Authored,Error))
        {bOutEntireDefinitionFromDataTable=false;LogWarningOnce(TEXT("InvalidAvoidanceInput"),Error);return Resolved;}
        Avoidance=Authored.Units.Find(Resolved.UnitTypeId);
    }
    if (!Avoidance) {bOutEntireDefinitionFromDataTable=false;return Resolved;}
    Resolved.MassAvoidanceRadiusCm=Avoidance->Solver.Radius;
    Resolved.GridReferenceDiameterCm=Avoidance->GridReferenceDiameterCm;

	if (FMath::IsFinite(Row->MaxHealth) && Row->MaxHealth > 0.0f
		&& Row->MaxHealth <= MaximumSupportedHealth)
	{
		Resolved.MaxHealth = Row->MaxHealth;
	}
	else
	{
		bOutEntireDefinitionFromDataTable = false;
		LogWarningOnce(
			TEXT("InvalidMaxHealth"),
			FString::Printf(
				TEXT("invalid MaxHealth value %.9g; using fallback %.9g (valid range: finite, >0, <=1e9)."),
				Row->MaxHealth,
				Fallback.MaxHealth));
	}

	Resolved.Defense = ResolveFiniteRange(
		Row->Defense,
		0.0f,
		MaximumReasonableCombatValue,
		Fallback.Defense,
		TEXT("InvalidDefense"),
		TEXT("Defense"),
		bOutEntireDefinitionFromDataTable);

	Resolved.StateTreeAsset = Cast<UStateTree>(Row->StateTreeAsset.LoadSynchronous());
	Resolved.ActorClass = Row->ActorClass.LoadSynchronous();
	Resolved.PresentationClass = ModelDefinition.ResourceType == TEXT("PresentationClass")
		? GuLiModels::LoadClass<AActor>(nullptr, Row->ModelId) : nullptr;
	if (!FMath::IsFinite(Row->PresentationScale) || Row->PresentationScale <= 0.0f)
	{
		bOutEntireDefinitionFromDataTable = false;
		return Resolved;
	}
	Resolved.PresentationScale = Row->PresentationScale;
	Resolved.FootprintCells=GuLiGrid::FootprintFromDiameter(Resolved.GridReferenceDiameterCm);
	Resolved.GridDimensionScale=float(GuLiGrid::ScaleFromDiameter(Resolved.GridReferenceDiameterCm));
	Resolved.PresentationScale*=Resolved.GridDimensionScale;
	Resolved.bSummonOnly = Row->bSummonOnly;
	Resolved.VATDefinition = Cast<UGuLiVATDefinition>(ModelDefinition.VATDefinition.LoadSynchronous());
	if (!ModelDefinition.VATDefinition.IsNull() && (!Resolved.VATDefinition || !Resolved.VATDefinition->IsValidDefinition()))
	{
		bOutEntireDefinitionFromDataTable = false;
		LogWarningOnce(TEXT("InvalidVATDefinition"), TEXT("Authored VAT definition is missing or invalid."));
	}
	if (!Row->ActorClass.IsNull())
	{
		Resolved.Model = nullptr;
		bOutEntireDefinitionFromDataTable &= Resolved.ActorClass
			&& Resolved.ActorClass->IsChildOf(AActor::StaticClass())
			&& Resolved.PresentationClass && Resolved.PresentationClass->IsChildOf(AActor::StaticClass());
		return Resolved;
	}
	UObject* LoadedModel = GuLiModels::Load<UStaticMesh>(nullptr, Row->ModelId);
	if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(LoadedModel))
	{
		Resolved.Model = StaticMesh;
		Resolved.MechanicalAnimation = FGuLiMechanicalAnimationConfig::FromStaticMesh(StaticMesh, Resolved.PresentationScale);
	}
	else
	{
		bOutEntireDefinitionFromDataTable = false;
		const FString SourcePath = ModelDefinition.ResourcePath.ToSoftObjectPath().ToString();
		LogWarningOnce(
			TEXT("InvalidModelType"),
			FString::Printf(
				TEXT("ModelAsset '%s' is missing or is not a UStaticMesh; keeping the current proxy fallback."),
				*SourcePath));
	}

	return Resolved;
}
