from pathlib import Path
root=Path('Source/GuLiStrike/Commander/Mass')
p=root/'Navigation/GuLiCommanderAvoidancePolicy.h'; s=p.read_text(encoding='utf-8-sig').replace('bool bMoving = false;', 'bool bMoving = false;\n\t\tbool bEnvironment = false;').replace('Rebuilds the 1500 cm center-point grid','Rebuilds the radius-covered environment / center-point soldier grid'); p.write_text(s,encoding='utf-8')
p=root/'Navigation/GuLiCommanderAvoidancePolicy.cpp'; s=p.read_text(encoding='utf-8-sig')
a=s.index('\t\t\tFAvoidanceBucket& Bucket = InOutGrid.FindOrAdd'); b=s.index('\n\t\t}',a)
s=s[:a]+'''\t\t\tconst FVector Extent=Agent.bEnvironment ? FVector(Agent.Radius,Agent.Radius,0) : FVector::ZeroVector;
\t\t\tconst auto Min=MakeSpatialCell(Agent.Location-Extent),Max=MakeSpatialCell(Agent.Location+Extent);
\t\t\tfor (int32 X=Min.X; X<=Max.X; ++X) for (int32 Y=Min.Y; Y<=Max.Y; ++Y)
\t\t\t{
\t\t\t\tauto& Bucket=InOutGrid.FindOrAdd({X,Y}); Bucket.Add(AgentIndex);
\t\t\t\tMaximumBucketOccupancy=FMath::Max(MaximumBucketOccupancy,Bucket.Num());
\t\t\t}'''+s[b:]
s=s.replace('const double DistanceCutoffSquared = FMath::Square(static_cast<double>(DetectionDistance));','const double DistanceCutoffSquared = FMath::Square(static_cast<double>(DetectionDistance));\n\t\tTSet<int32> Seen; FNearestCandidateList Environment;')
s=s.replace('++Metrics.BucketEntriesVisited;', '++Metrics.BucketEntriesVisited;\n\t\t\t\t\tif (Seen.Contains(OtherIndex)) continue;\n\t\t\t\t\tSeen.Add(OtherIndex);')
s=s.replace('const double DistanceSquared = FVector::DistSquared(Agent.Location, Other.Location);', 'const double CenterDistance = FVector::Dist2D(Agent.Location, Other.Location);\n\t\t\t\t\tconst double DistanceSquared=FMath::Square(Other.bEnvironment ? FMath::Max(0.,CenterDistance-Other.Radius-Agent.Radius) : CenterDistance);')
a=s.index('\t\t\t\t\tint32 InsertIndex = 0;'); b=s.index('\n\t\t\t\t}',a)
piece=s[a:b].replace('OutCandidates', 'List').replace('MaximumNearestCandidates', 'Limit')
s=s[:a]+'\t\t\t\t\tauto& List=Other.bEnvironment ? Environment : OutCandidates;\n\t\t\t\t\tconst int32 Limit=Other.bEnvironment ? 4 : MaximumNearestCandidates;\n'+piece+s[b:]
a=s.index('\t\treturn Metrics;',s.index('const double CenterDistance'))
s=s[:a]+'''\t\t// Reserve two of the six CPA collider slots for environmental geometry.
\t\tconst int32 Reserved=FMath::Min(2,Environment.Num());
\t\tfor (int32 I=Reserved-1; I>=0; --I) OutCandidates.Insert(Environment[I],0);
\t\tif (OutCandidates.Num()>MaximumNearestCandidates) OutCandidates.SetNum(MaximumNearestCandidates,EAllowShrinking::No);
'''+s[a:]
p.write_text(s,encoding='utf-8')
p=root/'GuLiCommanderPredictiveAvoidanceProcessor.cpp'; s=p.read_text(encoding='utf-8-sig').replace('#include "Engine/World.h"', '#include "Engine/World.h"\n#include "Gameplay/Navigation/GuLiDynamicObstacleRegistry.h"')
s=s.replace('bAutoRegisterWithProcessingPhases = true;', 'bAutoRegisterWithProcessingPhases = true;\n\tbRequiresGameThreadExecution = true; // Snapshot publication touches the world registry; solver reads immutable values only.')
a=s.index('\n\t\tTArray<FAgentSnapshot> PolicyAgents;')
s=s[:a]+'''
\t\tif (auto* Registry=World->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>())
\t\t{
\t\t\tconst auto Snapshot=Registry->GetSnapshot();
\t\t\tfor (const auto& Obstacle : Snapshot->Obstacles)
\t\t\t{
\t\t\t\tauto& Entry=Agents.AddDefaulted_GetRef();
\t\t\t\tEntry.Agent.StableKey=(uint64(1)<<63)|Obstacle.Handle.Value;
\t\t\t\tEntry.Agent.Location=Obstacle.Location; Entry.Agent.Radius=Obstacle.RadiusCentimeters;
\t\t\t\tEntry.Agent.bParticipates=true; Entry.Agent.bEnvironment=true;
\t\t\t}
\t\t}
'''+s[a:]; p.write_text(s,encoding='utf-8')
p=root/'GuLiBattleAuthoritySubsystem.cpp'; s=p.read_text(encoding='utf-8-sig')
def rep(a,b):
 global s
 assert a in s,a[:100]
 s=s.replace(a,b,1)
