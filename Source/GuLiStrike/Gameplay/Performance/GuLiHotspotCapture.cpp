#include "Gameplay/Performance/GuLiPerformanceSubsystem.h"
#include "Gameplay/Performance/GuLiHotspotOptions.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Commander/Orders/GuLiUnitTaskSubsystem.h"
#include "Commander/Presentation/GuLiCommanderCameraPawn.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Gameplay/Stronghold/GuLiHybridControlPoint.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "ProfilingDebugging/MiscTrace.h"

struct FGuLiHotspotCapture
{
    bool Interaction=false,Combat=false,Running=false,Finished=false;
    FString Error;
    TSharedPtr<FJsonObject> Recipe;
    TSharedPtr<FJsonObject> CensusStart,CensusEnd,CombatPreparation;
    TWeakObjectPtr<UGuLiCommanderNetSyncComponent> Sync;
    TWeakObjectPtr<UGuLiBattleAuthoritySubsystem> Army;
    TArray<FGuLiSoldierId> Red,Wounded;
    TArray<FGuLiHybridObjectKey> RedKeys,WoundedKeys;
    TArray<FVector> Starts;
    FVector Target=FVector::ZeroVector;
    FVector WoundedFocus=FVector::ZeroVector;
    TWeakObjectPtr<AGuLiCommanderCameraPawn> Camera;
    FDelegateHandle AckHandle,PositionHandle;
    struct FHit {FGuid Event;FGuLiDamageCommitResult Result;uint32 Replies=0;};
    TArray<FHit> Hits;
    TMap<uint32,FGuLiCommandAck> Acks;
    TMap<uint32,uint32> AckCounts;
    struct FMoveSample {uint32 Command;FGuLiSoldierId Soldier;FVector Position;double Submitted;double DisplacementMs=-1,AppliedMs=-1;};
    TArray<FMoveSample> MoveSamples;
    TMap<FGuLiSoldierId,int32> PendingPositions;
    TMap<uint32,double> SubmittedAt,ReceiptMilliseconds;
    TArray<FString> Actions;
    double Started=0,Elapsed=0;
    uint64 StartQpc=0,EndQpc=0;
    int32 Action=0,HitWave=0,Selections=0,Moved=0;
    int32 ResponseGroupSize=24,ResponseAction=0;
    uint64 FactEquivalenceChecks=0;
    float MinimumHeight=MAX_flt,MaximumHeight=0;
    FVector CameraStart=FVector::ZeroVector;
    ~FGuLiHotspotCapture()
    {
        if (Sync.IsValid()) Sync->OnCommandAckChanged.Remove(AckHandle);
#if !UE_BUILD_SHIPPING
        if (Army.IsValid()) Army->OnPositionApplied.Remove(PositionHandle);
#endif
    }
};
namespace
{
TArray<TSharedPtr<FJsonValue>> Point(const FVector& P)
{return {MakeShared<FJsonValueNumber>(P.X),MakeShared<FJsonValueNumber>(P.Y),MakeShared<FJsonValueNumber>(P.Z)};}
bool ReadPoint(const FJsonObject& J,const TCHAR* Name,FVector& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* V=nullptr;
    if (!J.TryGetArrayField(Name,V) || V->Num()!=3) return false;
    double X,Y,Z;if (!(*V)[0]->TryGetNumber(X) || !(*V)[1]->TryGetNumber(Y) || !(*V)[2]->TryGetNumber(Z)) return false;
    Out={X,Y,Z};return !Out.ContainsNaN();
}
TSharedRef<FJsonObject> SimulationShape(const FJsonObject& Census)
{
    auto Shape=MakeShared<FJsonObject>();const auto Definitions=Census.GetObjectField(TEXT("by_definition"));
    for (const auto& [Definition,Value]:Definitions->Values)
    {
        if (!Definition.StartsWith(TEXT("unit/")) && !Definition.StartsWith(TEXT("building/"))
            && !Definition.StartsWith(TEXT("territory/"))) continue;
        const auto Row=Value->AsObject();auto Counts=MakeShared<FJsonObject>();
        for (const auto* Field:{TEXT("alive"),TEXT("local_alive_owned"),TEXT("peer_alive_owned"),TEXT("local_simulation"),TEXT("dormant")})
            Counts->SetNumberField(Field,Row->GetNumberField(Field));
        Shape->SetObjectField(Definition,Counts);
    }
    return Shape;
}
bool CheckCensus(const FJsonObject& Recipe,const FJsonObject& Census,FString& Error,bool CheckShape=true)
{
    if (!Census.GetBoolField(TEXT("ready"))) {Error=TEXT("Simulation census is not ready");return false;}
    const auto Definitions=Census.GetObjectField(TEXT("by_definition"));
    const TSharedPtr<FJsonObject>* Minimum=nullptr;
    if (Recipe.TryGetObjectField(TEXT("minimum_local_simulation"),Minimum))
    {
        for (const auto& [Definition,Value]:(*Minimum)->Values)
        {
            double Required=0;const TSharedPtr<FJsonObject>* Row=nullptr;
            if (!Value->TryGetNumber(Required) || !FMath::IsFinite(Required) || Required<0 || Required!=FMath::FloorToDouble(Required)
                || !Definitions->TryGetObjectField(Definition,Row) || (*Row)->GetNumberField(TEXT("local_simulation"))<Required)
            {Error=TEXT("Insufficient locally simulated live workload: ")+Definition;return false;}
        }
    }
    else if (Census.GetNumberField(TEXT("live_soldiers"))!=600)
    {Error=TEXT("Default preparation requires 600 live soldiers; high-load recipes must declare local authority minima");return false;}
    const TSharedPtr<FJsonObject>* Expected=nullptr;
    if (CheckShape && Recipe.TryGetObjectField(TEXT("simulation_shape"),Expected))
    {
        const auto Actual=SimulationShape(Census);
        if ((*Expected)->Values.Num()!=Actual->Values.Num()) {Error=TEXT("Paired simulation definitions changed");return false;}
        for (const auto& [Definition,Value]:(*Expected)->Values)
        {
            const auto Wanted=Value->AsObject();const TSharedPtr<FJsonObject>* Row=nullptr;
            if (!Wanted || !Actual->TryGetObjectField(Definition,Row)) {Error=TEXT("Missing paired simulation definition: ")+Definition;return false;}
            for (const auto& [Field,Number]:Wanted->Values)
            {
                double Required=0,Present=0;
                if (!Number->TryGetNumber(Required) || !(*Row)->TryGetNumberField(Field,Present) || Required!=Present)
                {Error=TEXT("Paired simulation authority/life count changed: ")+Definition+TEXT("/")+Field;return false;}
            }
        }
    }
    return true;
}
void Hits(UWorld& W,const TSharedPtr<FGuLiHotspotCapture>& C,bool Initial)
{
    auto* Army=W.GetSubsystem<UGuLiBattleAuthoritySubsystem>();auto* H=W.GetSubsystem<UGuLiHybridWorldSubsystem>();
    auto* Ledger=W.GetSubsystem<UGuLiDamageLedgerSubsystem>();auto* Perf=W.GetSubsystem<UGuLiPerformanceSubsystem>();
    if (!Army || !H || !Ledger || C->Red.IsEmpty()) {C->Error=TEXT("Damage dependencies missing");return;}
    for (int32 I=0;I<C->Wounded.Num();++I)
    {
        FGuLiHybridObjectState O;if (!H->GetObject(C->WoundedKeys[I],O) || O.LifeState!=1) {C->Error=TEXT("Wounded target is not alive");return;}
        FGuLiDamageRequest R;R.MatchEpoch=Ledger->GetMatchEpoch();R.DamageEventId=FGuid::NewGuid();R.ShotId=FGuid::NewGuid();
        R.RootEventId=R.DamageEventId;R.Source=Army->MakeSoldierTargetHandle(C->Red[I]);R.Target=Army->MakeSoldierTargetHandle(C->Wounded[I]);
        // The production manifest determines damage and cooldown, including in performance workloads.
        R.SkillId=TEXT("Strafe");R.WeaponBinding.SlotId=TEXT("BasicAttack");
        R.Damage=10.f;R.HitLocation=O.Location;
        const int32 Index=C->Hits.Add({R.DamageEventId});const TWeakPtr<FGuLiHotspotCapture> Weak=C;
        const FGuid Submitted=Ledger->SubmitDamage(R,Perf,[Weak,Index](const FGuLiDamageCommitResult& Result)
        {
            if (auto State=Weak.Pin();State && State->Hits.IsValidIndex(Index))
            {auto& Hit=State->Hits[Index];Hit.Result=Result;++Hit.Replies;if (!Result.WasAccepted()) State->Error=TEXT("A formal damage request was rejected");}
        });
        if (Submitted!=R.DamageEventId) C->Error=TEXT("A formal damage request was not admitted");
    }
}
void Select(const TSharedPtr<FGuLiHotspotCapture>& C,int32 Group)
{
#if !UE_BUILD_SHIPPING
    if (!C->Sync.IsValid()) {C->Error=TEXT("Commander input is unavailable");return;}
    // The prepared groups are recalled through the same input path used by number keys.
    C->Sync->SubmitControlGroup(uint8(Group),false,false,false,false);
    int32 Count=0;for (const auto& Cohort:C->Sync->GetSelectionState().Cohorts) Count+=Cohort.MemberIds.Num();
    if (Count!=C->ResponseGroupSize) C->Error=TEXT("A group recall did not select the frozen response group");else ++C->Selections;
#endif
}
void Move(const TSharedPtr<FGuLiHotspotCapture>& C,uint32 Id,const FVector& P)
{
    if (!C->Sync.IsValid()) {C->Error=TEXT("Commander input is unavailable");return;}
    const double Now=FPlatformTime::Seconds();C->SubmittedAt.Add(Id,Now);
    auto* Army=C->Sync->GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
    for (int32 I=Id==900001?0:24,End=I+C->ResponseGroupSize;I<End;++I)
    {
        EGuLiTeam Team;uint16 Type=0;FVector Position;
        if (!Army||!Army->GetTaskSoldierInfo(C->Red[I],Team,Type,Position)) {C->Error=TEXT("Command latency source missing");return;}
        C->PendingPositions.Add(C->Red[I],C->MoveSamples.Add({Id,C->Red[I],Position,Now}));
    }
    FGuLiUnitTaskCommand Command;Command.CommandId=Id;Command.Target=P;Command.TargetIntent=TEXT("Move");Command.bGroundMoveOnly=true;
    C->Sync->SubmitOrderedTask(Command);
}
}
FString UGuLiPerformanceSubsystem::PrepareHotspotSelection(const TArray<int32>& Soldiers)
{
#if !UE_BUILD_SHIPPING
    if (bCapturing || Soldiers.IsEmpty() || Soldiers.Num()>25) return TEXT("{\"error\":\"Preparation requires 1..25 units outside a capture\"}");
    auto* W=GetWorld();auto* PC=W?W->GetFirstPlayerController():nullptr;
    auto* Sync=PC?PC->FindComponentByClass<UGuLiCommanderNetSyncComponent>():nullptr;
    auto* H=W?W->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;
    if (!Sync || !Sync->IsSoldierStreamReady() || !H || !H->CanPublishFacts()) return TEXT("{\"error\":\"Commander is not ready\"}");
    TArray<FGuLiSoldierId> Ids;TSet<int32> Seen;
    for (int32 Id:Soldiers)
    {
        if (Id<=0 || Seen.Contains(Id)) return TEXT("{\"error\":\"Invalid or duplicate soldier identity\"}");
        Seen.Add(Id);Ids.Add(FGuLiSoldierId(uint32(Id)));
    }
    // Existing review selection verifies authority, life and the current entity.
    // It neither creates units nor bypasses the subsequent skill's validation.
    if (!Sync->PrepareReviewSoldierSelection(Ids,910001)) return TEXT("{\"error\":\"Selection rejected\"}");
    auto Result=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Actual;
    for (const auto& Cohort:Sync->GetSelectionState().Cohorts)
        for (auto Id:Cohort.MemberIds) Actual.Add(MakeShared<FJsonValueNumber>(Id.Value));
    Result->SetArrayField(TEXT("soldiers"),Actual);FString Json;
    FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Json));return Json;
