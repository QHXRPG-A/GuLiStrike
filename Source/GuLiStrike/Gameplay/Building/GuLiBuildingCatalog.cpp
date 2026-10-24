#include "Gameplay/Building/GuLiBuildingCatalog.h"
#include "Gameplay/Navigation/Grid/GuLiGridTypes.h"
#include "Gameplay/Resources/GuLiResourceFactoryActor.h"
#include "Components/BoxComponent.h"
#include "Gameplay/Data/Generated/GuLiStrikeBuildingsTableRows.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Gameplay/Models/GuLiModelRegistrySubsystem.h"
#include "GameFramework/Actor.h"

UGuLiBuildingDataSettings::UGuLiBuildingDataSettings()
	:BuildingsTable(FSoftObjectPath(TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeBuildings_Buildings.DT_GuLiStrikeBuildings_Buildings"))),
	Catalog(FSoftObjectPath(UGuLiBuildingCatalog::GetDefaultCatalogObjectPath())) {}

const FGuLiBuildingDefinition* UGuLiBuildingCatalog::FindDefinition(EGuLiBuildingType Type) const
{
	return Definitions.FindByPredicate([Type](const auto& D) { return D.Type == Type; });
}
const FGuLiBuildingDefinition* UGuLiBuildingCatalog::FindById(int32 Id) const
{
	return Definitions.FindByPredicate([Id](const auto& D) { return D.DefinitionId == Id; });
}
bool UGuLiBuildingCatalog::ResolveTable(bool bForceRefresh)
{
	if (bTableResolved && !bForceRefresh) return true;
	const auto* Table = GetDefault<UGuLiBuildingDataSettings>()->BuildingsTable.LoadSynchronous();
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
		D.ModelId = R.ModelId;
		D.Mesh = GuLiModels::Load<UStaticMesh>(this, R.ModelId);
		D.PresentationClass = GuLiModels::LoadClass<AActor>(this, R.ModelId);
		if (const auto* GameplayClass=GameplayClasses.Find(R.Id)) D.GameplayClass=*GameplayClass;
		D.CollisionExtent = R.CollisionExtent; D.VisualOffset = R.VisualOffset; D.MeshScale = R.MeshScale;
		// The processing factory's actual shell envelope differs from its legacy preview row.
		if (D.Category==EGuLiBuildingCategory::Factory)
			D.CollisionExtent=GetDefault<AGuLiResourceFactoryActor>()->FindComponentByClass<UBoxComponent>()->GetUnscaledBoxExtent();
		D.SourceCollisionDiameterCm=2.f*D.CollisionExtent.Size2D();
		D.FootprintCells=GuLiGrid::FootprintFromDiameter(D.SourceCollisionDiameterCm);
		D.GridDimensionScale=float(GuLiGrid::ScaleFromDiameter(D.SourceCollisionDiameterCm));
		D.CollisionExtent*=D.GridDimensionScale;D.VisualOffset*=D.GridDimensionScale;D.MeshScale*=D.GridDimensionScale;
		D.PresentationScale=.2f*D.GridDimensionScale;
		D.MaxHealth = R.MaxHealth; D.MaxShield = R.MaxShield; D.BuildLevel = R.BuildLevel;
		D.Cost.Blue = R.BlueCost; D.Cost.Red = R.RedCost; D.ConstructionWork = R.ConstructionWork;
		D.ProductionUnitId = R.ProductionUnitId; D.ProductionSeconds = R.ProductionSeconds; D.ProductionCount = R.ProductionCount;
		D.CompletionUnitTypeId = R.CompletionUnitTypeId;
		D.TransitFieldId = R.TransitFieldId; D.ShieldRadius = R.ShieldRadius; D.ShieldRechargePerSecond = R.ShieldRechargePerSecond;
		TArray<FString> Gifts; R.FirstCaptureGiftIds.ParseIntoArray(Gifts, TEXT(","));
		for (const auto& Gift : Gifts) D.FirstCaptureGiftIds.Add(FCString::Atoi(*Gift));
		if (R.Id <= 0 || R.Category < 0 || R.Category > 6 || !D.IsUsable() || R.MaxHealth <= 0
			|| (D.Category == EGuLiBuildingCategory::ConstructedUnit && D.CompletionUnitTypeId <= 0))
		{
			UE_LOG(LogTemp, Error, TEXT("Invalid building row %s"), *Name.ToString());
			return false;
		}
	}
	for (const auto& [Id,Class]:GameplayClasses)
		if (Class.IsNull() || !Resolved.ContainsByPredicate([Id](const auto& D) {return D.DefinitionId==Id;}))
		{
			UE_LOG(LogTemp, Error, TEXT("Building gameplay class references an absent definition or empty class: %d"), Id);
			return false;
		}
	Definitions = MoveTemp(Resolved);
	bTableResolved = true;
	return true;
}
bool UGuLiBuildingCatalog::IsUsable() const
{
	TSet<int32> Ids;
	for (const auto& D:Definitions) {if (!D.IsUsable() || Ids.Contains(D.DefinitionId)) return false;Ids.Add(D.DefinitionId);}
	return !Ids.IsEmpty();
}
const TCHAR* UGuLiBuildingCatalog::GetDefaultCatalogObjectPath()
{
	return TEXT("/Game/GuLiStrike/Buildings/DA_GuLiBuildingCatalog.DA_GuLiBuildingCatalog");
}
UGuLiBuildingCatalog* UGuLiBuildingCatalog::LoadDefaultCatalog()
{
	auto* Catalog = GetDefault<UGuLiBuildingDataSettings>()->Catalog.LoadSynchronous();
	if (Catalog && !Catalog->ResolveTable()) return nullptr;
	return Catalog;
}
