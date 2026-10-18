#include "Gameplay/Performance/GuLiPerformanceSubsystem.h"
#include "Gameplay/Performance/GuLiHotspotOptions.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Commander/Presentation/GuLiCommanderCameraPawn.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "ProfilingDebugging/MiscTrace.h"

struct FGuLiHotspotCapture
{
    bool Interaction=false,Running=false,Finished=false;
    FString Error;
    TSharedPtr<FJsonObject> Recipe;
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
    if (Count!=24) C->Error=TEXT("A group recall did not select exactly 24 units");else ++C->Selections;
#endif
}
void Move(const TSharedPtr<FGuLiHotspotCapture>& C,uint32 Id,const FVector& P)
{
    if (!C->Sync.IsValid()) {C->Error=TEXT("Commander input is unavailable");return;}
    const double Now=FPlatformTime::Seconds();C->SubmittedAt.Add(Id,Now);
    auto* Army=C->Sync->GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
    for (int32 I=Id==900001?0:24,End=I+24;I<End;++I)
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

FString UGuLiPerformanceSubsystem::PrepareHotspotCombat(int32 LocalUnits)
{
#if !UE_BUILD_SHIPPING
    if (bCapturing || LocalUnits<1 || LocalUnits>150) return TEXT("{\"error\":\"Prepare 1..150 units outside capture\"}");
    auto* W=GetWorld();auto* H=W?W->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;
    auto* Army=W?W->GetSubsystem<UGuLiBattleAuthoritySubsystem>():nullptr;
    if (!H || !Army || !H->CanPublishFacts()) return TEXT("{\"error\":\"Combat preparation requires a ready world\"}");
    TArray<FGuLiHybridObjectState> Objects;H->GetObjects(Objects);
    Objects.Sort([](const auto& A,const auto& B){return A.Ordinal<B.Ordinal;});
    TArray<FGuLiHybridObjectState> Sources;
    for (const auto& O:Objects) if (O.Key.Kind==EGuLiHybridObjectKind::Soldier && O.LifeState==1 && H->HasSimulation(O.Key)
        && (O.Definition==TEXT("unit/1")||O.Definition==TEXT("unit/2"))) {Sources.Add(O);if (Sources.Num()==LocalUnits) break;}
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
FString UGuLiPerformanceSubsystem::PrepareHotspotWorkload(const FString& FrozenJson,bool Interaction)
{
#if !UE_BUILD_SHIPPING
    if (bCapturing) return TEXT("{\"error\":\"capture is active\"}");
    Hotspot=MakeShared<FGuLiHotspotCapture>();auto& C=*Hotspot;C.Interaction=Interaction;
    auto* W=GetWorld();auto* PC=W->GetFirstPlayerController();auto* H=W->GetSubsystem<UGuLiHybridWorldSubsystem>();
    auto* Army=W->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
    C.Sync=PC?PC->FindComponentByClass<UGuLiCommanderNetSyncComponent>():nullptr;
    C.Camera=PC?Cast<AGuLiCommanderCameraPawn>(PC->GetPawn()):nullptr;
    if (!H || !H->CanPublishFacts() || !Army || !C.Sync.IsValid() || !C.Sync->IsSoldierStreamReady() || !C.Camera.IsValid())
    {C.Error=TEXT("The Commander session is not ready");return GetCaptureJson();}
    TArray<FGuLiHybridObjectState> Objects;H->GetObjects(Objects); // Preparation only.
    Objects.Sort([](const auto& A,const auto& B){return A.Ordinal<B.Ordinal;});
    int32 Soldiers=0;for (const auto& O:Objects) if (O.Key.Kind==EGuLiHybridObjectKind::Soldier && O.LifeState==1) ++Soldiers;
    if (Soldiers!=600) {C.Error=TEXT("The preparation census is not 600 live units");return GetCaptureJson();}
    C.Recipe=MakeShared<FJsonObject>();
    if (!FrozenJson.IsEmpty() && !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FrozenJson),C.Recipe))
    {C.Error=TEXT("Invalid frozen recipe");return GetCaptureJson();}
    auto Pick=[&](const TCHAR* Field,EGuLiTeam Team,int32 Count,TArray<FGuLiSoldierId>& Ids,TArray<FGuLiHybridObjectKey>& Keys)
    {
        TArray<TSharedPtr<FJsonValue>> Values;
        if (FrozenJson.IsEmpty())
        {
            for (const auto& O:Objects) if (O.Key.Kind==EGuLiHybridObjectKind::Soldier && O.LifeState==1 && O.Team==Team && O.Ordinal)
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
            if (!O) return false;Ids.Add(FGuLiSoldierId(O->Ordinal));Keys.Add(O->Key);
        }
        return true;
    };
    if (!Pick(TEXT("red_ids"),EGuLiTeam::Red,48,C.Red,C.RedKeys) || !Pick(TEXT("wounded_ids"),EGuLiTeam::Blue,24,C.Wounded,C.WoundedKeys))
    {C.Error=TEXT("Frozen unit identities do not match this session");return GetCaptureJson();}
    TArray<TSharedPtr<FJsonValue>> Positions;
    for (auto Id:C.Red)
    {
        EGuLiTeam Team;uint16 Type=0;FVector Location;
        if (!Army->GetTaskSoldierInfo(Id,Team,Type,Location)) {C.Error=TEXT("A frozen unit position is unavailable");return GetCaptureJson();}
        C.Starts.Add(Location);auto J=MakeShared<FJsonObject>();J->SetArrayField(TEXT("position"),Point(Location));Positions.Add(MakeShared<FJsonValueObject>(J));
    }
    if (FrozenJson.IsEmpty())
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
    if (FrozenJson.IsEmpty())
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
    if (Interaction)
    {
        for (int32 Group=0;Group<2;++Group)
        {
            if (!C.Sync->PrepareReviewSoldierSelection(MakeArrayView(C.Red.GetData()+Group*24,24),800001+Group))
            {C.Error=TEXT("Cannot prepare the fixed 24-unit selection");return GetCaptureJson();}
            C.Sync->SubmitControlGroup(uint8(Group),true,false,false,false);
        }
        Select(Hotspot,0);Hits(*W,Hotspot,true);
    }
    return GetCaptureJson();
#else
    return TEXT("{\"error\":\"development only\"}");
#endif
}
bool UGuLiPerformanceSubsystem::StartHotspotWindow()
{
#if !UE_BUILD_SHIPPING
    if (!Hotspot || bCapturing || !Hotspot->Error.IsEmpty() || Hotspot->Running || Hotspot->Finished) return false;
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
    if (!Hotspot || !Hotspot->Running) return;
    auto C=Hotspot;C->Elapsed=FPlatformTime::Seconds()-C->Started;
    if (!C->MoveSamples.IsEmpty())
    {
        auto* Army=GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
        for (auto& Sample:C->MoveSamples) if (Sample.DisplacementMs<0)
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
    if (C->Elapsed>=30)
    {
        C->EndQpc=FPlatformTime::Cycles64();TRACE_BOOKMARK(TEXT("GuLiHotspotEnd"));EndCapture();C->Running=false;C->Finished=true;
        auto* Army=GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
        for (int32 I=0;I<C->Red.Num();++I)
        {EGuLiTeam T;uint16 Type=0;FVector P;if (Army && Army->GetTaskSoldierInfo(C->Red[I],T,Type,P) && FVector::Dist2D(P,C->Starts[I])>100) ++C->Moved;}
        if (C->Interaction)
        {
            if (C->Actions.Num()!=7 || C->Acks.Num()!=2 || C->Moved<24 || C->Selections!=4 || C->MaximumHeight-C->MinimumHeight<1)
                C->Error=TEXT("Incomplete interaction sequence, movement, selection or camera readback");
            for (const auto& Pair:C->AckCounts) if (Pair.Value!=1) C->Error=TEXT("Movement receipt did not complete exactly once");
            for (const auto& Hit:C->Hits) if (Hit.Replies!=1 || !Hit.Result.WasAccepted()) C->Error=TEXT("Damage receipt missing, repeated or rejected");
            for (const auto& Sample:C->MoveSamples) if (Sample.DisplacementMs<0 || Sample.AppliedMs<0 || Sample.DisplacementMs<Sample.AppliedMs)
                C->Error=TEXT("Command has no ordered authoritative application and observation timestamps");
        }
    }
#endif
}
void UGuLiPerformanceSubsystem::AppendHotspotCapture(const TSharedRef<FJsonObject>& Root) const
{
    if (!Hotspot) return;const auto& C=*Hotspot;auto J=MakeShared<FJsonObject>();
    J->SetStringField(TEXT("scenario"),C.Interaction?TEXT("interaction"):TEXT("static"));J->SetStringField(TEXT("error"),C.Error);
    J->SetBoolField(TEXT("running"),C.Running);J->SetBoolField(TEXT("finished"),C.Finished);J->SetBoolField(TEXT("valid"),C.Finished && C.Error.IsEmpty());
    J->SetNumberField(TEXT("seconds"),C.Elapsed);J->SetNumberField(TEXT("moved_units"),C.Moved);J->SetNumberField(TEXT("selection_actions"),C.Selections);
    J->SetNumberField(TEXT("start_qpc"),double(C.StartQpc));J->SetNumberField(TEXT("end_qpc"),double(C.EndQpc));
    J->SetNumberField(TEXT("height_min"),C.MinimumHeight);J->SetNumberField(TEXT("height_max"),C.MaximumHeight);
    if (C.Recipe) J->SetObjectField(TEXT("frozen_recipe"),C.Recipe.ToSharedRef());
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
    J->SetStringField(TEXT("displacement_timing"),TEXT("authoritative_mass_write_and_next_tick_observation_v2"));
    J->SetArrayField(TEXT("command_displacement_latency"),Latencies);auto Receipts=MakeShared<FJsonObject>();
    for (const auto& [Id,Ms]:C.ReceiptMilliseconds) Receipts->SetNumberField(FString::FromInt(Id),Ms);
    J->SetObjectField(TEXT("command_final_receipt_latency_ms"),Receipts);
    Root->SetObjectField(TEXT("hotspot_workload"),J);
}
