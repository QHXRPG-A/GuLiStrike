#include "Commander/UI/GuLiHealthBarLayout.h"
#include "Commander/UI/GuLiCommanderHealthBarRenderer.h"
#include "UI/GuLiSegmentedHealthBar.h"
#include "Serialization/JsonWriter.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "SceneView.h"
#include "Gameplay/Ship/Aiming/GuLiXuntianGunnery.h"

CSV_DEFINE_CATEGORY(GuLiHealthBarLayout, true);

FGuLiHealthBarView::FGuLiHealthBarView(const FSceneViewProjectionData& Projection, FVector2D CanvasSize, const FMatrix* Combined)
	: Matrix(Combined ? *Combined : Projection.ComputeViewProjectionMatrix())
{
	const FIntRect Full = Projection.GetViewRect(), Rect = Projection.GetConstrainedViewRect();
	const FVector2D Scale = CanvasSize / FVector2D(FMath::Max(Full.Width(),1), FMath::Max(Full.Height(),1));
	const FVector2D Min = FVector2D(Rect.Min - Full.Min) * Scale, Size = FVector2D(Rect.Size()) * Scale;
	Viewport = FBox2D(Min, Min + Size);
	Half = Size * .5;
	Middle = Min + Half;
	DesignPixel = FMath::Min(Size.X / 1920., Size.Y / 1080.);
	FocalX = Half.X * FMath::Abs(Projection.ProjectionMatrix.M[0][0]);
	ClipY = Matrix.GetColumn(1);
	ClipW = Matrix.GetColumn(3);
}

bool FGuLiHealthBarView::Project(const FVector& Center, FPoint& Point) const
{
	const FVector4 P = Matrix.TransformFVector4(FVector4(Center, 1));
	if (!(P.W > UE_SMALL_NUMBER && P.Z >= 0 && P.Z <= P.W)) return false;
	const double InvW = 1. / P.W;
	Point = {Middle + FVector2D(P.X * Half.X, -P.Y * Half.Y) * InvW, P.W, P.Y * InvW};
	return !Point.Screen.ContainsNaN();
}

bool FGuLiHealthBarView::Place(const FPoint& Point, const FGuLiHealthBarModel& Model, bool bShield,
	double BadgeTop, FBox2D& Bounds) const
{
	const double Width = Model.Width * FocalX / Point.W;
	if (DesignPixel <= 0 || !FMath::IsFinite(Width) || Width <= 0) return false;
	const double W = FMath::Clamp(Width, 24. * DesignPixel, 128. * DesignPixel);
	const double Pixel = GuLiHealthGrid::FMarkerSize::StylePixel(W);
	const double H = GuLiHealthGrid::FMarkerSize::ReferenceSize(bShield).Y * Pixel;
	// Upper support of the projected body, including camera tilt and off-axis perspective.
	const FVector Gradient = ClipY - Point.NdcY * ClipW;
	const double NearW = FMath::Max(Point.W * .5, Point.W - ClipW.GetAbs().Dot(Model.Extent));
	const double Top = Point.Screen.Y - Half.Y * Gradient.GetAbs().Dot(Model.Extent) / NearW;
	const double Bottom = FMath::Min(Top, BadgeTop) - 4. * Pixel;
	Bounds = FBox2D(FVector2D(Point.Screen.X - .5*W, Bottom-H), FVector2D(Point.Screen.X + .5*W, Bottom));
	return Bounds.Intersect(Viewport);
}

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

bool FGuLiHealthBarPoseCache::Resolve(FGuLiSceneUIHealthBar& Bar, TFunctionRef<bool(FGuLiHealthBarModel&)> Read,
	uint32 DisplacementRevision)
{
	auto& Sample = Anchors.FindOrAdd(Bar.Key);
	if (!Sample.bValid || (bSampleFrame && Sample.SeenFrame != Frame) || Sample.SeenFrame + 1 < Frame
		|| Sample.Target != Bar.Target || Sample.DisplacementRevision != DisplacementRevision)
	{
		Sample.bValid = Read(Sample.Model) && !Sample.Model.Center.ContainsNaN();
		Sample.Target = Bar.Target;
		Sample.DisplacementRevision = DisplacementRevision;
	}
	Sample.SeenFrame = Frame;
	if (Sample.bValid) static_cast<FGuLiHealthBarModel&>(Bar) = Sample.Model;
	return Sample.bValid;
}

