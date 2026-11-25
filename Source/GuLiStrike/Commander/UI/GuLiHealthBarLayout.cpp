#include "Commander/UI/GuLiHealthBarLayout.h"
#include "Serialization/JsonWriter.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CsvProfiler.h"

CSV_DEFINE_CATEGORY(GuLiHealthBarLayout, true);

namespace
{
	// Snap to the original cadence. A late frame samples the latest state once.
	bool AdvanceDeadline(double Now, double Interval, double& Next)
	{
		if (Next < 0)
		{
			Next = Now + Interval;
			return true;
		}
		if (Now + UE_SMALL_NUMBER < Next) return false;
		const double Periods = FMath::FloorToDouble((Now - Next + UE_SMALL_NUMBER) / Interval) + 1;
		Next += Periods * Interval;
		return true;
	}
}

void FGuLiHealthBarPoseCache::BeginFrame(double Now)
{
	if (Now < LastFrameTime) Reset();
	LastFrameTime = Now;
	++Frame;
	bSampleFrame = AdvanceDeadline(Now, SampleInterval, NextSample);
	if (bSampleFrame) ++SampleCount;
}

bool FGuLiHealthBarPoseCache::Resolve(const FGuLiSceneUIHealthBar& Bar, TFunctionRef<bool(FVector&)> Read,
	FVector& Out, uint32 DisplacementRevision)
{
	auto& Sample = Anchors.FindOrAdd(Bar.Key);
	if (!Sample.bValid || (bSampleFrame && Sample.SeenFrame != Frame) || Sample.SeenFrame + 1 < Frame
		|| Sample.Target != Bar.Target || Sample.DisplacementRevision != DisplacementRevision)
	{
		Sample.bValid = Read(Sample.Center) && !Sample.Center.ContainsNaN();
		Sample.Target = Bar.Target;
		Sample.DisplacementRevision = DisplacementRevision;
	}
	Sample.SeenFrame = Frame;
	if (Sample.bValid) Out = Sample.Center;
	return Sample.bValid;
}

void FGuLiHealthBarPoseCache::ApplySupplied(TArray<FGuLiSceneUIHealthBar>& Bars)
{
	// Ground outlines and overview icons already require a live position of their own.
	for (auto& Bar : Bars)
	{
		if (!Resolve(Bar, [&Bar](FVector& Center) { Center = Bar.Center; return true; }, Bar.Center))
			Bar.Opacity = 0;
	}
}

void FGuLiHealthBarPoseCache::EndFrame()
{
	if (!bSampleFrame) return;
	for (auto It = Anchors.CreateIterator(); It; ++It)
		if (It.Value().SeenFrame != Frame || !It.Key().Source.IsValid()) It.RemoveCurrent();
}

void FGuLiHealthBarPoseCache::Reset()
{
	Anchors.Reset();
	LastFrameTime = NextSample = -1;
	Frame = SampleCount = 0;
	bSampleFrame = false;
}

bool FGuLiHealthBarLayout::Publish(TArray<FGuLiSceneUIHealthBar>& Bars, double Now)
{
	CSV_SCOPED_TIMING_STAT(GuLiHealthBarLayout, Publish);
	if (Now < PublishedAt)
	{
		NextPublication = -1;
		Publications = 0;
	}
	const bool Due = AdvanceDeadline(Now, 0.1, NextPublication);
	if (Due)
	{
		PublishedAt = Now;
		++Publications;
		Information.Reset();
		Information.Reserve(Bars.Num());
		for (auto& Bar : Bars)
		{
			if (!Bar.Key.Source.IsValid()) { Bar.Opacity = 0; continue; }
			Information.Add(Bar.Key, {Bar.Target, Bar.Fraction, Bar.ShieldFraction, Bar.bHasShield});
		}
		return !Information.IsEmpty();
	}

	for (auto& Bar : Bars)
	{
		const FInfo* Info = Information.Find(Bar.Key);
		if (!Info || Info->Target != Bar.Target || !Bar.Key.Source.IsValid()) { Bar.Opacity = 0; continue; }
		Bar.Fraction = Info->Health;
		Bar.ShieldFraction = Info->Shield;
		Bar.bHasShield = Info->HasShield;
	}
	return false;
}

FBox2D FGuLiHealthBarLayout::ScreenBounds(const FGuLiSceneUIHealthBar& Bar, FVector2D Anchor, float DPI, bool bOverview)
{
	if (bOverview)
	{
		const float Width = FMath::Max(Bar.OverviewIconPixels, 22.f * DPI);
		const FVector2D Min = Anchor + FVector2D(-Width * .5f - DPI, Bar.OverviewIconPixels * .5f + 2.f * DPI);
		return FBox2D(Min, Min + FVector2D(Width + 2.f * DPI, (Bar.bHasShield ? 8.f : 5.f) * DPI));
	}
	const FVector2D Min = Anchor - FVector2D(43, 7);
	return FBox2D(Min, Min + FVector2D(86, Bar.bHasShield ? 18 : 14));
}

