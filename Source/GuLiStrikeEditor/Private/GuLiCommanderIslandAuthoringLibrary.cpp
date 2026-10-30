#include "GuLiCommanderIslandAuthoringLibrary.h"

#include "Gameplay/Stronghold/GuLiOutpostPresentationComponent.h"
#include "Gameplay/Models/GuLiModelRegistrySubsystem.h"
#include "Engine/StaticMesh.h"
#include "Dom/JsonObject.h"
#include "Materials/MaterialInterface.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FString UGuLiCommanderIslandAuthoringLibrary::ValidateOutpostPresentationAssets()
{
	const auto& Config = *GetDefault<UGuLiOutpostPresentationSettings>();
	UStaticMesh* Mesh = GuLiModels::Load<UStaticMesh>(nullptr,Config.ModelId);
	UMaterialInterface* Body = Config.BodyMaterial.LoadSynchronous();
	UMaterialInterface* Halo = Config.HaloMaterial.LoadSynchronous();
	if (!Mesh || !Body || !Halo) return TEXT("The configured outpost mesh, glow material and halo material are required.");
	FGuLiOutpostVisualLayout Layout;
	FString Warning;
	if (!UGuLiOutpostPresentationComponent::ResolveVisualLayout(Mesh, Layout, Warning) || !Warning.IsEmpty()) return Warning;
	if (!FMath::IsFinite(Config.HoverHeightCm) || !FMath::IsFinite(Config.FloatAmplitudeCm)
		|| !FMath::IsFinite(Config.FloatPeriodSeconds)
		|| Config.HoverHeightCm <= Config.FloatAmplitudeCm || Config.FloatAmplitudeCm < 0.0f
		|| Config.FloatPeriodSeconds <= 0.0f)
		return TEXT("Outpost hover height must exceed its nonnegative amplitude; its period must be finite and positive.");
	if (!FMath::IsFinite(Config.SelfRotationPeriodSeconds) || Config.SelfRotationPeriodSeconds <= 0.0f)
		return TEXT("Outpost self-rotation period must be finite and positive.");
	FLinearColor Color;
	float Scalar;
	if (!Body->GetVectorParameterValue(FMaterialParameterInfo(TEXT("GlowColor")), Color)
		|| !Body->GetScalarParameterValue(FMaterialParameterInfo(TEXT("GlowIntensity")), Scalar)
		|| !Halo->GetVectorParameterValue(FMaterialParameterInfo(TEXT("GlowColor")), Color)
		|| !Halo->GetScalarParameterValue(FMaterialParameterInfo(TEXT("HaloOpacity")), Scalar))
		return TEXT("Outpost materials must expose GlowColor, GlowIntensity and HaloOpacity.");
	return FString();
}

FString UGuLiCommanderIslandAuthoringLibrary::GetOutpostPresentationLayoutJson()
{
	const auto& Config = *GetDefault<UGuLiOutpostPresentationSettings>();
	UStaticMesh* Mesh = GuLiModels::Load<UStaticMesh>(nullptr, Config.ModelId);
	FGuLiOutpostVisualLayout Layout;
	FString Warning;
	const bool bSuccess = UGuLiOutpostPresentationComponent::ResolveVisualLayout(Mesh, Layout, Warning);
	const auto Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bSuccess);
	Result->SetStringField(TEXT("warning"), Warning);
	Result->SetBoolField(TEXT("using_reference_size"), Layout.bUsesReferenceSize);
	Result->SetBoolField(TEXT("always_floating"), true);
	Result->SetNumberField(TEXT("model_id"), Config.ModelId);
	Result->SetNumberField(TEXT("reference_building_id"), Config.ReferenceBuildingId);
	Result->SetNumberField(TEXT("size_multiplier"), Config.ReferenceSizeMultiplier);
	Result->SetStringField(TEXT("mesh"), Mesh ? Mesh->GetPathName() : FString());
	Result->SetStringField(TEXT("reference_mesh"), Layout.ReferenceMesh ? Layout.ReferenceMesh->GetPathName() : FString());
	const auto SetVector = [&Result](const TCHAR* Name, const FVector& Value)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (int32 Axis = 0; Axis < 3; ++Axis) Values.Add(MakeShared<FJsonValueNumber>(Value[Axis]));
		Result->SetArrayField(Name, Values);
	};
	SetVector(TEXT("mesh_scale"), Layout.MeshScale);
	SetVector(TEXT("ground_offset_cm"), Layout.GroundOffset);
	SetVector(TEXT("size_cm"), Layout.SizeCm);
	SetVector(TEXT("reference_size_cm"), Layout.ReferenceSizeCm);
	SetVector(TEXT("reference_mesh_scale"), Layout.ReferenceMeshScale);
	SetVector(TEXT("reference_ground_offset_cm"), Layout.ReferenceGroundOffset);
	Result->SetNumberField(TEXT("hover_height_cm"), Config.HoverHeightCm);
	Result->SetNumberField(TEXT("float_amplitude_cm"), Config.FloatAmplitudeCm);
	Result->SetNumberField(TEXT("float_period_seconds"), Config.FloatPeriodSeconds);
	Result->SetNumberField(TEXT("self_rotation_period_seconds"), Config.SelfRotationPeriodSeconds);
	FString Json;
	FJsonSerializer::Serialize(Result, TJsonWriterFactory<>::Create(&Json));
	return Json;
}