#else
    return TEXT("{\"error\":\"development only\"}");
#endif
}

FString UGuLiPerformanceSubsystem::PrepareHotspotCombat(int32 LocalUnits,int32 UnitType,int32 SkipLocalUnits)
{
#if !UE_BUILD_SHIPPING
    if (bCapturing || LocalUnits<1 || LocalUnits>150 || UnitType<0 || UnitType>MAX_uint16 || SkipLocalUnits<0 || SkipLocalUnits>10000)
        return TEXT("{\"error\":\"Invalid combat preparation outside capture\"}");
    auto* W=GetWorld();auto* H=W?W->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;
    auto* Army=W?W->GetSubsystem<UGuLiBattleAuthoritySubsystem>():nullptr;
    if (!H || !Army || !H->CanPublishFacts()) return TEXT("{\"error\":\"Combat preparation requires a ready world\"}");
    TArray<FGuLiHybridObjectState> Objects;H->GetObjects(Objects);
    Objects.Sort([](const auto& A,const auto& B){return A.Ordinal<B.Ordinal;});
    TArray<FGuLiHybridObjectState> Sources;int32 Skipped=0;
    for (const auto& O:Objects) if (O.Key.Kind==EGuLiHybridObjectKind::Soldier && O.LifeState==1 && H->HasSimulation(O.Key)
        && (UnitType?O.Definition==FName(*FString::Printf(TEXT("unit/%d"),UnitType)):(O.Definition==TEXT("unit/1")||O.Definition==TEXT("unit/2"))))
        {if (Skipped++<SkipLocalUnits) continue;Sources.Add(O);if (Sources.Num()==LocalUnits) break;}
    if (Sources.Num()!=LocalUnits) return TEXT("{\"error\":\"Insufficient live locally simulated combat units\"}");
    const auto* Enemy=Objects.FindByPredicate([&](const auto& O){return O.Key.Kind==EGuLiHybridObjectKind::Soldier
        &&O.LifeState==1&&O.Team!=Sources[0].Team&&O.Team!=EGuLiTeam::Unassigned&&O.Definition==TEXT("unit/2");});
    if (!Enemy) return TEXT("{\"error\":\"No live enemy combat anchor\"}");
    EGuLiTeam EnemyTeam;uint16 EnemyType=0;FVector EnemyPosition;
    if (!Army->GetTaskSoldierInfo(FGuLiSoldierId(Enemy->Ordinal),EnemyTeam,EnemyType,EnemyPosition))
        return TEXT("{\"error\":\"Enemy applied entity position is unavailable\"}");
    TArray<FGuLiMassExternalUnit> Units;TArray<TSharedPtr<FJsonValue>> Rows;
    TArray<FVector> Before;
    const auto Token=FGuid::NewGuid();
    for (int32 I=0;I<Sources.Num();++I)
    {
        EGuLiTeam Team;uint16 Type=0;FVector Previous;
        if (!Army->GetTaskSoldierInfo(FGuLiSoldierId(Sources[I].Ordinal),Team,Type,Previous))
            return TEXT("{\"error\":\"Source applied entity position is unavailable\"}");
        Before.Add(Previous);
        FVector Surface;const FVector Requested=EnemyPosition+FVector((I%10-4.5)*250,1800+(I/10)*250,0);
        bool Found=Army->ProjectExternalUnitLocation(Requested,Surface);
        for (const double Radius:{100.,200.,400.,800.})
        {
            for (int32 Direction=0;Direction<8&&!Found;++Direction)
            {
                const double Angle=Direction*UE_DOUBLE_PI/4;
                Found=Army->ProjectExternalUnitLocation(Requested+FVector(FMath::Cos(Angle)*Radius,FMath::Sin(Angle)*Radius,0),Surface);
            }
            if (Found) break;
        }
        if (!Found) return TEXT("{\"error\":\"Combat preparation location outside navigation\"}");
        FGuLiMassExternalUnit U;U.Id=FGuLiSoldierId(Sources[I].Ordinal);U.Transform=FTransform(FRotator::ZeroRotator,Surface);Units.Add(U);
    }
    // Existing displacement invalidates in-flight movement/navigation candidates.
    // No health, fire cadence, population, ownership or damage outcomes are injected.
    if (!Army->ApplyExternalUnitState(Units,Token,false,false,true)) return TEXT("{\"error\":\"Native spatial preparation rejected\"}");
    for (int32 I=0;I<Units.Num();++I)
    {
        EGuLiTeam Team;uint16 Type=0;FVector Position;
        if (!Army->GetTaskSoldierInfo(Units[I].Id,Team,Type,Position)||!Position.Equals(Units[I].Transform.GetLocation(),1.))
            return TEXT("{\"error\":\"Applied spatial readback differs\"}");
        auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("ordinal"),Units[I].Id.Value);
        J->SetStringField(TEXT("object"),Sources[I].Key.StableId.ToString(EGuidFormats::Digits));
        J->SetStringField(TEXT("definition"),Sources[I].Definition.ToString());
        J->SetNumberField(TEXT("generation"),Sources[I].Key.LifeGeneration);
        J->SetArrayField(TEXT("before"),Point(Before[I]));J->SetArrayField(TEXT("applied"),Point(Position));Rows.Add(MakeShared<FJsonValueObject>(J));
    }
    auto Result=MakeShared<FJsonObject>();Result->SetArrayField(TEXT("positions"),Rows);
    Result->SetStringField(TEXT("token"),Token.ToString(EGuidFormats::Digits));Result->SetNumberField(TEXT("enemy_ordinal"),Enemy->Ordinal);
    Result->SetArrayField(TEXT("enemy_position"),Point(EnemyPosition));FString Json;
    FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Json));return Json;
