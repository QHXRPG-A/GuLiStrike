// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Wingman/Mass/GuLiWingmanSwarmFlow.h"

namespace
{
	uint32 MixBits(uint32 Value)
	{
		Value ^= Value >> 16u;
		Value *= 0x7feb352du;
		Value ^= Value >> 15u;
		Value *= 0x846ca68bu;
		Value ^= Value >> 16u;
		return Value == 0u ? 1u : Value;
	}

	uint32 CombineSeed(const uint32 A, const uint32 B)
	{
		return MixBits(A ^ (B + 0x9e3779b9u + (A << 6u) + (A >> 2u)));
	}

	float UnitFromSeed(const uint32 Seed)
	{
		return static_cast<float>(MixBits(Seed) & 0x00ffffffu) / 16777215.0f;
	}

	float SignedFromSeed(const uint32 Seed)
	{
		return UnitFromSeed(Seed) * 2.0f - 1.0f;
	}

	float Smooth(const float Value)
	{
		return Value * Value * (3.0f - 2.0f * Value);
	}

	float LatticeValue(const int32 X, const int32 Y, const int32 Z, const uint32 Seed)
	{
		uint32 Hash = CombineSeed(Seed, static_cast<uint32>(X));
		Hash = CombineSeed(Hash, static_cast<uint32>(Y));
		Hash = CombineSeed(Hash, static_cast<uint32>(Z));
		return SignedFromSeed(Hash);
	}

	struct FValueNoiseSample
	{
		float Value = 0.0f;
		FVector Gradient = FVector::ZeroVector;
	};

	FValueNoiseSample SampleValueNoise3(const FVector& Point, const uint32 Seed)
	{
		const int32 X0 = FMath::FloorToInt(Point.X);
		const int32 Y0 = FMath::FloorToInt(Point.Y);
		const int32 Z0 = FMath::FloorToInt(Point.Z);
		const float FractionX = static_cast<float>(Point.X - X0);
		const float FractionY = static_cast<float>(Point.Y - Y0);
		const float FractionZ = static_cast<float>(Point.Z - Z0);
		const float X = Smooth(FractionX);
		const float Y = Smooth(FractionY);
		const float Z = Smooth(FractionZ);
		const float DerivativeX = 6.0f * FractionX * (1.0f - FractionX);
		const float DerivativeY = 6.0f * FractionY * (1.0f - FractionY);
		const float DerivativeZ = 6.0f * FractionZ * (1.0f - FractionZ);
		const auto Sample = [Seed](const int32 SX, const int32 SY, const int32 SZ)
		{
			return LatticeValue(SX, SY, SZ, Seed);
		};
		const float C000 = Sample(X0, Y0, Z0);
		const float C100 = Sample(X0 + 1, Y0, Z0);
		const float C010 = Sample(X0, Y0 + 1, Z0);
		const float C110 = Sample(X0 + 1, Y0 + 1, Z0);
		const float C001 = Sample(X0, Y0, Z0 + 1);
		const float C101 = Sample(X0 + 1, Y0, Z0 + 1);
		const float C011 = Sample(X0, Y0 + 1, Z0 + 1);
		const float C111 = Sample(X0 + 1, Y0 + 1, Z0 + 1);
		const float X00 = FMath::Lerp(C000, C100, X);
		const float X10 = FMath::Lerp(C010, C110, X);
		const float X01 = FMath::Lerp(C001, C101, X);
		const float X11 = FMath::Lerp(C011, C111, X);
		const float Y0Value = FMath::Lerp(X00, X10, Y);
		const float Y1Value = FMath::Lerp(X01, X11, Y);

		FValueNoiseSample Result;
		Result.Value = FMath::Lerp(Y0Value, Y1Value, Z);
		Result.Gradient.X = FMath::Lerp(
			FMath::Lerp(C100 - C000, C110 - C010, Y),
			FMath::Lerp(C101 - C001, C111 - C011, Y), Z) * DerivativeX;
		Result.Gradient.Y = FMath::Lerp(X10 - X00, X11 - X01, Z) * DerivativeY;
		Result.Gradient.Z = (Y1Value - Y0Value) * DerivativeZ;
		return Result;
	}

