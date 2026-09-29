#include "Commander/Presentation/GuLiCommanderLODSubsystem.h"

#include "CoreGlobals.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/DefaultValueHelper.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "SceneView.h"

DEFINE_LOG_CATEGORY_STATIC(LogGuLiCommanderLOD, Log, All);

namespace GuLiCommanderLOD
{
	constexpr const TCHAR* ConfigSection = TEXT("GuLiCommanderLOD");
	struct FField { const TCHAR* Key; float FGuLiCommanderLODSettings::* Member; };
	const FField Fields[] = {
		{TEXT("near.enter_distance_cm"), &FGuLiCommanderLODSettings::NearEnterDistance},
		{TEXT("near.hold_distance_cm"), &FGuLiCommanderLODSettings::NearHoldDistance},
		{TEXT("near.enter_screen"), &FGuLiCommanderLODSettings::NearEnterScreen},
		{TEXT("near.hold_screen"), &FGuLiCommanderLODSettings::NearHoldScreen},
		{TEXT("mid.enter_distance_cm"), &FGuLiCommanderLODSettings::MidEnterDistance},
		{TEXT("mid.hold_distance_cm"), &FGuLiCommanderLODSettings::MidHoldDistance},
		{TEXT("mid.enter_screen"), &FGuLiCommanderLODSettings::MidEnterScreen},
		{TEXT("mid.hold_screen"), &FGuLiCommanderLODSettings::MidHoldScreen},
		{TEXT("minimum_residence_seconds"), &FGuLiCommanderLODSettings::MinimumResidenceSeconds}
	};
	const FField* FindField(const FString& Key)
	{
		for (const auto& Field : Fields) if (Key.Equals(Field.Key, ESearchCase::IgnoreCase)) return &Field;
		return nullptr;
	}
	bool ParseNumber(const FString& Text, float& Out)
	{
		const FString Trimmed = Text.TrimStartAndEnd();
		if (Trimmed.IsEmpty()) return false;
		for (const TCHAR Character : Trimmed) if (FChar::IsWhitespace(Character)) return false;
		double Parsed = 0;
		if (!FDefaultValueHelper::ParseDouble(Trimmed, Parsed) || !FMath::IsFinite(Parsed)
			|| FMath::Abs(Parsed) > TNumericLimits<float>::Max()) return false;
		Out = static_cast<float>(Parsed);
		return true;
	}
}

bool UGuLiCommanderLODSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const auto* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld()
		&& World->GetNetMode() != NM_DedicatedServer;
}

void UGuLiCommanderLODSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	BaselineSettings = {};
	ConfigKeys.Reset();
	OverrideKeys.Reset();
	for (const auto& Field : GuLiCommanderLOD::Fields)
	{
		FString Raw;
		if (!GConfig || !GConfig->GetString(GuLiCommanderLOD::ConfigSection, Field.Key, Raw, GGameIni)) continue;
		float Value = 0;
		if (!GuLiCommanderLOD::ParseNumber(Raw, Value))
		{
			UE_LOG(LogGuLiCommanderLOD, Warning, TEXT("Invalid config %s; keeping the compiled default."), Field.Key);
			continue;
		}
		BaselineSettings.*(Field.Member) = Value;
		ConfigKeys.Add(Field.Key);
	}
	FString Error;
	if (!BaselineSettings.Validate(Error))
	{
		UE_LOG(LogGuLiCommanderLOD, Warning, TEXT("Invalid LOD config group: %s Using compiled defaults."), *Error);
		BaselineSettings = {};
		ConfigKeys.Reset();
	}
	EffectiveSettings = BaselineSettings;
}

void UGuLiCommanderLODSubsystem::Deinitialize()
{
	Views.Reset();
	ConsumerStats.Reset();
	OverrideKeys.Reset();
	ConfigKeys.Reset();
	ViewFrame = MAX_uint64;
	QueryCount = 0;
	ViewUpdateMilliseconds = 0;
	Super::Deinitialize();
}

void UGuLiCommanderLODSubsystem::EnsureViews()
{
	if (ViewFrame == GFrameCounter) return;
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCommanderLODViews);
	const double Started = FPlatformTime::Seconds();
	ViewFrame = GFrameCounter;
	Views.Reset();
	QueryCount = 0;
	for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const auto* Controller = It->Get();
		const auto* Player = Controller && Controller->IsLocalController() ? Controller->GetLocalPlayer() : nullptr;
		FSceneViewProjectionData Projection;
		if (!Player || !Player->ViewportClient || !Player->ViewportClient->Viewport
			|| !Player->GetProjectionData(Player->ViewportClient->Viewport, Projection)) continue;
		const float Scale = FMath::Max(float(Projection.ProjectionMatrix.M[0][0]), float(Projection.ProjectionMatrix.M[1][1]));
		if (Projection.ViewOrigin.ContainsNaN() || !FMath::IsFinite(Scale) || Scale <= 0) continue;
		auto& View = Views.AddDefaulted_GetRef();
		View.Position = Projection.ViewOrigin;
		View.ProjectionScale = Scale;
		GetViewFrustumBounds(View.Frustum, Projection.ComputeViewProjectionMatrix(), true);
	}
	ViewUpdateMilliseconds = (FPlatformTime::Seconds() - Started) * 1000.;
}