#else
    return TEXT("{\"error\":\"development only\"}");
#endif
}
FString UGuLiPerformanceSubsystem::PrepareHotspotTerritories(int32 NeutralTerritories)
{
#if !UE_BUILD_SHIPPING
    if (bCapturing || NeutralTerritories<1 || NeutralTerritories>47)
        return TEXT("{\"error\":\"Prepare 1..47 neutral territories outside capture\"}");
    auto* W=GetWorld();auto* H=W?W->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;
    auto* Army=W?W->GetSubsystem<UGuLiBattleAuthoritySubsystem>():nullptr;
    if (!H || !Army || !H->CanPublishFacts()) return TEXT("{\"error\":\"Territory preparation requires a ready world\"}");
    TArray<FGuLiHybridObjectState> Objects;H->GetObjects(Objects);
    Objects.Sort([](const auto& A,const auto& B){return A.Ordinal<B.Ordinal;});
    TArray<FGuLiHybridObjectState> Sources,Targets;
    for (const auto& O:Objects)
    {
        // Preserve pioneers in their original formations for the frozen summon inputs.
        if (O.Key.Kind==EGuLiHybridObjectKind::Soldier && O.LifeState==1 && H->HasSimulation(O.Key)
            && O.Definition==TEXT("unit/2")) Sources.Add(O);
        if (O.Definition==TEXT("territory/7") && O.LifeState==1
            && GuLiHybridControlPoint::Read(O).Owner==EGuLiTeam::Unassigned) Targets.Add(O);
    }
    if (Sources.Num()<NeutralTerritories || Targets.Num()<NeutralTerritories)
        return TEXT("{\"error\":\"Insufficient local units or neutral territories\"}");
    const EGuLiTeam Team=Sources[0].Team;
    // Fixed order starts at this team's side of the existing map, away from the opposing army.
    Targets.Sort([Team](const auto& A,const auto& B)
    {
        if (A.Location.Y!=B.Location.Y) return Team==EGuLiTeam::Red?A.Location.Y>B.Location.Y:A.Location.Y<B.Location.Y;
        return A.Ordinal<B.Ordinal;
    });
    TArray<FGuLiMassExternalUnit> Units;TArray<FVector> Before;TArray<TSharedPtr<FJsonValue>> Rows;
    for (int32 I=0;I<NeutralTerritories;++I)
    {
        EGuLiTeam ActualTeam;uint16 Type=0;FVector Previous,Surface;
        if (Sources[I].Team!=Team || !Army->GetTaskSoldierInfo(FGuLiSoldierId(Sources[I].Ordinal),ActualTeam,Type,Previous))
            return TEXT("{\"error\":\"Territory source lost its live local entity\"}");
        bool Found=false;
        for (const double Radius:{1500.,2000.,2500.,3000.})
        {
            for (int32 Direction=0;Direction<8&&!Found;++Direction)
            {
                const double Angle=Direction*UE_DOUBLE_PI/4;
                Found=Army->ProjectExternalUnitLocation(Targets[I].Location+FVector(FMath::Cos(Angle)*Radius,FMath::Sin(Angle)*Radius,0),Surface);
            }
            if (Found) break;
        }
        if (!Found) return TEXT("{\"error\":\"Territory has no navigable preparation point\"}");
        FGuLiMassExternalUnit U;U.Id=FGuLiSoldierId(Sources[I].Ordinal);U.Transform=FTransform(FRotator::ZeroRotator,Surface);
        Units.Add(U);Before.Add(Previous);
    }
    const auto Token=FGuid::NewGuid();
    if (!Army->ApplyExternalUnitState(Units,Token,false,false,true))
        return TEXT("{\"error\":\"Territory spatial preparation rejected\"}");
    for (int32 I=0;I<Units.Num();++I)
    {
        EGuLiTeam ActualTeam;uint16 Type=0;FVector Position;
        if (!Army->GetTaskSoldierInfo(Units[I].Id,ActualTeam,Type,Position) || !Position.Equals(Units[I].Transform.GetLocation(),1.))
            return TEXT("{\"error\":\"Territory position readback differs\"}");
        auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("ordinal"),Units[I].Id.Value);
        J->SetStringField(TEXT("object"),Sources[I].Key.StableId.ToString(EGuidFormats::Digits));
        J->SetStringField(TEXT("territory"),Targets[I].Key.StableId.ToString(EGuidFormats::Digits));
        J->SetArrayField(TEXT("before"),Point(Before[I]));J->SetArrayField(TEXT("applied"),Point(Position));
        Rows.Add(MakeShared<FJsonValueObject>(J));
    }
    auto Result=MakeShared<FJsonObject>();Result->SetArrayField(TEXT("positions"),Rows);
    Result->SetNumberField(TEXT("logical_tick"),double(H->GetLogicalTick()));
    FString Json;FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Json));return Json;
