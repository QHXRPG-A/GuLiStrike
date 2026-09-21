#include "Gameplay/CombatEffects/GuLiGroundWarningSubsystem.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "Components/DecalComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	bool SameCircle(const FGuLiGroundWarningParams& A, const FGuLiGroundWarningParams& B)
	{
		return A.Location == B.Location && A.Radius == B.Radius && A.Color == B.Color && A.Style == B.Style;
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
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld() && World->GetNetMode() != NM_DedicatedServer;
}

double UGuLiGroundWarningSubsystem::ServerTime() const
{
	const auto* State = GetWorld()->GetGameState();
	return State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

bool UGuLiGroundWarningSubsystem::IsTickable() const
{
	return !IsTemplate() && !Entries.IsEmpty() && GetWorld() && GetWorld()->GetNetMode() != NM_DedicatedServer;
}

TStatId UGuLiGroundWarningSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UGuLiGroundWarningSubsystem, STATGROUP_Tickables);
}

bool UGuLiGroundWarningSubsystem::UpsertWarning(const FGuid WarningId, const FGuLiGroundWarningParams& Params)
{
	if (!WarningId.IsValid() || !GetWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer) return false;
	if (Params.Location.ContainsNaN() || Params.Location.GetAbsMax() > 10000000 || !FMath::IsFinite(Params.Radius)
		|| Params.Radius <= 0 || Params.Radius > 100000 || !Params.Style || !Params.Style->IsValidStyle()
		|| !FMath::IsFinite(Params.Color.R) || !FMath::IsFinite(Params.Color.G) || !FMath::IsFinite(Params.Color.B) || !FMath::IsFinite(Params.Color.A)
		|| !FMath::IsFinite(Params.StartServerSeconds) || !FMath::IsFinite(Params.ExpireServerSeconds)
		|| Params.ExpireServerSeconds <= Params.StartServerSeconds || Params.ExpireServerSeconds <= ServerTime())
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
		Circle.Material->SetScalarParameterValue(TEXT("Age"), FMath::Max(0.0, ServerTime() - Params.StartServerSeconds));
		Circle.Decal->SetDecalMaterial(Circle.Material);
		// Material's outer boundary is r=.96, so its visible radius is exactly Params.Radius.
		Circle.Decal->DecalSize = GuLiVfx::Scale(this, Params.Style->MaterialVfxId, FVector(Params.Style->ProjectionDepth, Params.Radius / .96f, Params.Radius / .96f));
		Circle.Decal->SetWorldLocationAndRotation(Params.Location, FRotator(-90, 0, 0));
		Circle.Decal->SetVisibility(ServerTime() >= Params.StartServerSeconds);
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
	const double Now = ServerTime();
	TArray<FGuid> Expired;
	for (const auto& Pair : Entries) if (Now >= Pair.Value.ExpireServerSeconds) Expired.Add(Pair.Key);
	for (const auto& Id : Expired) RemoveWarning(Id);
	for (auto& Circle : Circles)
	{
		if (Circle.References == 0 || !Circle.Decal || !Circle.Material) continue;
		Circle.Decal->SetVisibility(Now >= Circle.Params.StartServerSeconds);
		Circle.Material->SetScalarParameterValue(TEXT("Age"), FMath::Max(0.0, Now - Circle.Params.StartServerSeconds));
	}
}

int32 UGuLiGroundWarningSubsystem::GetActiveCircleCount() const
{
	int32 Count = 0;
	for (const auto& Circle : Circles) Count += Circle.References > 0 ? 1 : 0;
	return Count;
}

void UGuLiGroundWarningSubsystem::Deinitialize()
{
	for (auto& Circle : Circles) if (Circle.Decal) Circle.Decal->DestroyComponent();
	Entries.Reset(); Circles.Reset(); Super::Deinitialize();
}
