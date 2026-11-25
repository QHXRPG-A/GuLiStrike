#include "Gameplay/Wingman/Movement/GuLiWingmanPredictiveFlight.h"
#include "Gameplay/Wingman/Movement/GuLiWingmanSteering.h"

namespace GuLiWingmanPredictiveFlight
{
using namespace GuLiWingmanFlightCollision;
using EMode = EGuLiWingmanSteer;
constexpr float Step = GuLiWingmanSteering::FixedStepSeconds;
struct FCandidate
{
    EMode Mode;
    FVector Direction;
    float Speed;
};
struct FResult
{
    float ClearSeconds;
    bool bClear;
    FVector Normal;
    bool bHitShip;
};

void Integrate(FPose& Pose, const FVector& DesiredDirection, float DesiredSpeed,
               const FGuLiWingmanFormationRuntimeConfig& Tuning, float DeltaSeconds)
{
    const float Acceleration = DesiredSpeed > Pose.Speed ? Tuning.MaximumAccelerationCentimetersPerSecondSquared
                                                         : Tuning.MaximumDecelerationCentimetersPerSecondSquared;
    Pose.Speed = FMath::FInterpConstantTo(Pose.Speed, DesiredSpeed, DeltaSeconds, Acceleration);
    Pose.Forward = GuLiWingmanSteering::TurnDirectionToward(
        Pose.Forward, DesiredDirection,
        FMath::DegreesToRadians(Tuning.MaximumTurnRateDegreesPerSecond * DeltaSeconds));
    Pose.Position += Pose.Forward * (Pose.Speed * DeltaSeconds);
}

static FResult Verify(UWorld& World, const TArray<FHull>& Hulls, const FPose& Start, const FCandidate& Command,
                      const FGuLiWingmanFormationRuntimeConfig& Tuning, int32 Steps, uint16& Queries)
{
    TArray<FVector, TInlineAllocator<144>> Points;
    FPose Predicted = Start;
    Points.Add(Predicted.Position);
    for (int32 Index = 0; Index < Steps; ++Index)
    {
        Integrate(Predicted, Command.Direction, Command.Speed, Tuning, Step);
        Points.Add(Predicted.Position);
    }
    const float Radius =
        Tuning.AgentRadiusCentimeters + (Command.Mode == EMode::Escape ? 0 : Tuning.SafetyMarginCentimeters());
    auto Check = [&](auto&& VerifySegment, int32 FirstIndex, int32 LastIndex) -> FResult {
        double MaxErrorSquared = 0;
        int32 Split = FirstIndex;
        for (int32 Index = FirstIndex + 1; Index < LastIndex; ++Index)
        {
            // Compare at the same simulation time so acceleration cannot skip a moving hull.
            const FVector ChordPoint = FMath::Lerp(Points[FirstIndex], Points[LastIndex],
                                                   double(Index - FirstIndex) / (LastIndex - FirstIndex));
            const double ErrorSquared = FVector::DistSquared(Points[Index], ChordPoint);
            if (ErrorSquared > MaxErrorSquared)
            {
                MaxErrorSquared = ErrorSquared;
                Split = Index;
            }
        }
        if (MaxErrorSquared > FMath::Square(125.0))
        {
            const auto FirstHalf = VerifySegment(VerifySegment, FirstIndex, Split);
            return FirstHalf.bClear ? VerifySegment(VerifySegment, Split, LastIndex) : FirstHalf;
        }
        ++Queries;
        const float SweepRadius = Radius + FMath::Sqrt(MaxErrorSquared);
        FHitResult Earliest;
        bool bHitShip = false;
        bool bBlocked = SweepTerrain(World, Points[FirstIndex], Points[LastIndex], SweepRadius, Earliest);
        for (const FHull& Hull : Hulls)
        {
            FVector EndPlane;
            FHitResult Hit;
            if (HullContact(Hull, Points[FirstIndex], Points[LastIndex], SweepRadius, FirstIndex * Step,
                            LastIndex * Step, Hit, EndPlane) &&
                (!bBlocked || Hit.Time < Earliest.Time))
            {
                Earliest = Hit;
                bBlocked = true;
                bHitShip = true;
            }
        }
        return {bBlocked ? (FirstIndex + (LastIndex - FirstIndex) * Earliest.Time) * Step : LastIndex * Step,
                !bBlocked, Earliest.Normal, bHitShip};
    };
    return Check(Check, 0, Steps);
}

void Choose(UWorld& World, const TArray<FHull>& Hulls, FGuLiWingmanRuntimeState& State, const FPose& Pose,
            const FVector& Requested, float MaxSpeed, float DeltaSeconds)
{
    auto& Plan = State.FlightPlan;
    const auto& Tuning = State.Tuning.Formation;
    const FVector Carrier = State.Carrier.Transform.GetLocation();
    if (Plan.CarrierTrail.IsEmpty() ||
        FVector::DistSquared(Plan.CarrierTrail.Last(), Carrier) > FMath::Square(1000.0))
    {
        if (!Plan.CarrierTrail.IsEmpty() &&
            FVector::DistSquared(Plan.CarrierTrail.Last(), Carrier) > FMath::Square(25000.0))
            Plan.CarrierTrail.Reset();
        if (Plan.CarrierTrail.Num() >= 32)
            Plan.CarrierTrail.RemoveAt(0, 1, EAllowShrinking::No);
        Plan.CarrierTrail.Add(Carrier);
    }
    const FVector DesiredDirection = Requested.GetSafeNormal(UE_SMALL_NUMBER, Pose.Forward);
    const float DesiredSpeed = FMath::Clamp(float(Requested.Size()), 0.0f, MaxSpeed);
    const uint32 TaskRevision =
        HashCombineFast(GetTypeHash(State.Attack.Target.Target),
                        HashCombineFast(State.Attack.RunId, HashCombineFast(State.Ability.FormationCommandRevision,
                                                                            uint32(State.Attack.Phase))));
    Plan.Hold = FMath::Max(0.f, Plan.Hold - DeltaSeconds);
    Plan.SenseAfter -= DeltaSeconds;
    if (Plan.SenseAfter > 0 && Plan.TaskRevision == TaskRevision &&
        FVector::DotProduct(DesiredDirection, Plan.LastNominal) > .94 &&
        (Plan.Mode != EMode::Track ||
         FMath::Abs(DesiredSpeed - Plan.Speed) < FMath::Max(100.f, .15f * DesiredSpeed)))
        return;
    Plan.TaskRevision = TaskRevision;
    Plan.LastNominal = DesiredDirection;
    uint16 Queries = 0;
    const float Horizon =
        FMath::Clamp(.75f + Pose.Speed / 1900.f + 42.f / Tuning.MaximumTurnRateDegreesPerSecond, 1.35f, 4.7f);
    const int32 Steps = FMath::CeilToInt(Horizon / Step);
    FCandidate Winner{EMode::Track, DesiredDirection, DesiredSpeed};
    FResult Best = Verify(World, Hulls, Pose, Winner, Tuning, Steps, Queries);
    const bool bDirectClear = Best.bClear;
    State.Avoidance.bDetectedWorldStatic = !bDirectClear && !Best.bHitShip;
    State.Avoidance.bDetectedWorldDynamic = !bDirectClear && Best.bHitShip;
    if (!bDirectClear && !Best.Normal.IsNearlyZero())
        Plan.ContactNormal = Best.Normal;
    Plan.ClearVotes = bDirectClear ? FMath::Min<int32>(2, Plan.ClearVotes + 1) : 0;
    if (!bDirectClear || (Plan.Mode != EMode::Track && (Plan.Hold > 0 || Plan.ClearVotes < 2)))
    {
        const FVector Horizontal = DesiredDirection.GetSafeNormal2D(
            UE_SMALL_NUMBER, Pose.Forward.GetSafeNormal2D(UE_SMALL_NUMBER, FVector::ForwardVector));
        const FVector LeftVector = FVector::CrossProduct(Horizontal, FVector::UpVector);
        const float EvasionSpeed = FMath::Min(DesiredSpeed, MaxSpeed * .78f);
        TArray<FCandidate, TInlineAllocator<6>> Candidates;
        if (FVector::DistSquared(Pose.Position, Carrier) >
            FMath::Square(double(Tuning.CatchUpDistanceCentimeters)))
        {
            int32 ClosestIndex = INDEX_NONE;
            double BestDistanceSquared = MAX_dbl;
            for (int32 Index = 0; Index < Plan.CarrierTrail.Num(); ++Index)
            {
                const double DistanceSquared = FVector::DistSquared(Plan.CarrierTrail[Index], Pose.Position);
                if (DistanceSquared < BestDistanceSquared)
                {
                    ClosestIndex = Index;
                    BestDistanceSquared = DistanceSquared;
                }
            }
            if (ClosestIndex != INDEX_NONE && ClosestIndex + 1 < Plan.CarrierTrail.Num())
                Candidates.Add({EMode::Trail,
                                (Plan.CarrierTrail[ClosestIndex + 1] +
                                 FVector(0, 0, Tuning.AgentRadiusCentimeters) - Pose.Position)
                                    .GetSafeNormal(),
                                EvasionSpeed});
        }
        Candidates.Add({EMode::Climb, (Horizontal + FVector::UpVector * 1.48).GetSafeNormal(), EvasionSpeed});
        Candidates.Add({EMode::Left,
                        (Horizontal * .46 + LeftVector * .89 + FVector::UpVector * .24).GetSafeNormal(),
                        EvasionSpeed});
        Candidates.Add({EMode::Right,
                        (Horizontal * .46 - LeftVector * .89 + FVector::UpVector * .24).GetSafeNormal(),
                        EvasionSpeed});
        const int32 HeldIndex = Candidates.IndexOfByPredicate(
            [&](const FCandidate& Candidate) { return Candidate.Mode == Plan.Mode; });
        if (HeldIndex > 0)
            Candidates.Swap(0, HeldIndex);
        FVector Escape = Plan.ContactNormal + FVector::UpVector * .6;
        Candidates.Add({EMode::Escape, Escape.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector),
                        FMath::Min(Tuning.MinimumSpeedCentimetersPerSecond, MaxSpeed)});
        for (const auto& Candidate : Candidates)
        {
            if (Candidate.Mode == EMode::Escape && bDirectClear)
                break;
            FResult Result = Verify(World, Hulls, Pose, Candidate, Tuning, Steps, Queries);
            if (Result.bClear ||
                (!Best.bClear && (Result.ClearSeconds > Best.ClearSeconds ||
                                  (Candidate.Mode == EMode::Escape && Best.ClearSeconds <= Step))))
            {
                Winner = Candidate;
                Best = Result;
            }
            if (Result.bClear)
                break;
        }
    }
    if (Plan.Mode != Winner.Mode)
    {
        Plan.Hold = .72f;
        Plan.ClearVotes = 0;
    }
    Plan.Mode = Winner.Mode;
    Plan.Direction = Winner.Direction;
    Plan.Speed = Winner.Speed;
    Plan.SafeSeconds = Best.ClearSeconds;
    const float PhaseDelay = (State.Identity.Handle.GetGroupMemberIndex() % 5) * .022f;
    Plan.SenseAfter = Best.bClear ? (Plan.Mode == EMode::Track ? .55f : .30f) + PhaseDelay
                                  : FMath::Clamp(Best.ClearSeconds * .45f, Step, .12f);
    State.Avoidance.HeadingProbeCount = Queries;
    State.Avoidance.bHasSafeDirection = Best.bClear;
    State.Avoidance.bHasNextStepSafeDirection = Best.ClearSeconds >= Step;
    State.Avoidance.SafeDirection = Plan.Direction;
}
}