#else
    return TEXT("{\"error\":\"development only\"}");
#endif
}
FString UGuLiPerformanceSubsystem::PrepareHotspotWorkload(const FString& FrozenJson,bool Interaction)
{
#if !UE_BUILD_SHIPPING
    if (bCapturing) return TEXT("{\"error\":\"capture is active\"}");
    bHotspotPending=false;Hotspot=MakeShared<FGuLiHotspotCapture>();auto& C=*Hotspot;C.Interaction=Interaction;
    auto* W=GetWorld();auto* PC=W->GetFirstPlayerController();auto* H=W->GetSubsystem<UGuLiHybridWorldSubsystem>();
    auto* Army=W->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
    C.Sync=PC?PC->FindComponentByClass<UGuLiCommanderNetSyncComponent>():nullptr;
    C.Camera=PC?Cast<AGuLiCommanderCameraPawn>(PC->GetPawn()):nullptr;
    if (!H || !H->CanPublishFacts() || !Army || !C.Sync.IsValid() || !C.Sync->IsSoldierStreamReady() || !C.Camera.IsValid())
    {C.Error=TEXT("The Commander session is not ready");return GetCaptureJson();}
    TArray<FGuLiHybridObjectState> Objects;H->GetObjects(Objects); // Preparation only.
    Objects.Sort([](const auto& A,const auto& B){return A.Ordinal<B.Ordinal;});
    C.Recipe=MakeShared<FJsonObject>();
    if (!FrozenJson.IsEmpty() && !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FrozenJson),C.Recipe))
    {C.Error=TEXT("Invalid frozen recipe");return GetCaptureJson();}
    C.CensusStart=H->GetSimulationCensus();
    if (!CheckCensus(*C.Recipe,*C.CensusStart,C.Error,false)) return GetCaptureJson();
    FString Scenario;C.Recipe->TryGetStringField(TEXT("scenario"),Scenario);C.Combat=Scenario==TEXT("combat");
    C.ResponseGroupSize=Interaction||C.Combat?24:1;
    C.Recipe->SetNumberField(TEXT("response_probe_version"),1);
    C.Recipe->SetNumberField(TEXT("response_probe_count"),C.ResponseGroupSize*2);
    const bool FrozenUnits=C.Recipe->HasField(TEXT("red_ids"));
    const bool HighLoad=C.Recipe->HasField(TEXT("minimum_local_simulation"));
    TSet<uint32> CombatParticipants;
    if (C.Combat)
        for (const auto& O:Objects)
            if (O.Key.Kind==EGuLiHybridObjectKind::Soldier && O.LifeState==1 && H->HasSimulation(O.Key)
                && (O.Definition==TEXT("unit/1") || O.Definition==TEXT("unit/2")))
            {CombatParticipants.Add(O.Ordinal);if (CombatParticipants.Num()==150) break;}
    auto Pick=[&](const TCHAR* Field,EGuLiTeam Team,int32 Count,TArray<FGuLiSoldierId>& Ids,TArray<FGuLiHybridObjectKey>& Keys)
    {
        TArray<TSharedPtr<FJsonValue>> Values;
        if (!FrozenUnits)
        {
            for (const auto& O:Objects) if (O.Key.Kind==EGuLiHybridObjectKind::Soldier && O.LifeState==1 && O.Team==Team && O.Ordinal
                && (Team!=EGuLiTeam::Red || (H->HasSimulation(O.Key) && !CombatParticipants.Contains(O.Ordinal)
                    && (!HighLoad || O.Definition==TEXT("unit/1")))))
            {Values.Add(MakeShared<FJsonValueNumber>(O.Ordinal));if (Values.Num()==Count) break;}
            C.Recipe->SetArrayField(Field,Values);
        }
        const TArray<TSharedPtr<FJsonValue>>* Input=nullptr;
        if (!C.Recipe->TryGetArrayField(Field,Input) || Input->Num()!=Count) return false;
        TSet<uint32> Seen;
        for (const auto& V:*Input)
        {
            double Number=0;if (!V->TryGetNumber(Number) || Number<1 || Number>MAX_uint32 || Number!=FMath::FloorToDouble(Number) || Seen.Contains(uint32(Number))) return false;
            Seen.Add(uint32(Number));const auto* O=Objects.FindByPredicate([&](const auto& Object){return Object.Key.Kind==EGuLiHybridObjectKind::Soldier && Object.Ordinal==uint32(Number) && Object.Team==Team && Object.LifeState==1;});
            if (!O || (Team==EGuLiTeam::Red && (!H->HasSimulation(O->Key) || CombatParticipants.Contains(O->Ordinal)))) return false;
            Ids.Add(FGuLiSoldierId(O->Ordinal));Keys.Add(O->Key);
        }
        return true;
    };
    if (!Pick(TEXT("red_ids"),EGuLiTeam::Red,48,C.Red,C.RedKeys) || !Pick(TEXT("wounded_ids"),EGuLiTeam::Blue,24,C.Wounded,C.WoundedKeys))
    {C.Error=TEXT("Frozen unit identities do not match this session");return GetCaptureJson();}
    if (HighLoad)
    {
        // Summons cause normal crowd separation during setup. Give both variants
        // the same clear interaction formation, away from the capture participants,
        // instead of accepting different origins or relaxing the position check.
        if (FrozenUnits && (!C.Recipe->HasField(TEXT("preparation_layout_version"))
            || C.Recipe->GetNumberField(TEXT("preparation_layout_version"))!=1))
        {C.Error=TEXT("High-load recipe predates the fixed interaction formation");return GetCaptureJson();}
        TArray<FGuLiMassExternalUnit> Formation;
        for (int32 I=0;I<C.Red.Num();++I)
        {
            EGuLiTeam Team;uint16 Type=0;FVector Previous,Surface;
            if (!Army->GetTaskSoldierInfo(C.Red[I],Team,Type,Previous) || Type!=1
                || !Army->ProjectExternalUnitLocation(FVector(-35000.+(I%8)*650.,62000.+(I/8)*650.,1200.),Surface))
            {C.Error=TEXT("High-load interaction formation has no valid local pioneer/navigation surface");return GetCaptureJson();}
            FGuLiMassExternalUnit U;U.Id=C.Red[I];U.Transform=FTransform(FRotator::ZeroRotator,Surface);Formation.Add(U);
        }
        if (!Army->ApplyExternalUnitState(Formation,FGuid::NewGuid(),false,false,true))
        {C.Error=TEXT("High-load interaction spatial preparation rejected");return GetCaptureJson();}
        C.Recipe->SetNumberField(TEXT("preparation_layout_version"),1);
    }
    TArray<TSharedPtr<FJsonValue>> Positions;
    for (auto Id:C.Red)
    {
        EGuLiTeam Team;uint16 Type=0;FVector Location;
        if (!Army->GetTaskSoldierInfo(Id,Team,Type,Location)) {C.Error=TEXT("A frozen unit position is unavailable");return GetCaptureJson();}
        C.Starts.Add(Location);auto J=MakeShared<FJsonObject>();J->SetArrayField(TEXT("position"),Point(Location));Positions.Add(MakeShared<FJsonValueObject>(J));
    }
    if (!FrozenUnits)
    {
        FVector Center=FVector::ZeroVector;for (int32 I=0;I<24;++I) Center+=C.Starts[I]/24.;
        if (!Army->ProjectExternalUnitLocation(Center+FVector(1800,1200,0),C.Target)) {C.Error=TEXT("The frozen movement target has no navigation surface");return GetCaptureJson();}
        C.Recipe->SetArrayField(TEXT("move_target"),Point(C.Target));C.Recipe->SetArrayField(TEXT("red_positions"),Positions);
    }
    else
    {
        const TArray<TSharedPtr<FJsonValue>>* Expected=nullptr;
        if (!ReadPoint(*C.Recipe,TEXT("move_target"),C.Target) || !C.Recipe->TryGetArrayField(TEXT("red_positions"),Expected) || Expected->Num()!=48)
        {C.Error=TEXT("Frozen positions/route missing");return GetCaptureJson();}
        for (int32 I=0;I<48;++I)
        {
            const auto J=(*Expected)[I]->AsObject();FVector P;
            if (!J || !ReadPoint(*J,TEXT("position"),P) || FVector::Dist(P,C.Starts[I])>2)
            {C.Error=TEXT("Initial unit positions changed from the frozen recipe");return GetCaptureJson();}
        }
    }
    C.CameraStart=C.Camera->GetActorLocation();
    if (!FrozenUnits)
    {
        for (const auto& Key:C.WoundedKeys) {FGuLiHybridObjectState O;H->GetObjectView(Key,O);C.WoundedFocus+=O.Location/C.WoundedKeys.Num();}
        C.Recipe->SetArrayField(TEXT("wounded_focus"),Point(C.WoundedFocus));
    }
    else if (!ReadPoint(*C.Recipe,TEXT("wounded_focus"),C.WoundedFocus))
    {C.Error=TEXT("Frozen wound-display camera focus is missing");return GetCaptureJson();}
    const TWeakPtr<FGuLiHotspotCapture> Weak=Hotspot;
    C.Army=Army;
    C.PositionHandle=C.Army->OnPositionApplied.AddLambda([Weak](FGuLiSoldierId Soldier,uint32 Command,const FVector& Position,double AppliedAt)
    {
        if (auto S=Weak.Pin();S && S->Running)
            if (const auto* Index=S->PendingPositions.Find(Soldier))
            {
                auto& Sample=S->MoveSamples[*Index];
                if (Sample.Command==Command && Sample.AppliedMs<0 && FVector::DistSquared2D(Position,Sample.Position)>4.)
                {Sample.AppliedMs=(AppliedAt-Sample.Submitted)*1000.;S->PendingPositions.Remove(Soldier);}
            }
    });
    C.AckHandle=C.Sync->OnCommandAckChanged.AddLambda([Weak](const FGuLiCommandAck& Ack)
    {
        if (auto S=Weak.Pin();S && Ack.ClientCommandId>=900001 && Ack.ClientCommandId<=900002)
        {
            ++S->AckCounts.FindOrAdd(Ack.ClientCommandId);S->Acks.Add(Ack.ClientCommandId,Ack);
            if (const auto* At=S->SubmittedAt.Find(Ack.ClientCommandId)) S->ReceiptMilliseconds.Add(Ack.ClientCommandId,(FPlatformTime::Seconds()-*At)*1000.);
            if (!Ack.IsAccepted()) S->Error=TEXT("A formal movement request was rejected");
        }
    });
    // Every scenario measures real command application and exactly-once receipts.
    // The static bulk workload has two single-unit probes. Combat probes use a
    // reserved local formation outside the fixed 150-unit engagement.
    for (int32 Group=0;Group<2;++Group)
    {
        if (!C.Sync->PrepareReviewSoldierSelection(MakeArrayView(C.Red.GetData()+Group*24,C.ResponseGroupSize),800001+Group))
        {C.Error=TEXT("Cannot prepare the fixed response selection");return GetCaptureJson();}
        C.Sync->SubmitControlGroup(uint8(Group),true,false,false,false);
    }
    Select(Hotspot,0);
    if (Interaction) Hits(*W,Hotspot,true);
    ValidateHotspotBehaviorFacts();return GetCaptureJson();
