#include "GuLiCommanderIslandAuthoringLibrary.h"

#include "Gameplay/Stronghold/GuLiOutpostPresentationComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

FString UGuLiCommanderIslandAuthoringLibrary::ValidateOutpostPresentationAssets()
{
	const auto& Config = *GetDefault<UGuLiOutpostPresentationSettings>();
	UStaticMesh* Mesh = Config.Mesh.LoadSynchronous();
	UMaterialInterface* Body = Config.BodyMaterial.LoadSynchronous();
	UMaterialInterface* Halo = Config.HaloMaterial.LoadSynchronous();
	if (!Mesh || !Body || !Halo) return TEXT("The configured outpost mesh, glow material and halo material are required.");
	if (Mesh->GetBounds().BoxExtent.Z <= 0.0 || Config.ModelHeightCm != 1000.0f
		|| Config.FloatAmplitudeCm != 5000.0f || Config.FloatPeriodSeconds != 5.0f || Config.LandingSeconds != 0.5f)
		return TEXT("Outpost dimensions and motion must match the approved 10m / +/-50m / 5s contract.");
	FLinearColor Color;
	float Scalar;
	if (!Body->GetVectorParameterValue(FMaterialParameterInfo(TEXT("GlowColor")), Color)
		|| !Body->GetScalarParameterValue(FMaterialParameterInfo(TEXT("GlowIntensity")), Scalar)
		|| !Halo->GetVectorParameterValue(FMaterialParameterInfo(TEXT("GlowColor")), Color)
		|| !Halo->GetScalarParameterValue(FMaterialParameterInfo(TEXT("HaloOpacity")), Scalar))
		return TEXT("Outpost materials must expose GlowColor, GlowIntensity and HaloOpacity.");
	return FString();
}
