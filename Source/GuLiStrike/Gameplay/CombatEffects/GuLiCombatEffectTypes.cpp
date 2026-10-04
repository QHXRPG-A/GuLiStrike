#include "Gameplay/CombatEffects/GuLiCombatEffectTypes.h"
#include "Misc/SecureHash.h"
#include "NativeGameplayTags.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectDefinition.h"
#include "Gameplay/CombatEffects/GuLiGroundWarningSubsystem.h"
#include "Engine/PackageMapClient.h"
#include "NiagaraSystem.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_GuLi_CombatEffectMissileWeapon, "Weapon.Missile");

bool FGuLiSpellFieldConfig::IsValid() const
{
	return !ConfigId.IsNone()
		&& FMath::IsFinite(Damage) && Damage > 0.0f && Damage <= 1000000000.0f
		&& FMath::IsFinite(Radius) && Radius > 0.0f && Radius <= 100000.0f
		&& FMath::IsFinite(Delay) && Delay >= 0.0f && Delay <= 120.0f
		&& FMath::IsFinite(Duration) && Duration >= 0.0f && Duration <= 120.0f
		&& FMath::IsFinite(PulseInterval) && PulseInterval >= 0.033f && PulseInterval <= 120.0f
		&& FMath::IsFinite(DissipationSeconds) && DissipationSeconds >= 0.0f && DissipationSeconds <= 30.0f
		&& (Timing != EGuLiSpellFieldTiming::Periodic || Duration >= 0.033f);
}

bool FGuLiWeaponMountConfig::IsValid() const
{
	if (!bCalibrated || UnitTypeId == 0 || SlotId.IsNone() || Muzzles.IsEmpty()
		|| AimOffset.ContainsNaN() || AimOffset.GetAbsMax() > 1000000.0)
	{
		return false;
	}
	return Muzzles.Num() <= MAX_uint8 + 1 && Muzzles.ContainsByPredicate([](const FVector& Point)
	{
		return Point.ContainsNaN() || Point.GetAbsMax() > 1000000.0;
	}) == false;
}

bool FGuLiProjectileMotionSettings::IsValid() const
{
	return FMath::IsFinite(Speed) && Speed > 0 && Speed <= 1000000
		&& FMath::IsFinite(LiftSeconds) && LiftSeconds >= 0 && LiftSeconds <= 10
		&& FMath::IsFinite(MinimumLiftHeight) && MinimumLiftHeight >= 0
		&& FMath::IsFinite(MaximumLiftHeight) && MaximumLiftHeight >= MinimumLiftHeight && MaximumLiftHeight <= 100000
		&& FMath::IsFinite(LateralOffset) && LateralOffset >= 0 && LateralOffset <= 100000
		&& FMath::IsFinite(VerticalCurve) && VerticalCurve >= 0 && VerticalCurve <= 100000
		&& FMath::IsFinite(LongitudinalCurve) && LongitudinalCurve >= 0 && LongitudinalCurve <= 100000
		&& FMath::IsFinite(ConvergenceDistance) && ConvergenceDistance > 0
		&& FMath::IsFinite(TurnRate) && TurnRate > 0 && TurnRate <= 1080
		&& FMath::IsFinite(SweepRadius) && SweepRadius >= 0 && SweepRadius <= 10000
		&& FMath::IsFinite(MaximumLifetime) && MaximumLifetime > LiftSeconds && MaximumLifetime <= 120;
}

