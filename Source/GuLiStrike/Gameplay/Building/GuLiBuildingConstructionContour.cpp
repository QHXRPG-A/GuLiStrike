#include "Gameplay/Building/GuLiBuildingConstructionVisualComponent.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Gameplay/Presentation/GuLiUnitRenderPolicy.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "GameFramework/Actor.h"

namespace
{
	constexpr int32 MaxContourSamples = 256;
	double Cross2(const FVector& A, const FVector& B) { return A.X * B.Y - A.Y * B.X; }
}

void UGuLiBuildingConstructionVisualComponent::ResolveShape()
{
	Shape = nullptr; WorldContours.Reset(); ContourLengths.Reset();
	EffectPoints.Reset(); EffectTangents.Reset(); EffectLengths.Reset();
	++ShapeRevision;
	if (GetOwner()->Implements<UGuLiConstructionShapeProvider>())
		Shape = IGuLiConstructionShapeProvider::Execute_GetConstructionShapeOverride(GetOwner());
	if (!Shape)
		if (auto* Catalog = GetDefault<UGuLiBuildingConstructionSettings>()->ShapeCatalog.LoadSynchronous())
			if (auto* Found = Catalog->Shapes.Find(Lifecycle->GetState().DefinitionId)) Shape = *Found;
	if (!Shape || !Shape->IsUsable())
	{
		if (!bShapeWarningLogged)
			UE_LOG(LogTemp, Warning, TEXT("[ConstructionShape] Missing valid baked footprint for %s, definition %d. Hologram/rise remain active."),
				*GetNameSafe(GetOwner()), Lifecycle->GetState().DefinitionId);
		bShapeWarningLogged = true; Shape = nullptr; return;
	}
	bShapeWarningLogged = false;
	const FVector Ground = Lifecycle->GetGroundLocation();
	const FTransform Transform(GetOwner()->GetActorQuat(), Ground, GetOwner()->GetActorScale3D());
	ModelTop = float(Transform.TransformPosition(FVector(0, 0, Shape->Bounds.Max.Z)).Z);
	EffectBounds = FBox(ForceInit);
	int32 Edges = 0;
	for (const auto& Contour : Shape->Contours)
	{
		auto& Points = WorldContours.AddDefaulted_GetRef();
		for (const FVector2D& XY : Contour.Points)
		{
			const FVector P = Transform.TransformPosition(FVector(XY, 0));
			Points.Add(P); EffectBounds += P - Ground;
		}
		float Length = 0;
		for (int32 I = 0; I < Points.Num(); ++I) Length += FVector::Dist2D(Points[I], Points[(I + 1) % Points.Num()]);
		ContourLengths.Add(Length);
		if (!Contour.bHole) Edges += Points.Num();
	}
	// Never join across a corner or between islands. Increase spacing to respect the shared pool budget.
	float Spacing = FMath::Max(1.f, GetDefault<UGuLiBuildingConstructionSettings>()->ContourSampleSpacing);
	for (int32 Attempt = 0; Attempt < 32; ++Attempt)
	{
		int32 Count = 0;
		for (int32 L = 0; L < WorldContours.Num(); ++L)
			if (!Shape->Contours[L].bHole)
				for (int32 E = 0; E < WorldContours[L].Num(); ++E)
					Count += FMath::Max(1, FMath::CeilToInt(FVector::Dist2D(WorldContours[L][E], WorldContours[L][(E + 1) % WorldContours[L].Num()]) / Spacing));
		if (Count <= MaxContourSamples) break;
		Spacing *= 1.25f;
	}
	if (Edges > MaxContourSamples)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ConstructionShape] %s exceeds %d outer edges. Simplify its footprint."), *Shape->GetPathName(), MaxContourSamples);
		Shape = nullptr; WorldContours.Reset(); ContourLengths.Reset(); return;
	}
	for (int32 L = 0; L < WorldContours.Num(); ++L)
	{
		if (Shape->Contours[L].bHole) continue;
		const auto& Points = WorldContours[L];
		for (int32 E = 0; E < Points.Num(); ++E)
		{
			const FVector A = Points[E], B = Points[(E + 1) % Points.Num()];
			const float Length = FVector::Dist2D(A, B);
			if (Length <= .01f) continue;
			const int32 Count = FMath::Max(1, FMath::CeilToInt(Length / Spacing));
			for (int32 I = 0; I < Count; ++I)
			{
				EffectPoints.Add(FMath::Lerp(A, B, (I + .5f) / Count) - Ground);
				EffectTangents.Add((B - A).GetSafeNormal());
				EffectLengths.Add(Length / Count);
			}
		}
	}
	EffectColumnHeight = FMath::Clamp((ModelTop - float(Ground.Z)) * .8f, 120.f, 600.f);
	EffectBounds.Min.Z = -25;
	EffectBounds.Max.Z = FMath::Max(0.f, ModelTop - float(Ground.Z)) + EffectColumnHeight + 150;
	EffectBounds = EffectBounds.ExpandBy(100);
}

