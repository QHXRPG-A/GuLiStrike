"""Reviewed spatial defaults only; absolute, conflict-checked and resumable migration.

This is a bulk mechanical rewrite, not a numeric search/replace. Each listed
symbol is a game-space length/velocity/acceleration (not an angle, time, count,
screen pixel or source-mesh coordinate). Original lines and hashes are retained.
"""
from pathlib import Path
from decimal import Decimal
import argparse
import hashlib
import json
import re

ROOT = Path(__file__).resolve().parents[2]
REPORT = ROOT / 'TestResults/Scale020/source-default-migration-v1.json'
SPECS = {
    'GuLiStrikeShip.h': 'BaseMaxSpeed BaseAcceleration CameraDefaultArmLength CameraZoomStep CameraZoomMin CameraZoomMax CameraCollisionProbeRadius CameraCollisionMinArm',
    'GuLiShipMovementComponent.h': 'MaxFlySpeed MaxAcceleration BrakingDecelerationFlying',
    'GuLiShipTargetingRangeComponent.h': 'TargetingRadiusCentimeters',
    'GuLiRuntimeTuningTypes.h': 'MovementSpeedCmPerSecond',
    'GuLiCommanderCameraPawn.h': 'DesiredArmLength',
    'GuLiCommanderCameraPawn.cpp': 'DefaultArmLength MinimumArmLength EmergencyMinimumArmLength MaximumArmLength PivotHeightAboveGround BoomHeightAboveGround CameraHeightAboveGround BoundaryPadding BoomSampleSpacing CruiseHeightBuffer CruiseHeightDeadZone MaximumCruiseRiseSpeed MaximumReanchorDescentSpeed ReanchorCompletionTolerance EmergencyLiftTolerance',
    'GuLiCommanderHealthBarRenderer.cpp': 'MaximumDrawDistanceCentimeters FallbackSoldierHeightCentimeters HeightPaddingCentimeters',
    'GuLiCommanderPresentationActor.h': 'HardSnapDistanceCentimeters MaximumPredictionDistanceCentimeters',
    'GuLiCommanderPresentationPerformanceSettings.h': 'DefaultUnitCullDistanceCentimeters',
    'GuLiCommanderWorldReplicationComponent.h': 'FullRatePoseDistanceCentimeters',
    'GuLiCommanderNetworkGateValidation.h': 'RequiredPresentedTravelDistanceCentimeters MaximumPresentedStepP95Centimeters',
    'GuLiBattleAuthoritySubsystem.h': 'GroupSpacingCentimeters MemberSpacingCentimeters MemberAgentRadiusCentimeters MovementSpeedCentimetersPerSecond FlowFieldCorridorHalfWidthCentimeters',
    'GuLiBattleAuthoritySubsystem.cpp': 'SpatialCellSizeCentimeters FormationGuideMaximumLeadCentimeters FormationWaypointToleranceCentimeters FormationArrivalToleranceCentimeters MaximumSurfaceStepZCentimeters ProgressDistanceCentimeters DestinationMinimumSeparationCentimeters DestinationMaximumProjectionCorrectionCentimeters FreeDestinationMaximumRadiusCentimeters',
    'GuLiCommanderNavigationPolicy.h': 'RequiredAgentRadiusCentimeters MaximumSurfaceMoveZDeltaCentimeters MinimumNavigationProgressCentimeters',
    'GuLiCommanderAvoidancePolicy.h': 'SpatialCellSizeCentimeters DetectionDistanceCentimeters MaximumHeightDifferenceCentimeters PredictiveAvoidanceDistance PredictiveAvoidanceStiffness',
    'GuLiCommanderDestinationPlanner.h': 'MemberSpacingCentimeters DefaultFreeCandidatePitchCentimeters DefaultFreeCandidateRadiusCentimeters DefaultSoftAnchorPitchCentimeters',
    'GuLiResourceTypes.h': 'GULI_RESOURCE_CLUSTER_OBSTACLE_RADIUS_CM GULI_RESOURCE_FACTORY_OBSTACLE_HALF_EXTENT_CM GULI_RESOURCE_MINING_VEHICLE_NAV_RADIUS_CM',
    'GuLiResourceMapDefinition.h': 'MiningDistanceCentimeters FactoryManeuverSpeedCentimetersPerSecond FactoryDockOffsetCentimeters',
    'GuLiMiningVehiclePawn.h': 'SpeedCentimetersPerSecond MiningDistanceCentimeters ManeuverSpeed',
    'GuLiConstructionVehiclePawn.h': 'BaseSpeed',
    'GuLiBuildingTypes.h': 'GroundMaximumRangeCentimeters PlacementClearanceCentimeters',
    'GuLiBuildingPlacementComponent.cpp': 'GroundContactToleranceCentimeters CollisionGroundEpsilonCentimeters',
    'GuLiGroundAccessRampComponent.h': 'MaximumGroundRise MaximumGroundDrop',
    'GuLiStrongholdTransitConfig.h': 'LaneHeight ExitRadius',
    'GuLiTeleportTypes.h': 'BeamHeightCentimeters MaxShipHeightCentimeters',
    'GuLiWingmanAttackProfile.h': 'FlightSpeed StripLength PullUpHeight ExplosionRadius AirFireStartDistance AirFireStopDistance MinimumGroundHeight MaximumPullUpHeight',
    'GuLiWingmanRuntimeTypes.h': 'RadiusCentimeters HeightCentimeters PreferredRadiusBaseCentimeters DesiredSpeedCentimetersPerSecond',
    'GuLiWingmanProtocolTypes.h': 'GULI_WINGMAN_MAXIMUM_CARRIER_DISTANCE_CENTIMETERS GULI_WINGMAN_OWNER_CARRIER_DISTANCE_RESERVE_CENTIMETERS',
    'GuLiWingmanRelayServer.h': 'MaximumWingmanSpeedCentimetersPerSecond SpeedEnvelopeSlackCentimetersPerSecond PositionEnvelopeSlackCentimeters',
    'GuLiWingmanRelayServer.cpp': 'ConstraintHoldPositionToleranceCentimeters ConstraintHoldSpeedToleranceCentimetersPerSecond EmergencyLowSpeedThresholdCentimetersPerSecond',
    'GuLiCombatEffectDefinition.h': 'LaserLength LaserCoreWidth TracerWidth MuzzleWidth MuzzleLength MuzzleLightRadius TracerLightRadius MaximumVisualDistance VisualScale',
    'GuLiCombatEffectTypes.h': 'Radius Speed MinimumLiftHeight MaximumLiftHeight LateralOffset ConvergenceDistance SweepRadius MaximumTravelDistance',
    'GuLiProjectilePoolSubsystem.h': 'Speed MaximumDistance SweepRadius',
    'GuLiUnitFeedbackSubsystem.h': 'CullDistance',
    'GuLiEngineeringAIController.h': 'PredictionDistance OptimizationDistance FootprintRadius',
    'GuLiStrikeCharacter.h': 'DashDistance ProjectileOffset KnockbackStrength',
    'GuLiShipAbilitySet.cpp': 'RangeCentimeters ProjectileSpeedCentimetersPerSecond SweepRadiusCentimeters FlightSpeed AirFireStartDistance AirFireStopDistance ExplosionRadius CatchUpDistanceCentimeters RecoveryDistanceCentimeters',
    'GuLiWingmanSimulationSubsystem.cpp': 'MinimumGoalDriftForRepathCentimeters MinimumWaypointReachDistanceCentimeters',
    'GuLiWingmanPawn.h': 'FlightTrailCullDistance',
    'GuLiWingmanCombatCoordinator.h': 'AcquireRadiusCentimeters ReleaseRadiusCentimeters',
    'GuLiCommanderDeploymentPoint.h': 'SpacingCentimeters',
    'GuLiCommanderGameState.h': 'EffectiveSoldierMoveSpeedCmPerSecond',
    'GuLiGroundCrowdManager.cpp': 'MaxAgentRadius',
    'GuLiStrikeSpawner.h': 'SpawnRadius',
    'GuLiLogicalMissileSubsystem.h': 'SpeedCentimetersPerSecond SweepRadiusCentimeters',
    'GuLiGroundWarningSubsystem.h': 'ProjectionDepth Radius',
}
FORMATION = ('InnerRingRadiusCentimeters OuterRingRadiusCentimeters InnerRingHeightCentimeters OuterRingHeightCentimeters '
             'MaximumAccelerationCentimetersPerSecondSquared MaximumDecelerationCentimetersPerSecondSquared '
             'AgentRadiusCentimeters SeparationRadiusCentimeters ObstacleLookAheadCentimeters CatchUpDistanceCentimeters RecoveryDistanceCentimeters '
             'RangeCentimeters ProjectileSpeedCentimetersPerSecond SweepRadiusCentimeters')
