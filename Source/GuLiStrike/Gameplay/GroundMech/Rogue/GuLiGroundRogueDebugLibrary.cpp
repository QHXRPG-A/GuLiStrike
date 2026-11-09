#include "Gameplay/GroundMech/Rogue/GuLiGroundRogueDebugLibrary.h"
#if WITH_EDITOR
#include "Gameplay/Buffs/GuLiBuffPresentation.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Network/GuLiHybridSessionSubsystem.h"
#include "Network/GuLiHybridManifest.h"
#include "Gameplay/CombatEffects/GuLiProjectilePoolSubsystem.h"
#include "Network/Private/GuLiHybridObjectConversions.h"
#include "GuLiNetGroundRogue.h"
#include "Gameplay/GroundMech/Rogue/GuLiGroundRogueRuntime.h"
#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Gameplay/GroundMech/Weapons/GuLiGroundMechWeaponComponent.h"
#include "Gameplay/GroundMech/Animation/GuLiGroundMechAnimInstance.h"
#include "Gameplay/Cards/GuLiRogueCardPresentation.h"
#include "Gameplay/GroundMech/Movement/GuLiGroundMechMovementComponent.h"
#include "Network/GuLiHybridActorComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
FString KeyText(const FGuLiHybridObjectKey& K)
{return FString::Printf(TEXT("%u/%s/%u"),uint8(K.Kind),*K.StableId.ToString(EGuidFormats::Digits),K.LifeGeneration);}
FString CoreKeyText(const GuLiNet::ObjectKey& K){return KeyText(GuLiNetBridge::ToUE(K));}
void Position(const TSharedRef<FJsonObject>& J,FVector P)
{J->SetArrayField(TEXT("center"),{MakeShared<FJsonValueNumber>(P.X),MakeShared<FJsonValueNumber>(P.Y),MakeShared<FJsonValueNumber>(P.Z)});}
TSharedRef<FJsonObject> Receipt(UGuLiHybridWorldSubsystem& H,const FGuid& Event)
{
    auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("event"),Event.ToString(EGuidFormats::Digits));
    FGuLiDamageCommitResult R;J->SetBoolField(TEXT("found"),H.FindDamageResult(Event,R));
    J->SetNumberField(TEXT("status"),uint8(R.Status));J->SetNumberField(TEXT("damage"),R.AppliedDamage);
    J->SetNumberField(TEXT("absorbed"),R.AbsorbedDamage);J->SetNumberField(TEXT("remaining"),R.RemainingHealth);J->SetBoolField(TEXT("killed"),R.bKilled);return J;
}
}
FString UGuLiGroundRogueDebugLibrary::ReadState(UObject* WorldContextObject,FVector Center,float RadiusCentimeters)
{
    auto* World=GEngine?GEngine->GetWorldFromContextObject(WorldContextObject,EGetWorldErrorMode::ReturnNull):nullptr;
    if(!World||Center.ContainsNaN()||!FMath::IsFinite(RadiusCentimeters)||RadiusCentimeters<=0)return TEXT("{}");
    auto* H=World->GetSubsystem<UGuLiHybridWorldSubsystem>();auto* D=World->GetSubsystem<UGuLiDamageLedgerSubsystem>();if(!H||!D)return TEXT("{}");
    auto Root=MakeShared<FJsonObject>();Root->SetNumberField(TEXT("buff_version"),GuLiNet::Buffs::Version);
    Root->SetNumberField(TEXT("rogue_version"),GuLiNet::GroundRogue::Version);
    Root->SetBoolField(TEXT("ready"),H->IsWorldReady());Root->SetNumberField(TEXT("tick"),double(H->GetLocalLogicalTick()));
    Root->SetStringField(TEXT("world"),World->GetPathName());
    Root->SetObjectField(TEXT("transactions"),H->GetTransactionCaptureSnapshot());
    if (const auto* Game=World->GetGameInstance())
        if (const auto* Session=Game->GetSubsystem<UGuLiHybridSessionSubsystem>())
            Root->SetObjectField(TEXT("network"),Session->GetNetworkCaptureSnapshot());
    if(auto* PC=World->GetFirstPlayerController())
    {
        if(const auto* Cards=PC->FindComponentByClass<UGuLiRogueCardPresentation>())
        {
            auto J=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Candidates;
            for(const auto& Id:Cards->GetCandidateIds())Candidates.Add(MakeShared<FJsonValueString>(Id));
            J->SetArrayField(TEXT("candidates"),Candidates);J->SetStringField(TEXT("session"),Cards->GetSessionId().ToString());
            J->SetBoolField(TEXT("open"),Cards->IsOpen());J->SetNumberField(TEXT("phase"),Cards->GetPresentationPhase());
            Root->SetObjectField(TEXT("offer"),J);
        }
        if(const auto* Pawn=Cast<AGuLiGroundMechCharacter>(PC->GetPawn()))
        if(const auto* Weapon=Pawn->FindComponentByClass<UGuLiGroundMechWeaponComponent>())
        {
            auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("fire_serial"),double(Weapon->FireSerial));
            J->SetNumberField(TEXT("next_side"),Weapon->NextFireSide);J->SetNumberField(TEXT("next_shot_time"),Weapon->NextShotTime);
            J->SetNumberField(TEXT("fire_rate"),Weapon->GetFireRate());J->SetNumberField(TEXT("damage"),Weapon->GetShotDamage());
            J->SetNumberField(TEXT("shots_fired"),Weapon->ShotsFired);J->SetBoolField(TEXT("held"),Weapon->bFireHeld);
            J->SetStringField(TEXT("upgrade"),Weapon->CurrentUpgradeId);Position(J,Pawn->GetActorLocation());
            if(const auto* Anim=Cast<UGuLiGroundMechAnimInstance>(Pawn->GetMesh()->GetAnimInstance()))
            {J->SetNumberField(TEXT("phase"),Anim->GetFireLoopPhase());J->SetBoolField(TEXT("playing"),Anim->IsFireLoopPlaying());}
            Root->SetObjectField(TEXT("weapon"),J);
        }
    }
    TArray<FGuLiCombatTargetSnapshot> Targets;D->GetTargetSnapshots(Targets);
    TArray<TSharedPtr<FJsonValue>> TargetRows,BuffRows,ShotRows,BuildRows,AttackRows,BlastRows;TSet<FGuLiHybridObjectKey> Nearby;
    for(const auto& T:Targets)
    {
        if(FVector::DistSquared(Center,T.GetHitCenter())>FMath::Square(double(RadiusCentimeters)))continue;
        FGuLiHybridObjectKey K;FGuLiHybridObjectState O;if(!H->ResolveTarget(T.Handle,K)||!H->GetObjectView(K,O))continue;Nearby.Add(K);
        auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("key"),KeyText(K));J->SetNumberField(TEXT("local_id"),T.Handle.LocalId);
        Position(J,T.GetHitCenter());J->SetNumberField(TEXT("radius"),T.CollisionRadius);J->SetNumberField(TEXT("team"),uint8(O.Affiliation));
        J->SetNumberField(TEXT("health"),O.Health.Health);J->SetNumberField(TEXT("max_health"),O.Health.MaxHealth);
        J->SetNumberField(TEXT("shield"),O.Health.Shield);J->SetBoolField(TEXT("dead"),O.Health.bDead);TargetRows.Add(MakeShared<FJsonValueObject>(J));
    }
    // Explicit editor probe only. The runtime HUD and simulation never call this full-directory copy.
    TArray<FGuLiHybridObjectState> Objects;H->GetObjects(Objects);Objects.Sort([](const auto& A,const auto& B){return A.BornAt>B.BornAt;});
    for(const auto& O:Objects)
    {
        if(const auto* C=O.Capabilities.Find(TEXT("ground_rogue")))
        {
            GuLiNet::GroundRogue::State S;
            if(GuLiNet::GroundRogue::DecodeState({C->Data.GetData(),SIZE_T(C->Data.Num())},S))for(const auto& [Player,B]:S.Players)
            {
                GuLiNet::Effects::Set E;if(!GuLiNet::GroundRogue::Resolve(S.Definitions,B,E))continue;
                auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("player"),UTF8_TO_TCHAR(Player.ToString().c_str()));
                uint32 Progress=0;
                for(const auto& Carrier:Objects)if(Carrier.Key.Kind==EGuLiHybridObjectKind::GroundPlayer)
                    if(const auto* Counts=Carrier.Capabilities.Find(TEXT("ground_shots")))
                    {GuLiNet::GroundRogue::ShotCounters CounterState;if(GuLiNet::GroundRogue::DecodeCounters({Counts->Data.GetData(),SIZE_T(Counts->Data.Num())},CounterState)&&CounterState.Players.contains(Player))Progress=GuLiNet::GroundRogue::ReadProgress(CounterState,Player,E);}
                J->SetNumberField(TEXT("revision"),double(B.RevisionValue));J->SetNumberField(TEXT("shot_progress"),Progress);
                J->SetNumberField(TEXT("fire_rate_multiplier"),GuLiNet::Effects::FireRateMultiplier(E));
                J->SetBoolField(TEXT("enhanced"),GuLiNet::Effects::HasEnhancedBullet(E));J->SetNumberField(TEXT("explosive_interval"),GuLiNet::Effects::GetExplosive(E).Interval());
                BuildRows.Add(MakeShared<FJsonValueObject>(J));
            }
        }
        if(AttackRows.Num()<64)if(const auto* C=O.Capabilities.Find(TEXT("ground_attack")))
        {
            GuLiNet::GroundRogue::Attack A;if(GuLiNet::GroundRogue::DecodeAttack({C->Data.GetData(),SIZE_T(C->Data.Num())},A))
            {
                auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("shot"),O.Key.StableId.ToString(EGuidFormats::Digits));
                J->SetNumberField(TEXT("revision"),double(A.RevisionValue));J->SetNumberField(TEXT("damage"),A.Damage);
                J->SetNumberField(TEXT("fire_rate"),A.FireRate);
                const FGuid Primary=FGuLiHybridManifest::MakeStableKey(TEXT("/Game/GuLiStrike/EffectEvents"),EGuLiHybridObjectKind::Flight,O.Key.StableId.ToString(EGuidFormats::Digits)+TEXT("/damage")).StableId;
                J->SetObjectField(TEXT("primary_receipt"),Receipt(*H,Primary));
                J->SetNumberField(TEXT("bullet_vfx"),A.BulletVfx);J->SetNumberField(TEXT("explosive_damage"),A.ExplosiveDamage);J->SetNumberField(TEXT("explosive_radius"),A.ExplosiveRadius);
                if(A.ExplosiveDamage>0)J->SetStringField(TEXT("companion"),UTF8_TO_TCHAR(GuLiNet::GroundRogue::BlastEvent(GuLiNetBridge::ToCore(O.Key.StableId)).ToString().c_str()));
                AttackRows.Add(MakeShared<FJsonValueObject>(J));
            }
        }
        if(BlastRows.Num()<64)if(const auto* C=O.Capabilities.Find(TEXT("ground_explosion")))
        {
            GuLiNet::GroundRogue::Blast B;if(GuLiNet::GroundRogue::DecodeBlast({C->Data.GetData(),SIZE_T(C->Data.Num())},B))
            {
                auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("shot"),O.Key.StableId.ToString(EGuidFormats::Digits));Position(J,FVector(B.X,B.Y,B.Z));
                TArray<TSharedPtr<FJsonValue>> Hits;for(const auto& K:B.Targets){auto Hit=Receipt(*H,GuLiNetBridge::ToUE(GuLiNet::GroundRogue::BlastHitEvent(B.Shot,K)));Hit->SetStringField(TEXT("target"),CoreKeyText(K));Hits.Add(MakeShared<FJsonValueObject>(Hit));}
                J->SetArrayField(TEXT("hits"),Hits);BlastRows.Add(MakeShared<FJsonValueObject>(J));
            }
        }
        if(const auto* C=O.Capabilities.Find(TEXT("buff_status")))
        {
            GuLiNet::Buffs::State S;if(!GuLiNet::Buffs::Decode({C->Data.GetData(),SIZE_T(C->Data.Num())},S))continue;
            if(!Nearby.Contains(GuLiNetBridge::ToUE(S.Host))&&FVector::DistSquared(Center,FVector(S.BurstX,S.BurstY,S.BurstZ))>FMath::Square(double(RadiusCentimeters)))continue;
            auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("key"),KeyText(O.Key));J->SetStringField(TEXT("host"),CoreKeyText(S.Host));
            J->SetBoolField(TEXT("active"),S.Active);J->SetNumberField(TEXT("started"),double(S.Started));J->SetNumberField(TEXT("expires"),double(S.Expires));
            J->SetStringField(TEXT("last_burst"),GuLiNetBridge::ToUE(S.LastBurst).ToString(EGuidFormats::Digits));J->SetNumberField(TEXT("burst_at"),double(S.BurstAt));
            Position(J,FVector(S.BurstX,S.BurstY,S.BurstZ));J->SetNumberField(TEXT("radius"),S.BurstRadius);J->SetNumberField(TEXT("start_radius"),S.BurstStartRadius);
            J->SetNumberField(TEXT("pulse_damage"),S.Pending.Damage);J->SetNumberField(TEXT("pulse_buff"),S.Pending.Buff);
            J->SetStringField(TEXT("pulse_event"),GuLiNetBridge::ToUE(S.Pending.Event).ToString(EGuidFormats::Digits));
            TArray<TSharedPtr<FJsonValue>> Slots,Hits;
            for(const auto& V:S.Slots){auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("id"),V.Frozen.Id);Row->SetNumberField(TEXT("next"),double(V.Next));Slots.Add(MakeShared<FJsonValueObject>(Row));}
            for(const auto& Hit:S.Pending.Targets){auto Row=Receipt(*H,GuLiNetBridge::ToUE(GuLiNet::Buffs::HitEvent(S.Pending.Event,Hit.Target)));Row->SetStringField(TEXT("target"),CoreKeyText(Hit.Target));Row->SetBoolField(TEXT("settled"),Hit.Settled);Hits.Add(MakeShared<FJsonValueObject>(Row));}
            J->SetArrayField(TEXT("slots"),Slots);J->SetArrayField(TEXT("hits"),Hits);BuffRows.Add(MakeShared<FJsonValueObject>(J));
        }
        if(ShotRows.Num()>=64)continue;
        if(const auto* C=O.Capabilities.Find(TEXT("ground_ricochet")))
        {
            GuLiNet::GroundRogue::Chain S;if(!GuLiNet::GroundRogue::DecodeChain({C->Data.GetData(),SIZE_T(C->Data.Num())},S))continue;
            if(FVector::DistSquared(Center,FVector(S.X,S.Y,S.Z))>FMath::Square(double(RadiusCentimeters)))continue;
            auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("key"),KeyText(O.Key));J->SetNumberField(TEXT("at"),double(S.At));
            J->SetStringField(TEXT("first"),CoreKeyText(S.First));J->SetNumberField(TEXT("base_damage"),S.Frozen.Damage);J->SetNumberField(TEXT("budget"),S.Budget);
            J->SetNumberField(TEXT("effects"),double(S.Frozen.Effects.size()));Position(J,FVector(S.X,S.Y,S.Z));TArray<TSharedPtr<FJsonValue>> Hops;
            for(size_t I=0;I<S.Hops.size();++I){const auto& V=S.Hops[I];auto Row=Receipt(*H,GuLiNetBridge::ToUE(GuLiNet::GroundRogue::HopEvent(S.Shot,uint32(I+1),V.Target)));Row->SetStringField(TEXT("target"),CoreKeyText(V.Target));Position(Row,FVector(V.X,V.Y,V.Z));Hops.Add(MakeShared<FJsonValueObject>(Row));}
            J->SetArrayField(TEXT("hops"),Hops);ShotRows.Add(MakeShared<FJsonValueObject>(J));
        }
    }
    if(const auto* P=World->GetSubsystem<UGuLiBuffPresentation>()){TSharedPtr<FJsonObject> J;auto Reader=TJsonReaderFactory<>::Create(P->GetDiagnostics());if(FJsonSerializer::Deserialize(Reader,J)&&J)Root->SetObjectField(TEXT("presentation"),J);}
    Root->SetArrayField(TEXT("targets"),TargetRows);Root->SetArrayField(TEXT("buffs"),BuffRows);Root->SetArrayField(TEXT("ricochets"),ShotRows);
    Root->SetArrayField(TEXT("builds"),BuildRows);Root->SetArrayField(TEXT("attacks"),AttackRows);Root->SetArrayField(TEXT("explosions"),BlastRows);
    if (const auto* Pool=World->GetSubsystem<UGuLiProjectilePoolSubsystem>())
    {TSharedPtr<FJsonObject> J;auto Reader=TJsonReaderFactory<>::Create(Pool->GroundShotDiagnostics());if(FJsonSerializer::Deserialize(Reader,J)&&J)Root->SetObjectField(TEXT("flight_settlement"),J);}
    FString Json;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Json));return Json;
}
bool UGuLiGroundRogueDebugLibrary::PrepareShooter(APlayerController* PC,FVector Location,FVector Aim)
{
    auto* P=PC?Cast<AGuLiGroundMechCharacter>(PC->GetPawn()):nullptr;
    if(!P||!PC->IsLocalController()||!PC->GetWorld()->IsPlayInEditor()||!PC->GetWorld()->GetMapName().Contains(TEXT("LVL_CommanderMassPrototype"))
        ||!UGuLiHybridActorComponent::HasSimulation(P)||Location.ContainsNaN()||Aim.ContainsNaN())return false;
    TGuardValue<bool> ScriptGuard(GAllowActorScriptExecutionInEditor,false);
    auto* M=CastChecked<UGuLiGroundMechMovementComponent>(P->GetCharacterMovement());M->StopMovementImmediately();
    M->TeleportBlink(Location);P->SetActorRotation((Aim-Location).Rotation());
    PC->SetControlRotation((Aim-Location).Rotation());
    return P->GetActorLocation().Equals(Location,1)&&P->SubmitGameplayInput(FVector2D::ZeroVector,Aim,false,false,false,true,true);
}
FString UGuLiGroundRogueDebugLibrary::CheckRouteBoundaries()
{
    GuLiNet::GroundRogue::Catalog Catalog;FString Error;if(!GuLiGroundRogue::LoadCatalog(Catalog,Error))return TEXT("{}");
    auto Id=[](uint32 N){return GuLiNet::Id{{1,2,3,N}};};
    GuLiNet::GroundRogue::Chain C;C.First={GuLiNet::ObjectKind::Soldier,Id(1),1};C.Shot=Id(2);
    C.Frozen.Source={GuLiNet::ObjectKind::GroundPlayer,Id(3),1};C.Frozen.Root={GuLiNet::ObjectKind::BusinessWork,Id(4),1};
    C.Frozen.Player=Id(5);C.Frozen.RevisionValue=1;C.Frozen.Damage=100;
    GuLiNet::GroundRogue::Build B;B.Acquired["10.01"]=1;B.Acquired["06.01"]=1;
    if(!GuLiNet::GroundRogue::Resolve(Catalog,B,C.Frozen.Effects))return TEXT("{}");
    C.Budget=GuLiNet::GroundRogue::HopBudget(C.Shot,GuLiNet::Effects::GetRicochet(C.Frozen.Effects));
    auto Root=MakeShared<FJsonObject>();Root->SetNumberField(TEXT("version"),GuLiNet::GroundRogue::Version);
    Root->SetNumberField(TEXT("range"),GuLiNet::Effects::GetRicochet(C.Frozen.Effects).Range);
    for(const double Distance:{4990.,5000.,5010.})
    {
        C.Hops={{{GuLiNet::ObjectKind::Soldier,Id(6),1},Distance,0,0}};
        const auto Bytes=GuLiNet::GroundRogue::EncodeChain(C);GuLiNet::GroundRogue::Chain Decoded;
        Root->SetBoolField(FString::Printf(TEXT("%.0f"),Distance),!Bytes.empty()&&GuLiNet::GroundRogue::DecodeChain(Bytes,Decoded));
    }
    C.Hops={{{GuLiNet::ObjectKind::Soldier,Id(6),1},4900,0,0},{{GuLiNet::ObjectKind::Soldier,Id(7),1},5100,0,0}};
    Root->SetBoolField(TEXT("drifting_chain_rejected"),GuLiNet::GroundRogue::EncodeChain(C).empty());
    C.Hops={{{GuLiNet::ObjectKind::Soldier,Id(6),1},0,0,5010}};
    Root->SetBoolField(TEXT("vertical_outside_rejected"),GuLiNet::GroundRogue::EncodeChain(C).empty());
    C.Hops.clear();auto Old=GuLiNet::GroundRogue::EncodeChain(C);if(!Old.empty())Old[0]=2;
    GuLiNet::GroundRogue::Chain Decoded;Root->SetBoolField(TEXT("v2_rejected"),!GuLiNet::GroundRogue::DecodeChain(Old,Decoded));
    FString Json;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Json));return Json;
}
#endif