#else
    return TEXT("{\"error\":\"development only\"}");
#endif
}
bool UGuLiPerformanceSubsystem::ValidateHotspotBehaviorFacts()
{
#if !UE_BUILD_SHIPPING
    if (!Hotspot || bCapturing) return false;
    const auto* Tasks=GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>();
    if (!Tasks) {Hotspot->Error=TEXT("Task fact validation has no subsystem");return false;}
    for (const auto& Pair:Tasks->States)
    {
        const uint32 All=Tasks->GetBehaviorFacts(Pair.Key);
        for (uint32 Bit=0;Bit<32;++Bit)
        {
            const uint32 Mask=uint32(1)<<Bit;
            if (Tasks->GetBehaviorFacts(Pair.Key,Mask)!=(All&Mask))
            {Hotspot->Error=TEXT("Masked task facts differ from the complete observation");return false;}
            ++Hotspot->FactEquivalenceChecks;
        }
    }
    return true;
#else
    return false;
#endif
}
bool UGuLiPerformanceSubsystem::StartHotspotWindow()
{
#if !UE_BUILD_SHIPPING
    if (!Hotspot || bCapturing || !Hotspot->Error.IsEmpty() || Hotspot->Running || Hotspot->Finished) return false;
    auto* CensusWorld=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    if (!CensusWorld) return false;
    double Scheduled=0;Hotspot->Recipe->TryGetNumberField(TEXT("start_logical_tick"),Scheduled);
    if (!FMath::IsFinite(Scheduled) || Scheduled<0 || Scheduled>double(MAX_int32) || Scheduled!=FMath::FloorToDouble(Scheduled))
    {Hotspot->Error=TEXT("Invalid native capture logical start tick");return false;}
    if (CensusWorld->GetLogicalTick()<uint64(Scheduled)) {bHotspotPending=true;return true;}
    bHotspotPending=false;
    Hotspot->CensusStart=CensusWorld->GetSimulationCensus();
    if (!CheckCensus(*Hotspot->Recipe,*Hotspot->CensusStart,Hotspot->Error)) return false;
    Hotspot->Recipe->SetObjectField(TEXT("simulation_shape"),SimulationShape(*Hotspot->CensusStart));
    if (Hotspot->Combat)
    {
        const FString Prepared=PrepareHotspotCombat(150);
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Prepared),Hotspot->CombatPreparation)
            || !Hotspot->CombatPreparation || Hotspot->CombatPreparation->HasField(TEXT("error")))
        {Hotspot->Error=TEXT("Combat preparation failed: ")+Prepared;return false;}
        // Freeze the actual applied combat inputs as well as the initial census.
        // Random request tokens and pre-placement positions are evidence only.
        const auto Inputs=MakeShared<FJsonObject>();
        Inputs->SetNumberField(TEXT("enemy_ordinal"),Hotspot->CombatPreparation->GetNumberField(TEXT("enemy_ordinal")));
        Inputs->SetArrayField(TEXT("enemy_position"),Hotspot->CombatPreparation->GetArrayField(TEXT("enemy_position")));
        TArray<TSharedPtr<FJsonValue>> Members;
        for (const auto& Value:Hotspot->CombatPreparation->GetArrayField(TEXT("positions")))
        {
            const auto& Row=Value->AsObject();auto Member=MakeShared<FJsonObject>();
            Member->SetNumberField(TEXT("ordinal"),Row->GetNumberField(TEXT("ordinal")));
            Member->SetNumberField(TEXT("generation"),Row->GetNumberField(TEXT("generation")));
            Member->SetStringField(TEXT("definition"),Row->GetStringField(TEXT("definition")));
            Member->SetArrayField(TEXT("applied"),Row->GetArrayField(TEXT("applied")));
            Members.Add(MakeShared<FJsonValueObject>(Member));
        }
        Inputs->SetArrayField(TEXT("members"),Members);
        const TSharedPtr<FJsonObject>* Expected=nullptr;
        if (Hotspot->Recipe->TryGetObjectField(TEXT("combat_inputs"),Expected))
        {
            if (!FJsonValue::CompareEqual(FJsonValueObject(Inputs),FJsonValueObject(*Expected)))
            {Hotspot->Error=TEXT("Frozen combat participants, types or applied positions differ");return false;}
        }
        Hotspot->Recipe->SetObjectField(TEXT("combat_inputs"),Inputs);
    }
    if (Hotspot->Interaction)
    {
        if (Hotspot->Hits.Num()!=24) return false;
        for (const auto& Hit:Hotspot->Hits) if (Hit.Replies!=1 || !Hit.Result.WasAccepted()) return false;
        auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
        for (const auto& Key:Hotspot->WoundedKeys)
        {FGuLiHybridObjectState O;if (!H->GetObject(Key,O) || O.LifeState!=1 || O.Health.Health>=O.Health.MaxHealth) return false;}
    }
    Hotspot->Started=FPlatformTime::Seconds();Hotspot->StartQpc=FPlatformTime::Cycles64();Hotspot->Running=true;
    BeginCapture();TRACE_BOOKMARK(TEXT("GuLiHotspotBegin interaction=%d"),Hotspot->Interaction);return true;