void FGuLiHealthBarPoseCache::ApplySupplied(TArray<FGuLiSceneUIHealthBar>& Bars)
{
	// Ground outlines and overview icons already require a live position of their own.
	for (auto& Bar : Bars)
	{
		if (!Resolve(Bar, [&Bar](FGuLiHealthBarModel& Model) { Model = Bar; return true; }, Bar.DisplacementRevision))
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
		Information.Reserve(Bars.Num());
		for (auto& Bar : Bars)
		{
			if (!Bar.Key.Source.IsValid()) { Bar.Opacity = 0; continue; }
			auto& Info = Information.FindOrAdd(Bar.Key);
			Info = {Bar.Target, Bar.Fraction, Bar.MaxHealth, Bar.ShieldFraction, Bar.MaxShield, Publications};
		}
		for (auto It = Information.CreateIterator(); It; ++It)
			if (It.Value().Seen != Publications) It.RemoveCurrent();
		return !Information.IsEmpty();
	}

	for (auto& Bar : Bars)
	{
		const FInfo* Info = Information.Find(Bar.Key);
		if (!Info || Info->Target != Bar.Target || !Bar.Key.Source.IsValid()) { Bar.Opacity = 0; continue; }
		Bar.Fraction = Info->Health;
		Bar.MaxHealth = Info->MaxHealth;
		Bar.ShieldFraction = Info->Shield;
		Bar.MaxShield = Info->MaxShield;
	}
	return false;
}

FBox2D FGuLiHealthBarLayout::OverviewBounds(const FGuLiSceneUIHealthBar& Bar, FVector2D Anchor, float DPI)
{
	const auto Marker = GuLiHealthGrid::FMarkerSize::ForView(Bar.MaxShield > 0.f, true, Bar.OverviewIconPixels, DPI);
	const FVector2D Size = Marker.Size;
	const double Top = Bar.OverviewIconPixels * .5 + 2. * Marker.Pixel;
	const FVector2D Offset(-Size.X * .5, Top);
	return FBox2D(Anchor + Offset, Anchor + Offset + Size);
}

bool FGuLiHealthBarLayout::WorldBounds(const FGuLiHealthBarView& View, const FGuLiSceneUIHealthBar& Bar, FBox2D& Bounds)
{
	FGuLiHealthBarView::FPoint Point;
	return Information.Contains(Bar.Key) && View.Project(Bar.Center, Point)
		&& View.Place(Point, Bar, Bar.MaxShield > 0, TNumericLimits<double>::Max(), Bounds);
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
	ResetPilot();
	Poses.Reset();
	Information.Reset();
	Visible.Reset();
	Projected = 0;
	PublishedAt = NextPublication = -1;
	Publications = 0;
}

void FGuLiHealthBarLayout::ResetPilot()
{
	PilotOwner.Reset();
	PilotModels.Reset();
	Pilot.Contacts.Reset();
	Pilot.Bars.Reset();
	PilotContactsRevision = PilotVitalRevision = MAX_uint64;
	NextPilotSample = -1;
}