bool FGuLiCombatEffectState::IsWellFormed() const
{
	if (MatchEpoch == 0 || !EffectId.IsValid() || Sequence == 0
		|| Kind > EGuLiCombatEffectKind::LinearProjectile || Phase > EGuLiCombatEffectPhase::Finished
		|| EndReason > EGuLiCombatEffectEndReason::Blocked) return false;
	// A terminal wire record only identifies what to remove. It carries no cast payload.
	if (Phase == EGuLiCombatEffectPhase::Finished)
		return (Kind != EGuLiCombatEffectKind::LinearProjectile && Kind != EGuLiCombatEffectKind::Projectile)
			|| (!Location.ContainsNaN() && FMath::IsFinite(SampleTime));
	if (!Source.IsValid()
		|| Location.ContainsNaN() || Velocity.ContainsNaN() || LastTargetLocation.ContainsNaN()
		|| LaunchLocation.ContainsNaN() || LaunchDirection.ContainsNaN()
		|| !FMath::IsFinite(StartTime) || !FMath::IsFinite(SampleTime) || SampleTime < StartTime
		|| !FMath::IsFinite(ActivationTime) || !FMath::IsFinite(EndTime) || EndTime < StartTime
		|| !FMath::IsFinite(Radius) || Radius < 0)
	{
		return false;
	}
	if (Kind == EGuLiCombatEffectKind::Projectile)
		return Motion.IsValid() && (GroundWarningStyle.IsNull() || (bFixedPoint && Radius > 0 && Radius <= 100000))
			&& (!GuidanceBatchId.IsValid() || (bFixedPoint && !GroundWarningStyle.IsNull()
				&& !GuidanceCenter.ContainsNaN() && GuidanceCenter.GetAbsMax() <= 10000000
				&& FMath::IsFinite(GuidanceRadius) && GuidanceRadius > 0 && GuidanceRadius <= 100000));
	if (Kind == EGuLiCombatEffectKind::SpellField) return true;
	if (Kind == EGuLiCombatEffectKind::LinearProjectile)
		return (Source.Kind == EGuLiTargetKind::Wingman || Source.Kind == EGuLiTargetKind::CommanderSoldier
			|| Source.Kind == EGuLiTargetKind::GroundActor || Source.Kind == EGuLiTargetKind::Ship) && !MuzzleOffset.ContainsNaN()
			&& (SourceTeam == EGuLiTeam::Red || SourceTeam == EGuLiTeam::Blue)
			&& FMath::IsFinite(Motion.Speed) && Motion.Speed > 0 && Motion.Speed <= 1000000
			&& SampleTime <= EndTime
			&& EndTime > StartTime && EndTime - StartTime <= 120.01f && !FVector(LaunchDirection).IsNearlyZero();
	return Target.IsValid() && !SlotId.IsNone() && !MuzzleOffset.ContainsNaN()
		&& MuzzleOffset.GetAbsMax() <= 1000000.0
		&& FMath::IsFinite(FireRateHz) && FireRateHz > 0.0f && FireRateHz <= 30.0f
		&& EndTime > StartTime;
}

namespace
{
	// Local to this cosmetic protocol: do not change the shared damage/wingman wire format.
	void SerializeEffectTarget(FArchive& Ar, FGuLiTargetHandle& Target, uint32 Epoch)
	{
		uint8 Kind = static_cast<uint8>(Target.Kind); Ar.SerializeBits(&Kind, 3);
		if (Ar.IsLoading()) { Target = {}; Target.Kind = static_cast<EGuLiTargetKind>(Kind); }
		if (Target.Kind == EGuLiTargetKind::None) return;
		uint8 bCompact = Target.Kind == EGuLiTargetKind::CommanderSoldier
			&& Target == GuLiCombatTargets::MakeCommanderSoldierTargetHandle(Epoch, Target.LocalId, Target.Generation);
		Ar.SerializeBits(&bCompact, 1);
		Ar.SerializeIntPacked(Target.Generation); Ar.SerializeIntPacked(Target.LocalId);
		if (bCompact)
		{
			if (Ar.IsLoading()) Target = GuLiCombatTargets::MakeCommanderSoldierTargetHandle(Epoch, Target.LocalId, Target.Generation);
		}
		else Ar << Target.AuthorityId;
	}

	template<class T> bool SerializeEffectAsset(FArchive& Ar, UPackageMap* Map, TSoftObjectPtr<T>& Reference)
	{
		if (!Map)
		{
			// Standalone bit-stream tests do not have a network package map.
			FString Path=Reference.ToSoftObjectPath().ToString(); Ar << Path;
			if (Ar.IsLoading()) Reference=TSoftObjectPtr<T>(FSoftObjectPath(Path));
			return true;
		}
		// Definitions are held strongly by the runtime. PackageMap sends their NetGUID,
		// not the same long soft-object path on every explosion and live snapshot.
		UObject* Object = Reference.Get();
		const bool bMapped = Map->SerializeObject(Ar, T::StaticClass(), Object);
		if (Ar.IsLoading()) Reference = Cast<T>(Object);
		return bMapped;
	}
}