	float ValueNoise3(const FVector& Point, const uint32 Seed)
	{
		return SampleValueNoise3(Point, Seed).Value;
	}

	FVector CurlNoise(const FVector& Point, const float Time, const uint32 Seed)
	{
		const FVector DriftA(Time * 0.37f, Time * -0.19f, Time * 0.11f);
		const FVector DriftB(Time * -0.13f, Time * 0.29f, Time * 0.23f);
		const FVector DriftC(Time * 0.17f, Time * 0.07f, Time * -0.31f);
		const FValueNoiseSample PotentialX = SampleValueNoise3(
			Point + DriftA, CombineSeed(Seed, 0x51f2e35du));
		const FValueNoiseSample PotentialY = SampleValueNoise3(
			Point + FVector(19.1f, -7.7f, 3.4f) + DriftB,
			CombineSeed(Seed, 0x7a4d91c3u));
		const FValueNoiseSample PotentialZ = SampleValueNoise3(
			Point + FVector(-11.8f, 5.9f, 23.3f) + DriftC,
			CombineSeed(Seed, 0xc3e29a71u));
		const FVector Curl(
			PotentialZ.Gradient.Y - PotentialY.Gradient.Z,
			PotentialX.Gradient.Z - PotentialZ.Gradient.X,
			PotentialY.Gradient.X - PotentialX.Gradient.Y);
		return Curl.ContainsNaN() ? FVector::ZeroVector : Curl.GetClampedToMaxSize(1.0f);
	}

	FVector BuildOrbitAxis(
		const FGuLiWingmanSwarmAgentFragment& Agent,
		const FGuLiWingmanSwarmOrbitTuning& Tuning,
		const float TimeSeconds)
	{
		const float Phase = TimeSeconds * Tuning.AxisPrecessionRadiansPerSecond;
		const FVector AxisNoise(
			ValueNoise3(FVector(Phase, 2.3f, 7.1f), CombineSeed(Agent.FlightSeed, 11u)),
			ValueNoise3(FVector(-3.7f, Phase, 5.9f), CombineSeed(Agent.FlightSeed, 23u)),
			ValueNoise3(FVector(4.1f, -8.3f, Phase), CombineSeed(Agent.FlightSeed, 47u)));
		const FVector Axis = (Agent.BaseOrbitAxis + AxisNoise * Tuning.AxisPrecessionAmount).GetSafeNormal();
		return Axis.IsNearlyZero() ? FVector::UpVector : Axis;
	}
}

void GuLiWingmanSwarmFlow::InitializeAgent(
	const FGuLiWingmanFormationRuntimeConfig& Formation,
	const FGuLiWingmanHandle& Handle,
	const uint32 InitialSimulationTick,
	FGuLiWingmanSwarmAgentFragment& OutAgent)
{
	const uint32 StableSlot = static_cast<uint32>(Handle.Flight.FlightIndex)
		* GULI_WINGMAN_MEMBERS_PER_FLIGHT + static_cast<uint32>(Handle.MemberIndex);
	OutAgent.FlightSeed = CombineSeed(Formation.FormationSeed,
		static_cast<uint32>(Handle.Flight.FlightIndex) + 0x2b992ddfu);
	OutAgent.AgentSeed = CombineSeed(OutAgent.FlightSeed,
		CombineSeed(StableSlot + 1u, Handle.EntityGeneration));
	OutAgent.FlowSimulationTick = InitialSimulationTick == 0u ? 1u : InitialSimulationTick;
	OutAgent.FlowStepAccumulator = 0.0f;
	const FGuLiWingmanSwarmOrbitTuning& Tuning = Formation.SwarmOrbit;
	const float RadiusAlpha = 0.16f + UnitFromSeed(CombineSeed(OutAgent.AgentSeed, 3u)) * 0.68f;
	OutAgent.PreferredRadiusBaseCentimeters = FMath::Lerp(
		Tuning.InnerSoftRadiusCentimeters,
		Tuning.OuterSoftRadiusCentimeters,
		RadiusAlpha);
	OutAgent.PreferredVerticalBiasCentimeters =
		SignedFromSeed(CombineSeed(OutAgent.AgentSeed, 5u))
		* Tuning.VerticalHalfExtentCentimeters * 0.65f;
	const float Azimuth = UnitFromSeed(CombineSeed(OutAgent.FlightSeed, 7u)) * UE_TWO_PI;
	const float Tilt = FMath::Lerp(0.18f, 0.48f,
		UnitFromSeed(CombineSeed(OutAgent.FlightSeed, 13u)));
	OutAgent.BaseOrbitAxis = FVector(
		FMath::Cos(Azimuth) * Tilt,
		FMath::Sin(Azimuth) * Tilt,
		FMath::Sqrt(FMath::Max(0.0f, 1.0f - Tilt * Tilt))).GetSafeNormal();
	OutAgent.NoiseDomainOffset = FVector(
		SignedFromSeed(CombineSeed(OutAgent.AgentSeed, 17u)),
		SignedFromSeed(CombineSeed(OutAgent.AgentSeed, 19u)),
		SignedFromSeed(CombineSeed(OutAgent.AgentSeed, 29u))) * 23.0f;
	OutAgent.SwirlSign = (MixBits(OutAgent.FlightSeed) & 1u) == 0u ? -1.0f : 1.0f;
}