void FGuLiHealthBarLayout::ProjectVisible(TConstArrayView<FGuLiSceneUIHealthBar> Bars, const FBox2D& Viewport,
	TFunctionRef<bool(int32, FBox2D&, float&)> Project)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiHealthBar_ScreenLayout);
	CSV_SCOPED_TIMING_STAT(GuLiHealthBarLayout, Select);
	Visible.Reset();
	Projected = 0;
	for (int32 Index = 0; Index < Bars.Num(); ++Index)
	{
		if (Bars[Index].Opacity <= 0) continue;
		FBox2D Bounds(ForceInit);
		float Opacity = 0;
		++Projected;
		if (!Project(Index, Bounds, Opacity) || Opacity <= 0 || !Bounds.bIsValid
			|| Bounds.Min.ContainsNaN() || Bounds.Max.ContainsNaN()) continue;
		const FBox2D Clipped(Bounds.Min.ComponentMax(Viewport.Min), Bounds.Max.ComponentMin(Viewport.Max));
		if (Clipped.Min.X >= Clipped.Max.X || Clipped.Min.Y >= Clipped.Max.Y) continue;
		Visible.Add({Index, Bounds, Opacity});
	}
	CSV_CUSTOM_STAT(GuLiHealthBarLayout, Drawn, Visible.Num(), ECsvCustomStatOp::Set);
}

void FGuLiHealthBarLayout::Reset()
{
	Poses.Reset();
	Information.Reset();
	Visible.Reset();
	Projected = 0;
	PublishedAt = NextPublication = -1;
	Publications = 0;
}

FString FGuLiHealthBarLayout::Describe(TConstArrayView<FGuLiSceneUIHealthBar> Bars, bool bIncludeCandidates) const
{
	FString Json;
	auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("candidates"), Bars.Num());
	Writer->WriteValue(TEXT("pose_samples"), static_cast<double>(Poses.GetSampleCount()));
	Writer->WriteValue(TEXT("published_at"), PublishedAt);
	Writer->WriteValue(TEXT("publications"), static_cast<double>(Publications));
	if (bIncludeCandidates)
	{
		Writer->WriteArrayStart(TEXT("inputs"));
		for (const auto& Bar : Bars)
		{
			Writer->WriteObjectStart();
			Writer->WriteValue(TEXT("target"), Bar.Target.StableId.ToString(EGuidFormats::Digits));
			Writer->WriteValue(TEXT("source"), GetNameSafe(Bar.Key.Source.Get()));
			Writer->WriteValue(TEXT("opacity"), Bar.Opacity);
			Writer->WriteArrayStart(TEXT("anchor"));
			for (double Value : {Bar.Center.X, Bar.Center.Y, Bar.Center.Z}) Writer->WriteValue(Value);
			Writer->WriteArrayEnd();
			Writer->WriteObjectEnd();
		}
		Writer->WriteArrayEnd();
	}
	Writer->WriteArrayStart(TEXT("visible"));
	for (const FVisible& Item : Visible)
	{
		if (!Bars.IsValidIndex(Item.Index)) continue;
		const auto& Bar = Bars[Item.Index];
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("source"), GetNameSafe(Bar.Key.Source.Get()));
		Writer->WriteValue(TEXT("element"), FString::Printf(TEXT("%llu"), Bar.Key.Element));
		Writer->WriteValue(TEXT("generation"), FString::Printf(TEXT("%llu"), Bar.Key.Generation));
		Writer->WriteValue(TEXT("health"), Bar.Fraction);
		Writer->WriteValue(TEXT("shield"), Bar.ShieldFraction);
		Writer->WriteValue(TEXT("has_shield"), Bar.bHasShield);
		Writer->WriteValue(TEXT("selected"), Bar.bSelected);
		Writer->WriteArrayStart(TEXT("anchor"));
		for (double Value : {Bar.Center.X, Bar.Center.Y, Bar.Center.Z}) Writer->WriteValue(Value);
		Writer->WriteArrayEnd();
		Writer->WriteArrayStart(TEXT("rect"));
		for (double Value : {Item.Bounds.Min.X, Item.Bounds.Min.Y, Item.Bounds.Max.X, Item.Bounds.Max.Y}) Writer->WriteValue(Value);
		Writer->WriteArrayEnd();
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();
	Writer->WriteObjectEnd();
	Writer->Close();
	return Json;
}