bool FGuLiCombatEffectState::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	if (Ar.IsLoading()) *this = {};
	Ar.SerializeIntPacked(MatchEpoch); Ar << EffectId; Ar.SerializeIntPacked(Sequence);
	uint8 KindValue=static_cast<uint8>(Kind), PhaseValue=static_cast<uint8>(Phase), ReasonValue=static_cast<uint8>(EndReason);
	Ar.SerializeBits(&KindValue,2); Ar.SerializeBits(&PhaseValue,2); Ar.SerializeBits(&ReasonValue,3);
	if (Ar.IsLoading()) { Kind=static_cast<EGuLiCombatEffectKind>(KindValue); Phase=static_cast<EGuLiCombatEffectPhase>(PhaseValue); EndReason=static_cast<EGuLiCombatEffectEndReason>(ReasonValue); }
	bool bMapped=true, bVector=true;
	if (Kind == EGuLiCombatEffectKind::LinearProjectile)
	{
		// Launch-only data; SampleTime is used once when bootstrapping a new peer.
		if (Phase == EGuLiCombatEffectPhase::Finished)
		{
			Location.NetSerialize(Ar, Map, bVector); Ar << SampleTime;
		}
		else
		{
			SerializeEffectTarget(Ar, Source, MatchEpoch);
			SerializeEffectTarget(Ar, Target, MatchEpoch);
			bool HasPoint = Target.IsValid() || !FVector(LastTargetLocation).IsNearlyZero(); Ar.SerializeBits(&HasPoint,1);
			if (HasPoint) LastTargetLocation.NetSerialize(Ar,Map,bVector);
			if (Source.Kind == EGuLiTargetKind::GroundActor)
			{
				uint32 Id = static_cast<uint32>(PlayerBulletVfxId);
				Ar.SerializeIntPacked(Id);
				if (Id > MAX_int32) Ar.SetError();
				if (Ar.IsLoading()) PlayerBulletVfxId = static_cast<int32>(Id);
			}
			LaunchLocation.NetSerialize(Ar, Map, bVector); LaunchDirection.NetSerialize(Ar, Map, bVector);
			FVector_NetQuantize Muzzle(MuzzleOffset); Muzzle.NetSerialize(Ar, Map, bVector);
			Ar << Motion.Speed << Motion.SweepRadius << StartTime << EndTime;
			Ar << SampleTime;
			uint8 Team = static_cast<uint8>(SourceTeam); Ar.SerializeBits(&Team, 2);
			if (Ar.IsLoading())
			{
				SourceTeam = static_cast<EGuLiTeam>(Team); MuzzleOffset = Muzzle;
				Velocity = FVector(LaunchDirection) * Motion.Speed;
				Location = FVector(LaunchLocation) + FVector(Velocity) * (SampleTime - StartTime);
				ActivationTime = StartTime;
			}
		}
		bOutSuccess = !Ar.IsError() && bVector && IsWellFormed(); return bMapped;
	}
	if (Phase == EGuLiCombatEffectPhase::Finished && Kind == EGuLiCombatEffectKind::Projectile)
	{
		// Preserve the authority impact point for the last historical trail segment.
		Location.NetSerialize(Ar, Map, bVector); Ar << SampleTime;
	}
	if (Phase != EGuLiCombatEffectPhase::Finished)
	{
		SerializeEffectTarget(Ar,Source,MatchEpoch); SerializeEffectTarget(Ar,Target,MatchEpoch);
		Location.NetSerialize(Ar,Map,bVector);
		Ar << StartTime << SampleTime << ActivationTime << EndTime << RandomSeed;
		if (Kind == EGuLiCombatEffectKind::Projectile)
		{
			bMapped &= SerializeEffectAsset(Ar,Map,ProjectileDefinition);
			bMapped &= SerializeEffectAsset(Ar,Map,FieldDefinition);
			bool bHasGroundWarning = !GroundWarningStyle.IsNull();
			Ar.SerializeBits(&bHasGroundWarning, 1);
			if (bHasGroundWarning)
			{
				bMapped &= SerializeEffectAsset(Ar, Map, GroundWarningStyle);
				Ar << Radius;
			}
			bool bHasGuidance = GuidanceBatchId.IsValid();
			Ar.SerializeBits(&bHasGuidance, 1);
			if (bHasGuidance)
			{
				Ar << GuidanceBatchId << GuidanceRadius;
				GuidanceCenter.NetSerialize(Ar, Map, bVector);
			}
			Velocity.NetSerialize(Ar,Map,bVector); LaunchLocation.NetSerialize(Ar,Map,bVector);
			LastTargetLocation.NetSerialize(Ar,Map,bVector); LaunchDirection.NetSerialize(Ar,Map,bVector);
			Ar << bFixedPoint;
			Ar << Motion.Speed << Motion.LiftSeconds << Motion.MinimumLiftHeight << Motion.MaximumLiftHeight
				<< Motion.LateralOffset << Motion.VerticalCurve << Motion.LongitudinalCurve
				<< Motion.ConvergenceDistance << Motion.TurnRate << Motion.SweepRadius << Motion.MaximumLifetime;
		}
		else if (Kind == EGuLiCombatEffectKind::SpellField)
		{
			bMapped &= SerializeEffectAsset(Ar,Map,FieldDefinition);
			uint32 Variant = static_cast<uint32>(VariantIndex+1); Ar.SerializeIntPacked(Variant);
			if (Ar.IsLoading()) { VariantIndex=static_cast<int32>(Variant)-1; LaunchLocation=Location; LastTargetLocation=Location; }
			Ar << Radius;
		}
		else
		{
			Ar << SlotId << MuzzleOffset << FireRateHz;
			LastTargetLocation.NetSerialize(Ar,Map,bVector);
			if (Ar.IsLoading())
			{
				LaunchLocation=Location; LaunchDirection=FVector::ForwardVector;
				Velocity=FVector::ZeroVector; Radius=0.0f;
			}
		}
	}
	bOutSuccess = !Ar.IsError() && bVector && IsWellFormed();
	return bMapped;
}