void GuLiWingmanSwarmFlow::AdvanceSimulationClock(
	const float FrameDeltaSeconds,
	FGuLiWingmanSwarmAgentFragment& InOutAgent)
{
	InOutAgent.FlowStepAccumulator = FMath::Min(
		InOutAgent.FlowStepAccumulator + FMath::Clamp(FrameDeltaSeconds, 0.0f, 0.25f),
		FixedStepSeconds * 4.0f);
	while (InOutAgent.FlowStepAccumulator >= FixedStepSeconds)
	{
		InOutAgent.FlowStepAccumulator -= FixedStepSeconds;
		++InOutAgent.FlowSimulationTick;
		if (InOutAgent.FlowSimulationTick == 0u)
		{
			InOutAgent.FlowSimulationTick = 1u;
		}
	}
}

FVector GuLiWingmanSwarmFlow::BuildInitialOffset(
	const FGuLiWingmanFormationRuntimeConfig& Formation,
	const FGuLiWingmanHandle& Handle,
	const FGuLiWingmanSwarmAgentFragment& Agent,
	const uint32 CandidateIndex)
{
	constexpr float GoldenAngle = 2.39996323f;
	const uint32 CandidateSeed = CandidateIndex == 0u
		? Agent.AgentSeed
		: CombineSeed(Agent.AgentSeed, CombineSeed(CandidateIndex, 0x6d2b79f5u));
	const float PhaseJitter = SignedFromSeed(CombineSeed(CandidateSeed, 31u))
		* (CandidateIndex == 0u ? 0.22f : 0.55f);
	const uint32 StableSlot = static_cast<uint32>(Handle.Flight.FlightIndex)
		* GULI_WINGMAN_MEMBERS_PER_FLIGHT + static_cast<uint32>(Handle.MemberIndex);
	const float Azimuth = static_cast<float>(StableSlot) * GoldenAngle
		+ static_cast<float>(CandidateIndex) * GoldenAngle + PhaseJitter;
	const float Radius = CandidateIndex == 0u
		? Agent.PreferredRadiusBaseCentimeters
		: FMath::Clamp(
			Agent.PreferredRadiusBaseCentimeters
				+ SignedFromSeed(CombineSeed(CandidateSeed, 43u))
					* (Formation.SwarmOrbit.OuterSoftRadiusCentimeters
						- Formation.SwarmOrbit.InnerSoftRadiusCentimeters) * 0.35f,
			Formation.SwarmOrbit.InnerSoftRadiusCentimeters * 1.05f,
			Formation.SwarmOrbit.OuterSoftRadiusCentimeters * 0.95f);
	const float Vertical = FMath::Clamp(
		Agent.PreferredVerticalBiasCentimeters
			+ SignedFromSeed(CombineSeed(CandidateSeed, 37u))
				* Formation.SwarmOrbit.VerticalHalfExtentCentimeters
				* (CandidateIndex == 0u ? 0.12f : 0.45f),
		-Formation.SwarmOrbit.VerticalHalfExtentCentimeters * 0.9f,
		Formation.SwarmOrbit.VerticalHalfExtentCentimeters * 0.9f);
	const float HorizontalRadius = FMath::Sqrt(FMath::Max(
		FMath::Square(Radius) - FMath::Square(Vertical),
		FMath::Square(Formation.SwarmOrbit.HullExclusionRadiusCentimeters)));
	return FVector(
		FMath::Cos(Azimuth) * HorizontalRadius,
		FMath::Sin(Azimuth) * HorizontalRadius,
		Vertical);
}

