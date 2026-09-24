#include "Gameplay/Building/GuLiBuildingCatalog.h"
#include "Gameplay/Data/Generated/GuLiStrikeBuildingsTableRows.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

const FGuLiBuildingDefinition* UGuLiBuildingCatalog::FindDefinition(EGuLiBuildingType Type) const
{
	return Definitions.FindByPredicate([Type](const auto& D) { return D.Type == Type; });
}
const FGuLiBuildingDefinition* UGuLiBuildingCatalog::FindById(int32 Id) const
{
	return Definitions.FindByPredicate([Id](const auto& D) { return D.DefinitionId == Id; });
}
bool UGuLiBuildingCatalog::ResolveTable()
{
	if (bTableResolved) return true;
	const auto* Table = LoadObject<UDataTable>(nullptr,
		TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeBuildings_Buildings.DT_GuLiStrikeBuildings_Buildings"));
	if (!Table || Table->GetRowStruct() != FGuLiStrikeBuildingsBuildingsRow::StaticStruct())
	{
		UE_LOG(LogTemp, Error, TEXT("Building table missing or wrong row structure. Import Buildings before starting the match."));
		return false;
	}
	TArray<FGuLiBuildingDefinition> Resolved;
	for (FName Name : Table->GetRowNames())
	{
		const auto& R = *Table->FindRow<FGuLiStrikeBuildingsBuildingsRow>(Name, TEXT("Building catalog"));
		auto& D = Resolved.AddDefaulted_GetRef();
		D.DefinitionId = R.Id; D.Type = EGuLiBuildingType(R.PlacementType); D.Category = EGuLiBuildingCategory(R.Category);
		D.DisplayName = FText::FromString(R.DisplayName); D.Description = FText::FromString(R.Description);
		D.Mesh = Cast<UStaticMesh>(R.Mesh.LoadSynchronous());
		D.CollisionExtent = R.CollisionExtent; D.VisualOffset = R.VisualOffset; D.MeshScale = R.MeshScale;
		D.MaxHealth = R.MaxHealth; D.MaxShield = R.MaxShield; D.BuildLevel = R.BuildLevel;
		D.Cost.Blue = R.BlueCost; D.Cost.Red = R.RedCost; D.ConstructionWork = R.ConstructionWork;
		D.ProductionUnitId = R.ProductionUnitId; D.ProductionSeconds = R.ProductionSeconds; D.ProductionCount = R.ProductionCount;
		D.TransitFieldId = R.TransitFieldId; D.ShieldRadius = R.ShieldRadius; D.ShieldRechargePerSecond = R.ShieldRechargePerSecond;
		TArray<FString> Gifts; R.FirstCaptureGiftIds.ParseIntoArray(Gifts, TEXT(","));
		for (const auto& Gift : Gifts) D.FirstCaptureGiftIds.Add(FCString::Atoi(*Gift));
		if (R.Id <= 0 || R.Category < 0 || R.Category > 5 || !D.IsUsable() || R.MaxHealth <= 0)
		{
			UE_LOG(LogTemp, Error, TEXT("Invalid building row %s"), *Name.ToString());
			return false;
		}
	}
	Definitions = MoveTemp(Resolved);
	bTableResolved = true;
	return true;
}
bool UGuLiBuildingCatalog::IsUsable() const
{
	return !Definitions.IsEmpty()
		&& Definitions.ContainsByPredicate([](const auto& D) { return D.Type == EGuLiBuildingType::MissileTurret; });
}
const TCHAR* UGuLiBuildingCatalog::GetDefaultCatalogObjectPath()
{
	return TEXT("/Game/GuLiStrike/Buildings/DA_GuLiBuildingCatalog.DA_GuLiBuildingCatalog");
}
UGuLiBuildingCatalog* UGuLiBuildingCatalog::LoadDefaultCatalog()
{
	auto* Catalog = LoadObject<UGuLiBuildingCatalog>(nullptr, GetDefaultCatalogObjectPath());
	if (Catalog && !Catalog->ResolveTable()) return nullptr;
	return Catalog;
}