bool FGuLiCombatEffectCorrection::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	Ar.SerializeIntPacked(MatchEpoch); Ar << EffectId; Ar.SerializeIntPacked(Sequence);
	bool bVector=true;
	Location.NetSerialize(Ar,Map,bVector); Velocity.NetSerialize(Ar,Map,bVector); LastTargetLocation.NetSerialize(Ar,Map,bVector);
	Ar << SampleTime;
	bOutSuccess=!Ar.IsError() && bVector && FMath::IsFinite(SampleTime);
	return true;
}

bool FGuLiCombatShotCue::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	Ar.SerializeIntPacked(MatchEpoch); Ar << ShotId;
	SerializeEffectTarget(Ar,Source,MatchEpoch); SerializeEffectTarget(Ar,Target,MatchEpoch);
	uint8 bBasicAttack=SlotId==TEXT("BasicAttack"); Ar.SerializeBits(&bBasicAttack,1);
	if (bBasicAttack) { if (Ar.IsLoading()) SlotId=TEXT("BasicAttack"); } else Ar << SlotId;
	uint32 Type=UnitTypeId; Ar.SerializeIntPacked(Type); if (Ar.IsLoading()) UnitTypeId=static_cast<uint16>(Type);
	Ar << MuzzleIndex << KeepAliveSeconds;
	uint8 MuzzleOnly = bMuzzleOnly; Ar.SerializeBits(&MuzzleOnly, 1);
	if (Ar.IsLoading()) bMuzzleOnly = MuzzleOnly != 0;
	// Use the same centimeter precision as pooled launch payloads, keeping muzzle-only cues within the existing budget.
	bool bVector = true;
	FVector_NetQuantize Muzzle(MuzzleOffset); Muzzle.NetSerialize(Ar, Map, bVector);
	if (Ar.IsLoading()) MuzzleOffset = Muzzle;
	Start.NetSerialize(Ar,Map,bVector); End.NetSerialize(Ar,Map,bVector); Ar << ServerTime;
	uint8 Mechanical = bMechanicalShot;
	Ar.SerializeBits(&Mechanical, 1);
	if (Ar.IsLoading()) bMechanicalShot = Mechanical != 0;
	if (bMechanicalShot)
	{
		MuzzleDirection.NetSerialize(Ar, Map, bVector);
		Ar << RecoilFromCentimeters << MechanicalPoseTimeSeconds;
		if (!FMath::IsFinite(MechanicalPoseTimeSeconds) || MechanicalPoseTimeSeconds < 0
			|| !FMath::IsFinite(RecoilFromCentimeters) || RecoilFromCentimeters < 0 || RecoilFromCentimeters > 100 || MuzzleIndex > 1) Ar.SetError();
	}
	bOutSuccess=!Ar.IsError() && bVector && Type<=MAX_uint16 && ShotId.IsValid() && FMath::IsFinite(ServerTime);
	return true;
}