SPECS['GuLiShipAbilityTypes.h'] = FORMATION + (' MinimumSpeedCentimetersPerSecond CruiseSpeedCentimetersPerSecond CatchUpSpeedCentimetersPerSecond '
    'InnerSoftRadiusCentimeters OuterSoftRadiusCentimeters VerticalHalfExtentCentimeters HullExclusionRadiusCentimeters '
    'SwirlSpeedMinCentimetersPerSecond SwirlSpeedMaxCentimetersPerSecond CurlStrengthCentimetersPerSecond NoiseSpatialScaleCentimeters '
    'BoundaryReturnSpeedCentimetersPerSecond PreferredRadiusReturnSpeedCentimetersPerSecond VerticalReturnSpeedCentimetersPerSecond')
SPECS['GuLiShipAbilityDefinitions.h'] = FORMATION + ' MinimumFlightSpeedCentimetersPerSecond CruiseFlightSpeedCentimetersPerSecond CatchUpFlightSpeedCentimetersPerSecond'

def run(apply=False):
    manifest = json.loads(REPORT.read_text(encoding='utf-8')) if REPORT.exists() else {'version': 1, 'scale': 0.2, 'entries': []}
    known = {(e['file'], e['symbol'], e['old_line']) for e in manifest['entries']}
    by_file = {}
    # Explicit Source tree only: no binary, engine, plugin caches or vendor content.
    for path in (ROOT / 'Source/GuLiStrike').rglob('*'):
        if path.name not in SPECS or 'Tests' in path.parts:
            continue
        relative = path.relative_to(ROOT).as_posix()
        source = path.read_bytes().decode('utf-8')
        for symbol in SPECS[path.name].split():
            existing = [e for e in manifest['entries'] if e['file'] == relative and e['symbol'] == symbol]
            if existing:
                continue
            pattern = re.compile(r'(?m)^.*?\b' + re.escape(symbol) + r'\s*=\s*(-?\d+(?:\.\d+)?)(f?)\s*;[^\r\n]*')
            matches = list(pattern.finditer(source))
            if not matches:
                raise RuntimeError(f'Missing reviewed symbol {relative}:{symbol}')
            for m in matches:
                if Decimal(m[1]) == 0:
                    continue
                value = Decimal(m[1]) * Decimal('0.2')
                target = format(value.normalize(), 'f')
                if '.' in m[1] or m[2]:
                    if '.' not in target:
                        target += '.0'
                line = m[0]
                start, end = m.start(1) - m.start(), m.end(1) - m.start()
                new_line = line[:start] + target + line[end:]
                entry = {'file': relative, 'symbol': symbol, 'space': 'gameplay_world_cm',
                         'old': m[1], 'target': target, 'old_line': line, 'target_line': new_line,
                         'captured_sha256': hashlib.sha256(source.encode('utf-8')).hexdigest(),
                         'occurrences': source.count(line)}
                if (relative, symbol, line) not in known:
                    manifest['entries'].append(entry)
                    known.add((relative, symbol, line))
        by_file[relative] = source
    # All-files preflight before any source write; reject edited numeric source lines.
    writes = {}
    for e in manifest['entries']:
        source = writes.get(e['file'], by_file[e['file']])
        old, new = e['old_line'], e['target_line']
        if new in source and old not in source:
            continue
        if source.count(old) != e.get('occurrences',1):
            raise RuntimeError(f'Conflict: {e["file"]}:{e["symbol"]}')
        writes[e['file']] = source.replace(old, new)
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
    if apply:
        for name, source in writes.items():
            (ROOT / name).write_bytes(source.encode('utf-8'))
    print(json.dumps({'apply': apply, 'fields': len(manifest['entries']), 'files_to_write': list(writes)}, ensure_ascii=False))

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--apply', action='store_true')
    run(parser.parse_args().apply)