rep('bool bGroundMechYielding = false;', '''uint32 ContactObstacle=0;
\t\tfloat BypassSide=0, ContactClearSeconds=0, FormationCorrectionAlpha=1.f, EnvironmentSpeedScale=1.f;
\t\tbool bGroundMechYielding = false;''')
# Helper uses only immutable data. Contact release hysteresis retains the first tangent side.
a=s.index('\n\tvoid SynchronizeSoldierWeapons(')
s=s[:a]+'''
\tFVector ConstrainEnvironmentVelocity(FSoldierRuntime& Soldier,const FGuLiDynamicObstacleSnapshot& Snapshot,FVector Velocity,float Dt,float Speed)
\t{
\t\tTArray<int32> Nearby; const float Reach=FMath::Max(80.f,Speed*.5f);
\t\tSnapshot.Query(Soldier.Location,Soldier.AvoidanceRadiusCentimeters+Reach+100.f,Nearby);
\t\tconst FGuLiDynamicObstacle* Chosen=nullptr; double Best=TNumericLimits<double>::Max();
\t\tif (const auto* I=Snapshot.ByHandle.Find(Soldier.ContactObstacle))
\t\t{
\t\t\tconst auto& O=Snapshot.Obstacles[*I];
\t\t\tif (FVector::Dist2D(Soldier.Location,O.Location)<O.RadiusCentimeters+Soldier.AvoidanceRadiusCentimeters+Reach+100.f
\t\t\t\t&& FMath::Abs(Soldier.Location.Z-O.Location.Z)<=300.f) Chosen=&O;
\t\t}
\t\tfor (int32 I : Nearby)
\t\t{
\t\t\tconst auto& O=Snapshot.Obstacles[I]; if (FMath::Abs(Soldier.Location.Z-O.Location.Z)>300.f) continue;
\t\t\tconst double Gap=FVector::Dist2D(Soldier.Location,O.Location)-O.RadiusCentimeters-Soldier.AvoidanceRadiusCentimeters;
\t\t\tif (!Chosen && Gap<Best && Gap<Reach) { Best=Gap; }
\t\t}
\t\tif (!Chosen && !Soldier.ContactObstacle)
\t\t\tfor (int32 I : Nearby)
\t\t\t{
\t\t\t\tconst auto& O=Snapshot.Obstacles[I];
\t\t\t\tconst double Gap=FVector::Dist2D(Soldier.Location,O.Location)-O.RadiusCentimeters-Soldier.AvoidanceRadiusCentimeters;
\t\t\t\tif (FMath::Abs(Soldier.Location.Z-O.Location.Z)<=300.f && Gap<=Best && Gap<Reach)
\t\t\t\t{ Chosen=&O; Best=Gap; }
\t\t\t}
\t\tfloat Pressure=0;
\t\tif (Chosen)
\t\t{
\t\t\tFVector Normal=(Soldier.Location-Chosen->Location).GetSafeNormal2D();
\t\t\tif (Normal.IsNearlyZero()) Normal=FVector(1,0,0);
\t\t\tconst FVector Left(-Normal.Y,Normal.X,0);
\t\t\tif (!Soldier.ContactObstacle)
\t\t\t{
\t\t\t\tSoldier.ContactObstacle=Chosen->Handle.Value;
\t\t\t\tconst double Side=FVector::DotProduct(Velocity,Left);
\t\t\t\tSoldier.BypassSide=FMath::Abs(Side)>1. ? (Side>0 ? 1.f : -1.f) : ((Soldier.SoldierId.Value&1) ? 1.f : -1.f);
\t\t\t}
\t\t\tconst double Gap=FVector::Dist2D(Soldier.Location,Chosen->Location)-Chosen->RadiusCentimeters-Soldier.AvoidanceRadiusCentimeters;
\t\t\tPressure=FMath::Clamp(1.f-static_cast<float>(Gap)/Reach,0.f,1.f);
\t\t\tif (Gap<Reach) Soldier.ContactClearSeconds=0; else Soldier.ContactClearSeconds+=Dt;
\t\t\tconst double Inward=FVector::DotProduct(Velocity,Normal);
\t\t\tif (Inward<0 && Pressure>0)
\t\t\t{
\t\t\t\t// At contact project onto the tangent, never apply alternating radial pushback.
\t\t\t\tVelocity-=Normal*Inward*Pressure;
\t\t\t\tVelocity+=Left*Soldier.BypassSide*Speed*.5f*Pressure;
\t\t\t}
\t\t\tif (Gap<2.f) { Velocity-=Normal*FMath::Min(0.,FVector::DotProduct(Velocity,Normal)); Velocity+=Normal*FMath::Min(Speed*.25f,static_cast<float>(2.-Gap)/FMath::Max(Dt,.001f)); }
\t\t}
\t\telse Soldier.ContactClearSeconds+=Dt;
\t\tif (Soldier.ContactClearSeconds>=.6f) { Soldier.ContactObstacle=0; Soldier.BypassSide=0; }
\t\tSoldier.EnvironmentSpeedScale=FMath::FInterpTo(Soldier.EnvironmentSpeedScale,1.f-.5f*Pressure,Dt,6.f);
\t\tSoldier.FormationCorrectionAlpha=FMath::FInterpTo(Soldier.FormationCorrectionAlpha,
\t\t\tSoldier.ConsecutiveSurfaceFailures ? 0.f : 1.f-Pressure,Dt,Pressure>0 ? 8.f : 2.f);
\t\t// Constrain every nearby obstacle, including those other than the latched steering obstacle.
\t\tfor (int32 I : Nearby)
\t\t{
\t\t\tconst auto& O=Snapshot.Obstacles[I]; if (FMath::Abs(Soldier.Location.Z-O.Location.Z)>300.f) continue;
\t\t\tconst FVector N=(Soldier.Location-O.Location).GetSafeNormal2D();
\t\t\tconst double Gap=FVector::Dist2D(Soldier.Location,O.Location)-O.RadiusCentimeters-Soldier.AvoidanceRadiusCentimeters;
\t\t\tconst double Into=FVector::DotProduct(Velocity,N);
\t\t\tconst double Limit=-FMath::Max(0.,Gap-1.)/FMath::Max(.001f,Dt);
\t\t\tif (Into<Limit) Velocity+=N*(Limit-Into);
\t\t}
\t\treturn Velocity.GetClampedToMaxSize(Speed*Soldier.EnvironmentSpeedScale);
\t}
'''+s[a:]
# Manual instantaneous separation contains soldiers only. Environment has independent constraints.
rep('AuthorityState->Soldiers.Num() + DynamicObstacles.Num()', 'AuthorityState->Soldiers.Num()')
a=s.index('\t\tfor (int32 ObstacleIndex = 0; ObstacleIndex < DynamicObstacles.Num(); ++ObstacleIndex)',s.index('const bool bPeriodicManualAvoidanceRefresh'))
b=s.index('\n\t\tif (bAnySoldierReceivesAvoidance)',a)
s=s[:a]+s[b:]
rep('FVector SlotVelocity = (SlotCorrectionTarget - Soldier.Location)', 'FVector SlotVelocity = (SlotCorrectionTarget - Soldier.Location)') # anchor below used by steering validation later
rep('* SlotCorrectionWeight);','* SlotCorrectionWeight * Soldier.FormationCorrectionAlpha);')
rep('const FVector TargetVelocity = (DesiredVelocities[SoldierIndex] + AvoidanceVelocity)', 'FVector TargetVelocity = (DesiredVelocities[SoldierIndex] + AvoidanceVelocity)')
rep('Soldier.Velocity = TargetVelocity.IsNearlyZero(1.0f)', '''if (auto* Registry=World->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>())
\t\t\t\tTargetVelocity=ConstrainEnvironmentVelocity(Soldier,*Registry->GetSnapshot(),TargetVelocity,MovementDeltaSeconds,MovementSpeedCentimetersPerSecond);
\t\t\tSoldier.Velocity = TargetVelocity.IsNearlyZero(1.0f)''')
# Re-project after interpolation too, so previous momentum cannot violate the inward constraint.
rep('if (!Soldier.Velocity.IsNearlyZero(1.0f))\n\t\t\t{\n\t\t\t\tconst FVector CandidateLocation', '''if (Soldier.ContactObstacle) Soldier.Velocity=TargetVelocity;
\t\t\tif (!Soldier.Velocity.IsNearlyZero(1.0f))
\t\t\t{
\t\t\t\tconst FVector CandidateLocation''')
rep('Soldier.Location = SurfaceLocation.Location;\n\t\t\t\t\tSoldier.LastValidNavLocation = SurfaceLocation;', 'Soldier.Velocity=(SurfaceLocation.Location-Soldier.Location)/FMath::Max(MovementDeltaSeconds,UE_SMALL_NUMBER);\n\t\t\t\t\tSoldier.Location = SurfaceLocation.Location;\n\t\t\t\t\tSoldier.LastValidNavLocation = SurfaceLocation;')
rep('Soldier.Location = SurfaceLocation.Location;\n\t\t\t\tSoldier.LastValidNavLocation = SurfaceLocation;\n\t\t\t\tSoldier.Velocity = YieldVelocity;', 'Soldier.Velocity=(SurfaceLocation.Location-Soldier.Location)/FMath::Max(FixedDeltaSeconds,UE_SMALL_NUMBER);\n\t\t\t\tSoldier.Location = SurfaceLocation.Location;\n\t\t\t\tSoldier.LastValidNavLocation = SurfaceLocation;')
rep('YieldVelocity.GetSafeNormal2D().Rotation().Yaw,', 'Soldier.Velocity.GetSafeNormal2D().Rotation().Yaw,')
# Refresh terminal/transport/dead states even when integration takes an early exit.
rep('if (Soldier.bExternalActionsLocked && Soldier.IsAlive()) continue;', 'if (Soldier.bExternalActionsLocked && Soldier.IsAlive()) { RefreshSoldierNavigationState(Soldier.SoldierId); continue; }')
rep('Health.WreckSecondsRemaining = FMath::Max(', 'RefreshSoldierNavigationState(Soldier.SoldierId);\n\t\t\tHealth.WreckSecondsRemaining = FMath::Max(')
p.write_text(s,encoding='utf-8')