float UGuLiBuildingConstructionVisualComponent::GetConstructionHeight() const
{
	if (!Lifecycle) return 0;
	return FMath::Max(float(Lifecycle->GetGroundLocation().Z) + 2.f, ModelTop - RiseHeight * (1.f - DisplayedProgress) + 2.f);
}

TArray<FVector> UGuLiBuildingConstructionVisualComponent::GetConstructionContour(int32 Contour) const
{
	if (!bConstructing || !WorldContours.IsValidIndex(Contour)) return {};
	auto Points = WorldContours[Contour];
	for (FVector& P : Points) P.Z = GetConstructionHeight();
	return Points;
}

bool UGuLiBuildingConstructionVisualComponent::SampleConstructionContour(int32 Contour, float Distance, FVector& OutPoint) const
{
	if (!bConstructing || !WorldContours.IsValidIndex(Contour) || ContourLengths[Contour] <= .01f) return false;
	const auto& Points = WorldContours[Contour];
	Distance = FMath::Fmod(Distance, ContourLengths[Contour]);
	if (Distance < 0) Distance += ContourLengths[Contour];
	for (int32 I = 0; I < Points.Num(); ++I)
	{
		const FVector A = Points[I], B = Points[(I + 1) % Points.Num()];
		const float Length = FVector::Dist2D(A, B);
		if (Distance <= Length || I == Points.Num() - 1)
		{
			OutPoint = FMath::Lerp(A, B, FMath::Clamp(Distance / FMath::Max(.001f, Length), 0.f, 1.f));
			OutPoint.Z = GetConstructionHeight(); return true;
		}
		Distance -= Length;
	}
	return false;
}

bool UGuLiBuildingConstructionVisualComponent::IsContourPointVisible(const FVector& From, const FVector& Point) const
{
	const FVector Ray = Point - From;
	for (int32 L = 0; L < WorldContours.Num(); ++L)
	{
		if (Shape->Contours[L].bHole) continue;
		const auto& P = WorldContours[L];
		for (int32 I = 0; I < P.Num(); ++I)
		{
			const FVector Edge = P[(I + 1) % P.Num()] - P[I];
			const double Denom = Cross2(Ray, Edge);
			if (FMath::Abs(Denom) < 1.e-8) continue;
			const double T = Cross2(P[I] - From, Edge) / Denom, U = Cross2(P[I] - From, Ray) / Denom;
			if (T > .001 && T < .999 && U >= 0 && U <= 1) return false;
		}
	}
	return true;
}

bool UGuLiBuildingConstructionVisualComponent::FindNearestConstructionSpan(FVector From, FGuLiConstructionSpan& Out) const
{
	Out = {};
	if (!Shape || !bConstructing) return false;
	double Best = TNumericLimits<double>::Max();
	float Center = 0;
	for (int32 L = 0; L < WorldContours.Num(); ++L)
	{
		if (Shape->Contours[L].bHole) continue;
		const auto& P = WorldContours[L];
		float Passed = 0;
		for (int32 E = 0; E < P.Num(); ++E)
		{
			FVector A = P[E], B = P[(E + 1) % P.Num()]; A.Z = B.Z = From.Z;
			const FVector Near = FMath::ClosestPointOnSegment(From, A, B);
			const double D = FVector::DistSquared2D(Near, From);
			if (D < Best && IsContourPointVisible(From, Near))
			{
				Best = D; Out.Contour = L; Center = Passed + FVector::Dist2D(A, Near);
			}
			Passed += FVector::Dist2D(A, B);
		}
	}
	if (Out.Contour == INDEX_NONE) return false;
	// Extend over short circular segments, but stop before a hidden edge of a concavity.
	const float Half = FMath::Min(120.f, ContourLengths[Out.Contour] * .12f);
	float Distances[2] = {0, 0};
	for (int32 Side = 0; Side < 2; ++Side)
		for (int32 Step = 1; Step <= 32; ++Step)
		{
			const float Distance = Half * Step / 32.f;
			FVector P; SampleConstructionContour(Out.Contour, Center + (Side ? Distance : -Distance), P);
			if (!IsContourPointVisible(From, P)) break;
			Distances[Side] = Distance;
		}
	Out.Start = Center - Distances[0]; Out.Length = Distances[0] + Distances[1]; Out.Revision = ShapeRevision;
	return Out.Length > .1f;
}

