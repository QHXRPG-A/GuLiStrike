#include "Gameplay/CombatEffects/GuLiGroundWarningSubsystem.h"
#include "Network/GuLiNetworkSettings.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Commander/Presentation/GuLiCommanderLandscapeQuerySubsystem.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "Gameplay/Performance/GuLiPerformanceSubsystem.h"
#include "Components/DecalComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	bool SameCircle(const FGuLiGroundWarningParams& A, const FGuLiGroundWarningParams& B)
	{
		return A.Location == B.Location && A.Radius == B.Radius && A.Color == B.Color && A.Style == B.Style
			&& A.PoolGroupId == B.PoolGroupId && A.bUsesBattleClock == B.bUsesBattleClock && A.bPilotOutline == B.bPilotOutline;
	}

	void BuildPilotRim(FGuLiGroundWarningCircle& Circle, UWorld& World)
	{
		Circle.PilotRim.Reset();
		if (!Circle.Params.bPilotOutline) return;
		const FVector Center = Circle.Params.Location;
		const double Radius = Circle.Params.Radius;
		FVector2D Slope = FVector2D::ZeroVector;
		const auto* Terrain = World.GetSubsystem<UGuLiCommanderLandscapeQuerySubsystem>();
		float Height, Forward, Right;
		if (Terrain && Terrain->TryGetLandscapeHeight(FVector2D(Center), Height) && FMath::Abs(Height-Center.Z) <= 80
			&& Terrain->TryGetLandscapeHeight(FVector2D(Center.X+Radius, Center.Y), Forward)
			&& Terrain->TryGetLandscapeHeight(FVector2D(Center.X, Center.Y+Radius), Right))
			Slope = FVector2D(Forward-Height, Right-Height);
		constexpr int32 Segments = 32;
		Circle.PilotRim.Reserve(Segments + 1);
		for (int32 I = 0; I < Segments; ++I)
		{
			const double Angle = UE_TWO_PI * I / Segments;
			const FVector2D Radial(FMath::Cos(Angle), FMath::Sin(Angle));
			Circle.PilotRim.Add(Center + FVector(Radial * Radius, FVector2D::DotProduct(Slope, Radial)));
		}
		const FVector First = Circle.PilotRim[0];
		Circle.PilotRim.Add(First);
	}
}

bool UGuLiGroundWarningStyle::IsValidStyle() const
{
	return MaterialVfxId > 0 && FMath::IsFinite(WavePeriod) && WavePeriod >= 0.05f
		&& FMath::IsFinite(RingWidth) && RingWidth >= 0.001f && RingWidth <= 0.2f
		&& FMath::IsFinite(Opacity) && Opacity >= 0 && Opacity <= 1
		&& FMath::IsFinite(ProjectionDepth) && ProjectionDepth > 0 && ProjectionDepth <= 100000;
}

bool UGuLiGroundWarningSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const auto* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld() && UGuLiNetworkSettings::CanPresent(World);
}

double UGuLiGroundWarningSubsystem::WarningTime(bool bBattleClock) const
{
	if (bBattleClock)
		if (const auto* Hybrid = GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()) return Hybrid->GetLocalLogicalSeconds();
	const auto* State = GetWorld()->GetGameState();
	return State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

bool UGuLiGroundWarningSubsystem::IsTickable() const
{
	return !IsTemplate() && !Entries.IsEmpty() && GetWorld() && UGuLiNetworkSettings::CanPresent(GetWorld());
}

TStatId UGuLiGroundWarningSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UGuLiGroundWarningSubsystem, STATGROUP_Tickables);
}