FVector GuLiCombatEffects::LiftPosition(const FGuLiCombatEffectState& State, const float Age)
{
	FRandomStream Random(State.RandomSeed);
	const float Height = Random.FRandRange(State.Motion.MinimumLiftHeight, State.Motion.MaximumLiftHeight);
	const float Side = Random.FRandRange(-State.Motion.LateralOffset, State.Motion.LateralOffset);
	const float Alpha = FMath::Clamp(Age / State.Motion.LiftSeconds, 0.0f, 1.0f);
	const FVector Forward = FVector(State.LaunchDirection).GetSafeNormal2D(UE_SMALL_NUMBER, FVector::ForwardVector);
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
	return FVector(State.LaunchLocation) + FVector::UpVector * Height * Alpha
		+ Forward * State.Motion.Speed * State.Motion.LiftSeconds * 0.3f * Alpha
		+ Right * Side * FMath::Sin(Alpha * HALF_PI);
}

FVector GuLiCombatEffects::AdvanceProjectile(FGuLiCombatEffectState& State, const float NewAge, const float DeltaSeconds)
{
	const FVector Previous = State.Location;
	if (DeltaSeconds <= 0 || !FMath::IsFinite(DeltaSeconds)) return Previous;
	if (State.Motion.LiftSeconds > 0 && NewAge <= State.Motion.LiftSeconds)
	{
		State.Location = LiftPosition(State, NewAge);
		State.Velocity = (FVector(State.Location) - Previous) / DeltaSeconds;
		return State.Location;
	}
	const FVector ToTarget = FVector(State.LastTargetLocation) - Previous;
	const double Distance = ToTarget.Size();
	FRandomStream Random(State.RandomSeed);
	Random.FRand(); // Keep the random stream positions shared with LiftPosition.
	const float Side = Random.FRandRange(-State.Motion.LateralOffset, State.Motion.LateralOffset);
	const FVector Right = FVector::CrossProduct(FVector::UpVector, ToTarget.GetSafeNormal2D()).GetSafeNormal();
	const float Envelope = FMath::Clamp(static_cast<float>(Distance) / State.Motion.ConvergenceDistance, 0.0f, 1.0f);
	FVector Curve = Right * Side * Envelope * FMath::Sin(NewAge * 3.0f);
	if (State.Motion.VerticalCurve > 0 || State.Motion.LongitudinalCurve > 0)
	{
		const float Phase = Random.FRandRange(-PI, PI);
		const float Frequency = Random.FRandRange(1.7f, 3.8f);
		const float Vertical = Random.FRandRange(0.35f, 1.0f) * State.Motion.VerticalCurve;
		const float Longitudinal = Random.FRandRange(-1.0f, 1.0f) * State.Motion.LongitudinalCurve;
		const float SmoothEnvelope = Envelope * Envelope * (3.0f - 2.0f * Envelope);
		const float Departure = FMath::Clamp((NewAge - State.Motion.LiftSeconds) / 0.3f, 0.0f, 1.0f);
		// Offsets vanish with zero slope near impact. Each missile owns its height,
		// frequency, phase and fore/aft bend; none is driven by client frame randomness.
		Curve = (Right * Side * FMath::Sin(NewAge * Frequency + Phase)
			+ FVector::UpVector * Vertical * (0.6f + 0.4f * FMath::Sin(NewAge * Frequency * 0.73f - Phase))
			+ FVector(State.LaunchDirection).GetSafeNormal2D() * Longitudinal * FMath::Sin(NewAge * Frequency * 0.61f + Phase))
			* SmoothEnvelope * Departure;
	}
	const FVector Desired = (ToTarget + Curve).GetSafeNormal();
	FVector Direction = FVector(State.Velocity).GetSafeNormal();
	if (Direction.IsNearlyZero()) Direction = Desired;
	const double Angle = FMath::Acos(FMath::Clamp(FVector::DotProduct(Direction, Desired), -1.0, 1.0));
	if (Angle > UE_SMALL_NUMBER)
	{
		const double Fraction = FMath::Min(1.0, FMath::DegreesToRadians(State.Motion.TurnRate) * DeltaSeconds / Angle);
		Direction = FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(Direction, Desired), Fraction).RotateVector(Direction).GetSafeNormal();
	}
	State.Velocity = Direction * State.Motion.Speed;
	State.Location = Previous + FVector(State.Velocity) * DeltaSeconds;
	return State.Location;
}