TArray<FVector> UGuLiBuildingConstructionVisualComponent::GetRoofContour() const { return GetConstructionContour(); }
int32 UGuLiBuildingConstructionVisualComponent::FindNearestRoofEdge(const FVector& Location) const
{
	const auto P = GetConstructionContour(); int32 Best = INDEX_NONE; double D = TNumericLimits<double>::Max();
	for (int32 I = 0; I < P.Num(); ++I)
	{
		const double Candidate = FVector::DistSquared2D(Location, FMath::ClosestPointOnSegment(Location, P[I], P[(I + 1) % P.Num()]));
		if (Candidate < D) { Best = I; D = Candidate; }
	}
	return Best;
}
bool UGuLiBuildingConstructionVisualComponent::SampleRoofEdge(int32 Edge, float Fraction, FVector& Out) const
{
	const auto P = GetConstructionContour(); if (!P.IsValidIndex(Edge)) return false;
	Out = FMath::Lerp(P[Edge], P[(Edge + 1) % P.Num()], FMath::Clamp(Fraction, 0.f, 1.f)); return true;
}

void UGuLiBuildingConstructionVisualComponent::ConfigureEffect(UNiagaraComponent& Effect, bool bCompletion)
{
	Effect.SetCastShadow(false); Effect.SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GuLiUnitRenderPolicy::ApplyReflectionExclusions(Effect);
	Effect.SetSystemFixedBounds(EffectBounds);
	TArray<FVector> Points = EffectPoints;
	if (bCompletion)
	{
		Effect.SetWorldRotation(GetOwner()->GetActorRotation());
		Effect.SetWorldScale3D(GetOwner()->GetActorScale3D());
		for (FVector& P : Points) P = Effect.GetComponentTransform().InverseTransformVector(P);
		// The short CPU completion burst grows beyond the model and emits moving sprites.
		// Let Niagara include their real bounds; the persistent construction loop stays fixed-bounds.
		Effect.ClearSystemFixedBounds();
	}
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(&Effect, TEXT("User.RoofPoints"), Points);
	Effect.SetVariableInt(TEXT("User.RoofPointCount"), EffectPoints.Num());
	if (bCompletion)
	{
		Effect.SetVariableFloat(TEXT("User.BuildingHeight"), FMath::Max(1.f, ModelTop - float(Lifecycle->GetGroundLocation().Z))
			/ FMath::Max(.001f, float(FMath::Abs(GetOwner()->GetActorScale3D().Z))));
		Effect.SetVariableStaticMesh(TEXT("User.ContourWallMesh"), Shape->WallMesh);
		Effect.SetVariableStaticMesh(TEXT("User.FootprintMesh"), Shape->BaseMesh);
	}
	else
	{
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(&Effect, TEXT("User.RoofTangents"), EffectTangents);
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayFloat(&Effect, TEXT("User.SegmentLengths"), EffectLengths);
		Effect.SetVariableFloat(TEXT("User.ColumnHeight"), EffectColumnHeight);
	}
}

void UGuLiBuildingConstructionVisualComponent::StopTopEffect()
{
	if (IsValid(TopEffect)) TopEffect->DestroyComponent(); TopEffect = nullptr;
}

void UGuLiBuildingConstructionVisualComponent::UpdateTopEffect()
{
	if (!bConstructing || !Lifecycle || Lifecycle->GetState().WorkDone <= 0 || !Shape || EffectPoints.IsEmpty()) return;
	if (!TopEffect)
	{
		auto* System = GuLiVfx::Load<UNiagaraSystem>(this, GetDefault<UGuLiBuildingConstructionSettings>()->TopLoopVfxId);
		if (!System) return;
		TopEffect = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, System, Lifecycle->GetGroundLocation(),
			FRotator::ZeroRotator, FVector::OneVector, false, false, ENCPoolMethod::None, false);
		if (!TopEffect) return;
		ConfigureEffect(*TopEffect, false);
	}
	TopEffect->SetVariableFloat(TEXT("User.ConstructionHeight"), GetConstructionHeight() - Lifecycle->GetGroundLocation().Z);
	TopEffect->SetVariableFloat(TEXT("User.SparksActive"), Lifecycle->GetState().bHasActiveBuilders ? 1.f : 0.f);
	if (!TopEffect->IsActive()) TopEffect->Activate(true);
}

void UGuLiBuildingConstructionVisualComponent::PlayCompletionEffect()
{
	if (!Shape || !Shape->WallMesh || !Shape->BaseMesh) return;
	auto* System = GuLiVfx::Load<UNiagaraSystem>(this, GetDefault<UGuLiBuildingConstructionSettings>()->CompleteVfxId);
	if (!System) return;
	CompletionEffect = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, System, Lifecycle->GetGroundLocation(),
		FRotator::ZeroRotator, FVector::OneVector, true, false, ENCPoolMethod::None, false);
	if (!CompletionEffect) return;
	ConfigureEffect(*CompletionEffect, true); CompletionEffect->Activate(true);
}