FVector GuLiWingmanSwarmFlow::BuildFlightRecoveryOffset(
	const FGuLiWingmanFormationRuntimeConfig& Formation,
	const uint8 FlightIndex)
{
	const uint32 FlightSeed = CombineSeed(Formation.FormationSeed,
		static_cast<uint32>(FlightIndex) + 0x2b992ddfu);
	const float Radius = FMath::Lerp(
		Formation.SwarmOrbit.InnerSoftRadiusCentimeters,
		Formation.SwarmOrbit.OuterSoftRadiusCentimeters,
		0.42f + UnitFromSeed(CombineSeed(FlightSeed, 41u)) * 0.16f);
	const float Azimuth = static_cast<float>(FlightIndex) * UE_TWO_PI / 5.0f
		+ SignedFromSeed(CombineSeed(FlightSeed, 43u)) * 0.35f;
	const float Vertical = SignedFromSeed(CombineSeed(FlightSeed, 53u))
		* Formation.SwarmOrbit.VerticalHalfExtentCentimeters * 0.45f;
	return FVector(Radius * FMath::Cos(Azimuth), Radius * FMath::Sin(Azimuth), Vertical);
}

FVector GuLiWingmanSwarmFlow::BuildPreferredVelocity(
	const FVector& Position,
	const FVector& Velocity,
	const FVector& CarrierPosition,
	const FVector& CarrierVelocity,
	const FGuLiWingmanSwarmAgentFragment& Agent,
	const FGuLiWingmanFormationRuntimeConfig& Formation,
	const EGuLiWingmanFlightMode Mode)
{
	const FGuLiWingmanSwarmOrbitTuning& Tuning = Formation.SwarmOrbit;
	FVector Relative = Position - CarrierPosition;
	float Radius = static_cast<float>(Relative.Size());
	FVector Radial = Relative.GetSafeNormal();
	if (Radial.IsNearlyZero())
	{
		Radial = FVector::ForwardVector;
		Radius = 0.0f;
	}
	const float TimeSeconds = static_cast<float>(Agent.FlowSimulationTick) * FixedStepSeconds;
	const FVector Axis = BuildOrbitAxis(Agent, Tuning, TimeSeconds);
	FVector Tangent = FVector::CrossProduct(Axis, Radial).GetSafeNormal();
	if (Tangent.IsNearlyZero())
	{
		Tangent = FVector::CrossProduct(FVector::RightVector, Radial).GetSafeNormal();
	}
	Tangent *= Agent.SwirlSign;
	const float AgentSpeedAlpha = UnitFromSeed(CombineSeed(Agent.AgentSeed, 59u));
	const float SpeedPulse = 0.92f + 0.08f * ValueNoise3(
		FVector(TimeSeconds / 8.0f, Agent.NoiseDomainOffset.Y, Agent.NoiseDomainOffset.Z),
		CombineSeed(Agent.AgentSeed, 61u));
	const FVector SwirlVelocity = Tangent * FMath::Lerp(
		Tuning.SwirlSpeedMinCentimetersPerSecond,
		Tuning.SwirlSpeedMaxCentimetersPerSecond,
		AgentSpeedAlpha) * SpeedPulse;
	const FVector NoisePoint = Relative / Tuning.NoiseSpatialScaleCentimeters
		+ Agent.NoiseDomainOffset;
	const FVector CurlVelocity = CurlNoise(
		NoisePoint,
		TimeSeconds / Tuning.NoiseTemporalScaleSeconds,
		Agent.AgentSeed) * Tuning.CurlStrengthCentimetersPerSecond;

	FVector ConstraintVelocity = FVector::ZeroVector;
	if (Radius < Tuning.HullExclusionRadiusCentimeters)
	{
		const float Alpha = 1.0f - FMath::Clamp(
			Radius / Tuning.HullExclusionRadiusCentimeters, 0.0f, 1.0f);
		ConstraintVelocity += Radial * Tuning.BoundaryReturnSpeedCentimetersPerSecond
			* FMath::Lerp(1.0f, 2.0f, Alpha);
	}
	else if (Radius < Tuning.InnerSoftRadiusCentimeters)
	{
		const float Alpha = (Tuning.InnerSoftRadiusCentimeters - Radius)
			/ (Tuning.InnerSoftRadiusCentimeters - Tuning.HullExclusionRadiusCentimeters);
		ConstraintVelocity += Radial * Tuning.BoundaryReturnSpeedCentimetersPerSecond
			* FMath::Clamp(Alpha, 0.0f, 1.0f);
	}
	else if (Radius > Tuning.OuterSoftRadiusCentimeters)
	{
		const float Alpha = (Radius - Tuning.OuterSoftRadiusCentimeters)
			/ FMath::Max(1.0f,
				Formation.CatchUpDistanceCentimeters - Tuning.OuterSoftRadiusCentimeters);
		ConstraintVelocity -= Radial * Tuning.BoundaryReturnSpeedCentimetersPerSecond
			* FMath::Clamp(Alpha, 0.0f, 1.5f);
	}
	else
	{
		const float PreferredError = Agent.PreferredRadiusBaseCentimeters - Radius;
		ConstraintVelocity += Radial * FMath::Clamp(
			PreferredError / FMath::Max(1.0f,
				Tuning.OuterSoftRadiusCentimeters - Tuning.InnerSoftRadiusCentimeters),
			-1.0f, 1.0f) * Tuning.PreferredRadiusReturnSpeedCentimetersPerSecond;
	}

	const float VerticalError = Relative.Z - Agent.PreferredVerticalBiasCentimeters;
	const float VerticalBoundary = FMath::Abs(Relative.Z) - Tuning.VerticalHalfExtentCentimeters;
	if (VerticalBoundary > 0.0f)
	{
		ConstraintVelocity.Z -= FMath::Sign(Relative.Z)
			* Tuning.VerticalReturnSpeedCentimetersPerSecond
			* FMath::Clamp(VerticalBoundary / Tuning.VerticalHalfExtentCentimeters, 0.0f, 1.5f);
	}
	else
	{
		ConstraintVelocity.Z -= FMath::Clamp(
			VerticalError / Tuning.VerticalHalfExtentCentimeters, -1.0f, 1.0f)
			* Tuning.PreferredRadiusReturnSpeedCentimetersPerSecond * 0.35f;
	}

	const FVector RelativeVelocity = Velocity - CarrierVelocity;
	const float BoundaryInfluence = Radius < Tuning.InnerSoftRadiusCentimeters
		|| Radius > Tuning.OuterSoftRadiusCentimeters ? 0.35f : 0.06f;
	ConstraintVelocity -= Radial * FVector::DotProduct(RelativeVelocity, Radial) * BoundaryInfluence;
	float StyleWeight = 1.0f;
	if (Mode == EGuLiWingmanFlightMode::CatchUp)
	{
		StyleWeight = Tuning.CatchUpStyleWeight;
	}
	else if (Mode == EGuLiWingmanFlightMode::Recover)
	{
		StyleWeight = Tuning.RecoveryStyleWeight;
	}
	const FVector Preferred = CarrierVelocity
		+ (SwirlVelocity + CurlVelocity) * StyleWeight
		+ ConstraintVelocity;
	return Preferred.ContainsNaN() ? CarrierVelocity : Preferred;
}