#else
    return false;
#endif
}
void UGuLiPerformanceSubsystem::TickHotspot(float)
{
#if !UE_BUILD_SHIPPING
    if (bHotspotPending && Hotspot)
    {
        const auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
        double At=0;Hotspot->Recipe->TryGetNumberField(TEXT("start_logical_tick"),At);
        if (H && H->GetLogicalTick()>=uint64(At))
        {bHotspotPending=false;StartHotspotWindow();}
    }
    if (!Hotspot || !Hotspot->Running) return;
    auto C=Hotspot;C->Elapsed=FPlatformTime::Seconds()-C->Started;
    if (!C->MoveSamples.IsEmpty())
    {
        auto* Army=GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
        // Avoidance can move an idle probe before its command is applied. Only
        // observe the displacement after that command's authoritative write.
        for (auto& Sample:C->MoveSamples) if (Sample.DisplacementMs<0 && Sample.AppliedMs>=0)
        {
            EGuLiTeam Team;uint16 Type=0;FVector Position;
            if (Army&&Army->GetTaskSoldierInfo(Sample.Soldier,Team,Type,Position)&&FVector::Dist2D(Position,Sample.Position)>2.)
                Sample.DisplacementMs=(FPlatformTime::Seconds()-Sample.Submitted)*1000.;
        }
    }
    if (C->Interaction)
    {
        // Small fixed schedule. No JSON, file access, world census or Python during this window.
        const double Times[]={1,5,9,13,17,21,25};
        if (C->Action<UE_ARRAY_COUNT(Times) && C->Elapsed>=Times[C->Action])
        {
            switch (C->Action)
            {
            case 0:Move(C,900001,C->Target);break;
            case 1:if (C->Camera.IsValid()) C->Camera->AddZoomInput(-1);break;
            case 2:Select(C,1);break;
            case 3:Move(C,900002,C->Target+FVector(0,1000,0));break;
            case 4:Select(C,0);if (C->Camera.IsValid()) C->Camera->JumpToWorldLocation(C->WoundedFocus);break;
            case 5:if (C->Camera.IsValid()) C->Camera->AddZoomInput(1);break;
            case 6:Select(C,1);if (C->Camera.IsValid()) C->Camera->JumpToWorldLocation(C->CameraStart);break;
            }
            C->Actions.Add(FString::Printf(TEXT("action_%d"),C->Action++));
        }
        if (C->Elapsed>=2+C->HitWave*4 && C->HitWave<7) {Hits(*GetWorld(),C,false);++C->HitWave;}
        if (C->Camera.IsValid())
        {
            if (C->Elapsed>=3 && C->Elapsed<6) C->Camera->AddPlanarMovement({.3,0});
            if (C->Elapsed>=19 && C->Elapsed<22) C->Camera->AddPlanarMovement({-.3,0});
            C->MinimumHeight=FMath::Min(C->MinimumHeight,C->Camera->GetActualHeightMeters());
            C->MaximumHeight=FMath::Max(C->MaximumHeight,C->Camera->GetActualHeightMeters());
        }
    }
    else if (C->ResponseAction<2 && C->Elapsed>=1+C->ResponseAction*12)
    {
        if (C->ResponseAction) Select(C,1);
        Move(C,900001+C->ResponseAction,C->Target+FVector(0,C->ResponseAction*1000,0));
        ++C->ResponseAction;
    }
    if (C->Elapsed>=30)
    {
        C->EndQpc=FPlatformTime::Cycles64();TRACE_BOOKMARK(TEXT("GuLiHotspotEnd"));EndCapture();C->Running=false;C->Finished=true;
        if (const auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()) C->CensusEnd=Hybrid->GetSimulationCensus();
        auto* Army=GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
        for (int32 I=0;I<C->Red.Num();++I)
        {EGuLiTeam T;uint16 Type=0;FVector P;if (Army && Army->GetTaskSoldierInfo(C->Red[I],T,Type,P) && FVector::Dist2D(P,C->Starts[I])>100) ++C->Moved;}
        if (C->Interaction)
        {
            if (C->Actions.Num()!=7 || C->Acks.Num()!=2 || C->Moved<24 || C->Selections!=4 || C->MaximumHeight-C->MinimumHeight<1)
                C->Error=TEXT("Incomplete interaction sequence, movement, selection or camera readback");
            for (const auto& Hit:C->Hits) if (Hit.Replies!=1 || !Hit.Result.WasAccepted()) C->Error=TEXT("Damage receipt missing, repeated or rejected");
        }
        if (C->MoveSamples.Num()!=C->ResponseGroupSize*2 || C->Acks.Num()!=2 || C->ReceiptMilliseconds.Num()!=2)
            C->Error=TEXT("Response probe commands, displacements or final receipts are incomplete");
        for (const auto& Pair:C->AckCounts) if (Pair.Value!=1) C->Error=TEXT("Movement receipt did not complete exactly once");
        for (const auto& Sample:C->MoveSamples) if (Sample.DisplacementMs<0 || Sample.AppliedMs<0 || Sample.DisplacementMs<Sample.AppliedMs)
            C->Error=TEXT("Command has no ordered authoritative application and observation timestamps");
        ValidateHotspotBehaviorFacts();
    }
#endif
}
void UGuLiPerformanceSubsystem::AppendHotspotCapture(const TSharedRef<FJsonObject>& Root) const
{
    if (!Hotspot) return;const auto& C=*Hotspot;auto J=MakeShared<FJsonObject>();
    J->SetStringField(TEXT("scenario"),C.Combat?TEXT("combat"):C.Interaction?TEXT("interaction"):TEXT("static"));J->SetStringField(TEXT("error"),C.Error);
    J->SetBoolField(TEXT("pending_start"),bHotspotPending);
    J->SetNumberField(TEXT("fact_equivalence_checks"),double(C.FactEquivalenceChecks));
    J->SetNumberField(TEXT("response_probe_count"),C.ResponseGroupSize*2);
    if (C.CombatPreparation) J->SetObjectField(TEXT("combat_preparation"),C.CombatPreparation);
    J->SetBoolField(TEXT("running"),C.Running);J->SetBoolField(TEXT("finished"),C.Finished);J->SetBoolField(TEXT("valid"),C.Finished && C.Error.IsEmpty());
    J->SetNumberField(TEXT("seconds"),C.Elapsed);J->SetNumberField(TEXT("moved_units"),C.Moved);J->SetNumberField(TEXT("selection_actions"),C.Selections);
    J->SetNumberField(TEXT("start_qpc"),double(C.StartQpc));J->SetNumberField(TEXT("end_qpc"),double(C.EndQpc));
    J->SetNumberField(TEXT("height_min"),C.MinimumHeight);J->SetNumberField(TEXT("height_max"),C.MaximumHeight);
    if (C.Recipe) J->SetObjectField(TEXT("frozen_recipe"),C.Recipe.ToSharedRef());
    if (C.CensusStart) J->SetObjectField(TEXT("simulation_census_start"),C.CensusStart.ToSharedRef());
    if (C.CensusEnd) J->SetObjectField(TEXT("simulation_census_end"),C.CensusEnd.ToSharedRef());
    TArray<TSharedPtr<FJsonValue>> Damage;
    for (const auto& H:C.Hits)
    {auto V=MakeShared<FJsonObject>();V->SetStringField(TEXT("event"),H.Event.ToString());V->SetNumberField(TEXT("replies"),H.Replies);V->SetNumberField(TEXT("status"),uint8(H.Result.Status));V->SetNumberField(TEXT("damage"),H.Result.AppliedDamage);Damage.Add(MakeShared<FJsonValueObject>(V));}
    J->SetArrayField(TEXT("damage_receipts"),Damage);auto Acks=MakeShared<FJsonObject>();
    for (const auto& Pair:C.Acks)
    {auto V=MakeShared<FJsonObject>();V->SetNumberField(TEXT("replies"),C.AckCounts.FindRef(Pair.Key));V->SetBoolField(TEXT("accepted"),Pair.Value.IsAccepted());V->SetNumberField(TEXT("result"),uint8(Pair.Value.Result));Acks->SetObjectField(FString::FromInt(Pair.Key),V);}
    J->SetObjectField(TEXT("movement_receipts"),Acks);
    TArray<TSharedPtr<FJsonValue>> Latencies;
    for (const auto& S:C.MoveSamples)
    {auto V=MakeShared<FJsonObject>();V->SetNumberField(TEXT("command"),S.Command);V->SetNumberField(TEXT("soldier"),S.Soldier.Value);
        V->SetNumberField(TEXT("displacement_ms"),S.DisplacementMs);V->SetNumberField(TEXT("applied_ms"),S.AppliedMs);
        V->SetNumberField(TEXT("observation_wait_ms"),S.DisplacementMs-S.AppliedMs);Latencies.Add(MakeShared<FJsonValueObject>(V));}
    J->SetStringField(TEXT("displacement_timing"),TEXT("authoritative_mass_write_and_ordered_next_tick_observation_v3"));
    J->SetArrayField(TEXT("command_displacement_latency"),Latencies);auto Receipts=MakeShared<FJsonObject>();
    for (const auto& [Id,Ms]:C.ReceiptMilliseconds) Receipts->SetNumberField(FString::FromInt(Id),Ms);
    J->SetObjectField(TEXT("command_final_receipt_latency_ms"),Receipts);
    Root->SetObjectField(TEXT("hotspot_workload"),J);
}