bool UGuLiGroundWarningSubsystem::UpsertWarning(const FGuid WarningId, const FGuLiGroundWarningParams& Params)
{
	FGuLiPerformanceScope Timing(GetWorld(),TEXT("GroundWarning.UpsertMs"));
	if (!WarningId.IsValid() || !GetWorld() || !UGuLiNetworkSettings::CanPresent(GetWorld())) return false;
	const double Now = WarningTime(Params.bUsesBattleClock);
	if (Params.Location.ContainsNaN() || Params.Location.GetAbsMax() > 10000000 || !FMath::IsFinite(Params.Radius)
		|| Params.Radius <= 0 || Params.Radius > 100000 || !Params.Style || !Params.Style->IsValidStyle()
		|| !FMath::IsFinite(Params.Color.R) || !FMath::IsFinite(Params.Color.G) || !FMath::IsFinite(Params.Color.B) || !FMath::IsFinite(Params.Color.A)
		|| !FMath::IsFinite(Params.StartServerSeconds) || !FMath::IsFinite(Params.ExpireServerSeconds)
		|| Params.ExpireServerSeconds <= Params.StartServerSeconds || Params.ExpireServerSeconds <= Now)
	{ RemoveWarning(WarningId); return false; }
	if (auto* Existing = Entries.Find(WarningId))
	{
		if (SameCircle(Circles[Existing->Circle].Params, Params))
		{ Existing->ExpireServerSeconds = Params.ExpireServerSeconds; return true; }
		RemoveWarning(WarningId);
	}
	int32 Slot = Circles.IndexOfByPredicate([&Params](const auto& Circle) { return Circle.References > 0 && SameCircle(Circle.Params, Params); });
	if (Slot == INDEX_NONE)
	{
		auto* BaseMaterial = GuLiVfx::Load<UMaterialInterface>(this, Params.Style->MaterialVfxId);
		if (!BaseMaterial) return false;
		Slot = Circles.IndexOfByPredicate([](const auto& Circle) { return Circle.References == 0; });
		if (Slot == INDEX_NONE) Slot = Circles.AddDefaulted();
		auto& Circle = Circles[Slot];
		Circle.Params = Params;
		BuildPilotRim(Circle, *GetWorld());
		if (!Circle.Decal)
		{
			Circle.Decal = NewObject<UDecalComponent>(GetWorld()->GetWorldSettings(), NAME_None, RF_Transient);
			Circle.Decal->SetVisibility(false);
			Circle.Decal->FadeScreenSize = 0;
			Circle.Decal->RegisterComponentWithWorld(GetWorld());
		}
		// Reset every per-instance value before revealing a recycled component.
		if (!Circle.Material || Circle.Material->Parent != BaseMaterial)
			Circle.Material = UMaterialInstanceDynamic::Create(BaseMaterial, Circle.Decal);
		Circle.Material->SetVectorParameterValue(TEXT("Tint"), Params.Color);
		Circle.Material->SetScalarParameterValue(TEXT("Opacity"), Params.Style->Opacity);
		Circle.Material->SetScalarParameterValue(TEXT("WavePeriod"), Params.Style->WavePeriod);
		Circle.Material->SetScalarParameterValue(TEXT("RingWidth"), Params.Style->RingWidth);
		Circle.Material->SetScalarParameterValue(TEXT("Age"), FMath::Max(0.0, Now - Params.StartServerSeconds));
		Circle.Decal->SetDecalMaterial(Circle.Material);
		// Material's outer boundary is r=.96, so its visible radius is exactly Params.Radius.
		Circle.Decal->DecalSize = GuLiVfx::Scale(this, Params.Style->MaterialVfxId, FVector(Params.Style->ProjectionDepth, Params.Radius / .96f, Params.Radius / .96f));
		Circle.Decal->SetWorldLocationAndRotation(Params.Location, FRotator(-90, 0, 0));
		Circle.Decal->SetVisibility(Now >= Params.StartServerSeconds);
	}
	++Circles[Slot].References;
	Entries.Add(WarningId, {Slot, Params.ExpireServerSeconds});
	return true;
}

void UGuLiGroundWarningSubsystem::RemoveWarning(const FGuid WarningId)
{
	FEntry Entry;
	if (!Entries.RemoveAndCopyValue(WarningId, Entry)) return;
	auto& Circle = Circles[Entry.Circle];
	if (--Circle.References == 0)
	{
		if (Circle.Decal) Circle.Decal->SetVisibility(false);
		Circle.Params = {};
		int32 IdleComponents = 0;
		for (const auto& Item : Circles) IdleComponents += Item.References == 0 && Item.Decal ? 1 : 0;
		if (IdleComponents > 64)
		{
			if (Circle.Decal) Circle.Decal->DestroyComponent();
			Circle.Decal = nullptr; Circle.Material = nullptr;
		}
	}
}

void UGuLiGroundWarningSubsystem::Tick(float DeltaTime)
{
	FGuLiPerformanceScope Timing(GetWorld(),TEXT("GroundWarning.UpdateMs"));
	const double WorldNow = WarningTime(false), BattleNow = WarningTime(true);
	TArray<FGuid> Expired;
	for (const auto& Pair : Entries)
	{
		const double Now = Circles[Pair.Value.Circle].Params.bUsesBattleClock ? BattleNow : WorldNow;
		if (Now >= Pair.Value.ExpireServerSeconds) Expired.Add(Pair.Key);
	}
	for (const auto& Id : Expired) RemoveWarning(Id);
	for (auto& Circle : Circles)
	{
		if (Circle.References == 0 || !Circle.Decal || !Circle.Material) continue;
		const double Now = Circle.Params.bUsesBattleClock ? BattleNow : WorldNow;
		Circle.Decal->SetVisibility(Now >= Circle.Params.StartServerSeconds);
		Circle.Material->SetScalarParameterValue(TEXT("Age"), FMath::Max(0.0, Now - Circle.Params.StartServerSeconds));
	}
	if (auto* Capture=GetWorld()->GetSubsystem<UGuLiPerformanceSubsystem>())
	{
		Capture->Record(TEXT("GroundWarning.Entries"),Entries.Num());
		Capture->Record(TEXT("GroundWarning.Circles"),GetActiveCircleCount());
	}
}

int32 UGuLiGroundWarningSubsystem::GetActiveCircleCount() const
{
	int32 Count = 0;
	for (const auto& Circle : Circles) Count += Circle.References > 0 ? 1 : 0;
	return Count;
}

void UGuLiGroundWarningSubsystem::VisitPilotRims(TFunctionRef<void(TConstArrayView<FVector>, const FLinearColor&)> Visit) const
{
	if (Entries.IsEmpty()) return;
	for (const auto& Circle : Circles)
		if (Circle.References > 0 && !Circle.PilotRim.IsEmpty() && Circle.Decal && Circle.Decal->IsVisible())
			Visit(Circle.PilotRim, Circle.Params.Color);
}

void UGuLiGroundWarningSubsystem::Deinitialize()
{
	for (auto& Circle : Circles) if (Circle.Decal) Circle.Decal->DestroyComponent();
	Entries.Reset(); Circles.Reset(); Super::Deinitialize();
}