bool GuLiCombatEffects::IntersectsSphere(const FVector& Center, const float Radius, const FGuLiCombatTargetSnapshot& Target)
{
	return Target.bAlive && !Center.ContainsNaN() && !Target.Location.ContainsNaN()
		&& FMath::IsFinite(Radius) && Radius >= 0 && FMath::IsFinite(Target.CollisionRadius) && Target.CollisionRadius >= 0
		&& FVector::DistSquared(Center, Target.Location) <= FMath::Square(static_cast<double>(Radius + Target.CollisionRadius));
}

int32 GuLiCombatEffects::PulsesDue(const EGuLiSpellFieldTiming Timing, const double Activation,
	const double Duration, const double Interval, const double Now)
{
	if (!FMath::IsFinite(Now) || !FMath::IsFinite(Activation) || Now + 1.e-6 < Activation) return 0;
	if (Timing != EGuLiSpellFieldTiming::Periodic) return 1;
	if (!FMath::IsFinite(Duration) || !FMath::IsFinite(Interval) || Duration <= 0 || Interval < 0.033) return 0;
	const int32 Total = FMath::CeilToInt(Duration / Interval - 1.e-6);
	return FMath::Clamp(FMath::FloorToInt((Now - Activation + 1.e-6) / Interval) + 1, 0, Total);
}

FGuid GuLiCombatEffects::DamageId(const FGuid& EffectId, const int32 PulseIndex, const FGuLiTargetHandle& Target)
{
	FMD5 Hash;
	Hash.Update(reinterpret_cast<const uint8*>(&EffectId), sizeof(EffectId));
	Hash.Update(reinterpret_cast<const uint8*>(&PulseIndex), sizeof(PulseIndex));
	const uint8 Kind = static_cast<uint8>(Target.Kind);
	Hash.Update(&Kind, sizeof(Kind));
	Hash.Update(reinterpret_cast<const uint8*>(&Target.AuthorityId), sizeof(Target.AuthorityId));
	Hash.Update(reinterpret_cast<const uint8*>(&Target.Generation), sizeof(Target.Generation));
	Hash.Update(reinterpret_cast<const uint8*>(&Target.LocalId), sizeof(Target.LocalId));
	uint8 Digest[16]; Hash.Final(Digest);
	FGuid Result; FMemory::Memcpy(&Result, Digest, sizeof(Result));
	return Result;
}

bool GuLiCombatEffects::IsNewerState(const FGuLiCombatEffectState& Incoming, const FGuLiCombatEffectState& Previous)
{
	return Incoming.IsWellFormed() && Incoming.MatchEpoch == Previous.MatchEpoch
		&& Incoming.EffectId == Previous.EffectId && Previous.Phase != EGuLiCombatEffectPhase::Finished
		&& Incoming.Sequence > Previous.Sequence;
}
