#include "Gameplay/CombatEffects/GuLiFlightEvent.h"
#include "Battle/Combat/GuLiLogicalMissileSubsystem.h"

FGuLiCombatEffectState GuLiFlightWire::FromLogicalMissile(const FGuLiLogicalMissileState& Missile)
{
	FGuLiCombatEffectState State;
	State.MatchEpoch = Missile.MatchEpoch; State.EffectId = Missile.MissileId;
	State.Sequence = Missile.SimulationSequence+1;
	State.Kind = EGuLiCombatEffectKind::Projectile;
	State.Source = Missile.Emitter.IsValid() ? GuLiCombatTargets::MakeWingmanTargetHandle(Missile.Emitter) : Missile.Source;
	State.Target = Missile.Target;
	State.Location = Missile.Position; State.Velocity = Missile.Velocity;
	State.LaunchLocation = Missile.LaunchPosition; State.LaunchDirection = Missile.LaunchDirection;
	State.LastTargetLocation = Missile.LastTargetLocation;
	State.StartTime = State.ActivationTime = Missile.LaunchTime;
	State.SampleTime = Missile.LaunchTime+Missile.AgeSeconds;
	State.EndTime = Missile.LaunchTime+Missile.MaximumLifetimeSeconds;
	State.Motion.Speed = Missile.SpeedCentimetersPerSecond;
	State.Motion.TurnRate = FMath::Max(.001f,Missile.TurnRateDegreesPerSecond);
	State.Motion.SweepRadius = Missile.SweepRadiusCentimeters;
	State.Motion.MaximumLifetime = Missile.MaximumLifetimeSeconds;
	State.Motion.LiftSeconds = State.Motion.MinimumLiftHeight = State.Motion.MaximumLiftHeight = 0;
	State.Motion.LateralOffset = State.Motion.VerticalCurve = State.Motion.LongitudinalCurve = 0;
	State.RandomSeed = GetTypeHash(Missile.MissileId);
	return State;
}
