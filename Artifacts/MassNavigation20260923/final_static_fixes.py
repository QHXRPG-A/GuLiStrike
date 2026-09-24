from pathlib import Path
p=Path('Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.cpp')
s=p.read_text(encoding='utf-8-sig')
# Remove obsolete synchronous planning helpers after migrating all call sites.
a=s.index('\t// 只投影一次公共终点')
b=s.index('\tbool FindMoveAlongCurrentNavigationSurface(',a)
s=s[:a]+s[b:]
a=s.index('\tbool HasReachableSurfaceSegment(')
b=s.index('\t// Move-plan retries are expected',a)
s=s[:a]+s[b:]
# A stale polygon refresh during movement is also repair work under the same frame budget.
a=s.index('\tbool FindMoveAlongCurrentNavigationSurface(')
b=s.index('\t// Move-plan retries',a)
part=s[a:b].replace('FNavLocation& OutLocation,','FNavLocation& OutLocation,\n\t\tFGuLiNavigationWorkBudget& Budget,',1)
part=part.replace('if (!NavigationData.ProjectPoint(Start.Location, CurrentStart,',
'''FGuLiNavigationWorkBudget::FScope Scope(Budget);
\t\t\tif (!Budget.TakeProjection()) return false;
\t\t\tFGuLiNavigationWorkBudget::FQueryScope Query(Budget);
\t\t\tif (!NavigationData.ProjectPoint(Start.Location, CurrentStart,''',1)
s=s[:a]+part+s[b:]
# All remaining surface integrations are members of TickAuthority and share the same budget.
start=s.index('void UGuLiBattleAuthoritySubsystem::TickAuthority(')
before=s[:start]; after=s[start:]
import re
after,n=re.subn(r'(FindMoveAlongCurrentNavigationSurface\(\s*\*CommanderNavigationData,\s*Soldier.LastValidNavLocation,\s*[^,]+,\s*\w+,)(\s*this\);)',r'\1\n\t\t\t\t\tAuthorityState->PlanningBudget,\2',after)
assert n==3,n
s=before+after
# Reassignment is a bounded (25-member) planning work unit, including active formation width changes.
s=s.replace('&& Formation.SlotBySoldierId.Num() != ActiveIndices.Num())',
            '&& Formation.SlotBySoldierId.Num() != ActiveIndices.Num() && AuthorityState->PlanningBudget.CanWork())',1)
a=s.index('&& Formation.SlotBySoldierId.Num()')
b=s.index('\n\t\t\tAssignFormationSlots(',a)
s=s[:b]+'\n\t\t\tFGuLiNavigationWorkBudget::FScope ReassignScope(AuthorityState->PlanningBudget);'+s[b:]
s=s.replace('if (!Formation.bFinalApproachStarted)\n\t\t{\n\t\t\tconst int32 DesiredColumnCount',
'''if (!Formation.bFinalApproachStarted && AuthorityState->PlanningBudget.CanWork())
\t\t{
\t\t\tFGuLiNavigationWorkBudget::FScope WidthScope(AuthorityState->PlanningBudget);
\t\t\tconst int32 DesiredColumnCount''',1)
# Recovery start keeps exact XY after its tight projection, so stationary members can commit a refreshed reference.
needle='W->bReady=!W->bValid || !W->bHadFinal; W->Stage=1;'
s=s.replace(needle,'''if (W->bValid) { W->Start.Location.X=Soldier.Location.X; W->Start.Location.Y=Soldier.Location.Y; }
\t\t\t\t'''+needle,1)
s=s.replace('Soldier.Location.Equals(W->Start.Location,1.)',
'''(FVector::DistSquared2D(Soldier.Location,W->Start.Location)<=1.
\t\t\t&& FMath::Abs(Soldier.Location.Z-W->Start.Location.Z)<=MaximumSurfaceStepZCentimeters)''',1)
s=s.replace('bool bReady=false, bValid=false, bHadFinal=false, bArrived=false;', 'bool bReady=false, bValid=false, bHadFinal=false;')
p.write_text(s,encoding='utf-8')