FGuLiCommanderLODDecision UGuLiCommanderLODSubsystem::Evaluate(const FGuLiCommanderLODQuery& Query)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCommanderLODEvaluate);
	EnsureViews();
	++QueryCount;
	FGuLiCommanderLODDecision Result;
	if (!Query.Bounds.IsValid || Query.Bounds.Min.ContainsNaN() || Query.Bounds.Max.ContainsNaN()
		|| Query.Bounds.Min.X > Query.Bounds.Max.X || Query.Bounds.Min.Y > Query.Bounds.Max.Y
		|| Query.Bounds.Min.Z > Query.Bounds.Max.Z)
	{
		Result.Reason = EGuLiCommanderLODReason::InvalidBounds;
		return Result;
	}
	if (Views.IsEmpty()) return Result;
	Result.Reason = EGuLiCommanderLODReason::OutsideFrustum;
	for (const auto& View : Views)
	{
		if (!View.Frustum.IntersectBox(Query.Bounds.GetCenter(), Query.Bounds.GetExtent())) continue;
		const double Distance = FMath::Sqrt(Query.Bounds.ComputeSquaredDistanceToPoint(View.Position));
		const float Screen = float(Query.Bounds.GetExtent().Size() / FMath::Max(1.0, Distance)) * View.ProjectionScale;
		if (!FMath::IsFinite(Distance) || !FMath::IsFinite(Screen)) continue;
		Result.bVisible = true;
		Result.DistanceCentimeters = FMath::Min(Result.DistanceCentimeters, Distance);
		Result.ScreenFraction = FMath::Max(Result.ScreenFraction, Screen);
		// Classify each view separately: never pair one view's distance with another view's screen size.
		const auto Level = EffectiveSettings.Classify(Distance, Screen, Query.CurrentLevel);
		if (static_cast<uint8>(Level) < static_cast<uint8>(Result.TargetLevel)) Result.TargetLevel = Level;
	}
	if (Result.bVisible)
	{
		const double Now = GetWorld()->GetTimeSeconds();
		Result.bCanTransition = !Query.CurrentLevel.IsSet() || !FMath::IsFinite(Query.LastChangeWorldSeconds)
			|| Now < Query.LastChangeWorldSeconds || Now - Query.LastChangeWorldSeconds >= EffectiveSettings.MinimumResidenceSeconds;
		Result.Reason = !Result.bCanTransition && Result.TargetLevel != Query.CurrentLevel.GetValue()
			? EGuLiCommanderLODReason::MinimumResidence : EGuLiCommanderLODReason::DistanceAndScreen;
	}
	return Result;
}

TArray<FGuLiCommanderLODSettingView> UGuLiCommanderLODSubsystem::ListSettings(const FString& Prefix) const
{
	TArray<FGuLiCommanderLODSettingView> Result;
	for (const auto& Field : GuLiCommanderLOD::Fields)
	{
		if (!FString(Field.Key).StartsWith(Prefix, ESearchCase::IgnoreCase)) continue;
		Result.Add({Field.Key, BaselineSettings.*(Field.Member), EffectiveSettings.*(Field.Member),
			OverrideKeys.Contains(Field.Key) ? TEXT("LocalOverride") : ConfigKeys.Contains(Field.Key) ? TEXT("Config") : TEXT("CppDefault")});
	}
	return Result;
}

bool UGuLiCommanderLODSubsystem::SetSetting(const FString& Key, const FString& Value, FString& Error)
{
	const auto* Field = GuLiCommanderLOD::FindField(Key);
	if (!Field) { Error = TEXT("Unknown LOD key. Use gs.Commander.LOD.List."); return false; }
	float Parsed = 0;
	if (!GuLiCommanderLOD::ParseNumber(Value, Parsed)) { Error = TEXT("Expected one finite number, without trailing text."); return false; }
	auto Candidate = EffectiveSettings;
	Candidate.*(Field->Member) = Parsed;
	if (!Candidate.Validate(Error)) return false;
	EffectiveSettings = Candidate;
	OverrideKeys.Add(Field->Key);
	return true;
}

bool UGuLiCommanderLODSubsystem::ResetSetting(const FString& KeyOrAll, FString& Error)
{
	if (KeyOrAll.Equals(TEXT("all"), ESearchCase::IgnoreCase))
	{
		EffectiveSettings = BaselineSettings;
		OverrideKeys.Reset();
		Error.Reset();
		return true;
	}
	const auto* Field = GuLiCommanderLOD::FindField(KeyOrAll);
	if (!Field) { Error = TEXT("Unknown LOD key. Use gs.Commander.LOD.List."); return false; }
	auto Candidate = EffectiveSettings;
	Candidate.*(Field->Member) = BaselineSettings.*(Field->Member);
	if (!Candidate.Validate(Error)) { Error += TEXT(" Reset all to restore the complete baseline."); return false; }
	EffectiveSettings = Candidate;
	OverrideKeys.Remove(Field->Key);
	return true;
}

void UGuLiCommanderLODSubsystem::ReportConsumer(const FName Consumer, const FGuLiCommanderLODConsumerStats& Stats)
{
	if (Consumer.IsNone()) return;
	auto& Snapshot = ConsumerStats.FindOrAdd(Consumer);
	Snapshot = Stats;
	Snapshot.ReportFrame = GFrameCounter;
}

void UGuLiCommanderLODSubsystem::ClearConsumer(const FName Consumer)
{
	ConsumerStats.Remove(Consumer);
}