const FGuLiPilotHealthFrame& FGuLiHealthBarLayout::PilotFrame(const UGuLiXuntianGunnery& Gun,
	const FGuLiHealthBarView& View, const GuLiHealthGrid::FCanvas& Canvas, FVector Hull, uint64 VitalRevision,
	double Now, TFunctionRef<bool(const FGuLiCombatTargetSnapshot&,FGuLiHealthBarModel&)> ReadModel,
	TFunctionRef<const FGuLiSceneUIHealthBar*(const FGuLiCombatTargetSnapshot&)> ReadVitals)
{
	if (PilotOwner.Get() != &Gun || Now + .1 < NextPilotSample)
	{
		ResetPilot();
		PilotOwner = &Gun;
	}
	const auto& Contacts = Gun.GetContacts();
	const uint64 Revision = Gun.GetContactsRevision();
	const bool ContentsChanged = PilotContactsRevision != Revision;
	bool Rebuild = ContentsChanged || VitalRevision != PilotVitalRevision || Gun.Focus != PilotFocus || Hull != PilotHull
		|| !View.Matrix.Equals(PilotMatrix, 0) || View.Viewport != PilotViewport || Canvas.Scale != PilotCanvasScale
		|| !(Canvas.Transform == PilotTransform);
	auto Sample = [&](const FGuLiCombatTargetSnapshot& C, FPilotModel& Sampled)
	{
		FGuLiHealthBarModel Body;
		if (!ReadModel(C, Body)) Body.Center = C.GetHitCenter();
		Rebuild |= !(Body == Sampled.Body);
		Sampled.Body = Body;
		Sampled.ContactCenter = C.GetHitCenter();
	};
	if (ContentsChanged)
	{
		for (const auto& C : Contacts)
		{
			auto& Entry = PilotModels.FindOrAdd(C.Handle);
			if (!Entry.LastRevision || Entry.bDetailed) Sample(C, Entry);
			else
			{
				Entry.Body.Center += C.GetHitCenter() - Entry.ContactCenter;
				Entry.ContactCenter = C.GetHitCenter();
			}
			Entry.LastRevision = Revision;
		}
		for (auto It = PilotModels.CreateIterator(); It; ++It)
			if (It.Value().LastRevision != Revision) It.RemoveCurrent();
		NextPilotSample = Now + FGuLiHealthBarPoseCache::SampleInterval;
	}
	else if (AdvanceDeadline(Now, FGuLiHealthBarPoseCache::SampleInterval, NextPilotSample))
	{
		// Only contacts admitted on the last paint need a new visual pose between ledger snapshots.
		for (const auto& Item : Pilot.Contacts)
			Sample(Contacts[Item.Index], PilotModels.FindChecked(Contacts[Item.Index].Handle));
	}
	if (!Rebuild) return Pilot;
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiHealthBar_PilotScreenLayout);
	CSV_SCOPED_TIMING_STAT(GuLiHealthBarLayout, PilotLayout);
	PilotContactsRevision = Revision; PilotVitalRevision = VitalRevision; PilotFocus = Gun.Focus; PilotHull = Hull;
	PilotMatrix = View.Matrix; PilotViewport = View.Viewport; PilotCanvasScale = Canvas.Scale; PilotTransform = Canvas.Transform;
	Pilot.Contacts.Reset(); Pilot.Bars.Reset();
	int32 GroundLabels = 0;
	for (int32 I = 0; I < Contacts.Num(); ++I)
	{
		const auto& C = Contacts[I];
		auto& Entry = PilotModels.FindChecked(C.Handle);
		Entry.bDetailed = false;
		FGuLiHealthBarView::FPoint Point;
		if (!View.Project(Entry.Body.Center, Point)) continue;
		const bool Air = C.Handle.Kind == EGuLiTargetKind::Ship || C.Handle.Kind == EGuLiTargetKind::Wingman;
		const FVector Delta = Entry.Body.Center - Hull;
		const double Range = Delta.Size(), Miss = FVector2D::Distance(View.Middle, Point.Screen);
		const FVector2D Limit = View.Half - FVector2D(12.);
		const FVector2D Offset = (Point.Screen-View.Middle).GetAbs();
		const bool OnScreen = Offset.X <= Limit.X && Offset.Y <= Limit.Y;
		const bool Badge = OnScreen && (Air || C.Handle == Gun.Focus || (Range <= 65000 && (Miss <= 98 || GroundLabels < 6)));
		const auto* Vitals = ReadVitals(C);
		const float BarOpacity = GuLiHealthBars::Visibility(Hull, C.Location, Vitals && Vitals->bRecentDamage ? Vitals->Opacity : 0.f);
		if (!Badge && BarOpacity <= 0) continue;
		if (Badge && !Air) ++GroundLabels;
		const double Radius = FMath::Clamp(C.CollisionRadius/FMath::Max(3000.,Range)*View.Half.Y*1.56,
			Air ? 10. : 12., Air ? 30. : 32.);
		const FVector2D Flag = Point.Screen - FVector2D(0, Radius*.62 + 9);
		auto& Item = Pilot.Contacts.AddDefaulted_GetRef();
		Item.Index = I; Item.Center = Point.Screen; Item.Flag = Flag; Item.Radius = Radius; Item.Range = Range;
		Item.Miss = Miss; Item.DeltaZ = Delta.Z; Item.Depth = Point.W; Item.bBadge = Badge; Item.bChanged = Vitals && Vitals->bRecentDamage;
		Item.Life = Vitals ? GuLiHealthGrid::FBarValue{Vitals->Fraction*Vitals->MaxHealth,Vitals->MaxHealth}
			: GuLiHealthGrid::FBarValue{C.Health,C.MaxHealth};
		Item.Shield = Vitals ? GuLiHealthGrid::FBarValue{Vitals->ShieldFraction*Vitals->MaxShield,Vitals->MaxShield}
			: GuLiHealthGrid::FBarValue{C.Shield,C.MaxShield};
		Entry.bDetailed = true;
		FBox2D Bounds(ForceInit);
		if (BarOpacity <= 0 || !View.Place(Point, Entry.Body, Item.Shield.Maximum > 0.f,
			Badge ? Flag.Y - 13. : TNumericLimits<double>::Max(), Bounds)) continue;
		Item.Bar = Bounds;
		Pilot.Bars.Add(Canvas, Bounds.Min, Bounds.GetSize(), Item.Life, Item.Shield, BarOpacity,
			GuLiHealthGrid::FMarkerSize::StylePixel(Bounds.GetSize().X));
	}
	return Pilot;
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
		Writer->WriteValue(TEXT("max_health"), Bar.MaxHealth);
		Writer->WriteValue(TEXT("model_width_cm"), Bar.Width);
		Writer->WriteValue(TEXT("cells"), GuLiHealthGrid::CellCount(Bar.MaxHealth));
		Writer->WriteValue(TEXT("shield"), Bar.ShieldFraction);
		Writer->WriteValue(TEXT("has_shield"), Bar.MaxShield > 0.f);
		Writer->WriteValue(TEXT("max_shield"), Bar.MaxShield);
		Writer->WriteValue(TEXT("shield_cells"), GuLiHealthGrid::CellCount(Bar.MaxShield));
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
