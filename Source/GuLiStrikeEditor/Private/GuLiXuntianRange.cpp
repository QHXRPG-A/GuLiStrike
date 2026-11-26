#include "GuLiXuntianRange.h"
#include "GuLiHybridAuthoringLibrary.h"
#include "Editor.h"
#include "Misc/Paths.h"
#include "Network/GuLiHybridAdapterRegistry.h"
#include "Network/GuLiHybridManifest.h"
#include "Gameplay/Ship/Aiming/GuLiXuntianGunnery.h"
#include "Engine/TargetPoint.h"
#include "EngineUtils.h"

FString UGuLiHybridAuthoringLibrary::ExportXuntianHUDRange(const FString& File)
{
    auto* World=GEditor && !GEditor->PlayWorld ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World || File.IsEmpty()) return TEXT("No idle editor world or output file.");
    FGuLiHybridManifestData Data; FString Error;
    if (!FGuLiHybridManifest::Read(FPaths::ProjectConfigDir()/TEXT("Hybrid/RuntimeManifest-600.json"),Data,Error)
        || !GuLiXuntianRange::Prepare(*World,Data,File,Error)) return Error;
    return {};
}

bool GuLiXuntianRange::Prepare(UWorld& World,FGuLiHybridManifestData& Manifest,const FString& File,FString& Error)
{
    const FString TargetDef=TEXT("range/xuntian_target");
    if (Manifest.MapPackage!=World.GetOutermost()->GetName())
    { Error=TEXT("巡天试飞需要本地图的 Air 席位和运行清单。"); return false; }
    if (!UGuLiXuntianGunnery::BindRangeHull(Manifest,Error)) return false;
    const TCHAR* Names[]={TEXT("Start"),TEXT("High"),TEXT("Level"),TEXT("Low"),TEXT("Inside"),TEXT("Outside")};
    TArray<FTransform> Stations;
    for (const TCHAR* Name:Names)
    {
        const FName Tag(*(FString(TEXT("GuLi.XuntianHUD."))+Name));
        ATargetPoint* Found=nullptr;
        for (TActorIterator<ATargetPoint> It(&World);It;++It)
        {
            if (!It->ActorHasTag(Tag)) continue;
            if (Found) { Error=TEXT("重复的巡天试飞标记 ")+Tag.ToString(); return false; }
            Found=*It;
        }
        if (!Found) { Error=TEXT("缺少巡天试飞标记 ")+Tag.ToString(); return false; }
        Stations.Add(Found->GetActorTransform());
    }
    auto* Carrier=Manifest.Objects.FindByPredicate([](const auto& O){return O.Definition==TEXT("player/ship") && O.Team==EGuLiTeam::Red;});
    if (!Carrier) {Error=TEXT("巡天红方席位缺失。");return false;}
    Carrier->Location=Stations[0].GetLocation(); Carrier->FacingYawDegrees=Stations[0].Rotator().Yaw;
    const float Health=Carrier->Health;
    for (int32 I=1;I<=3;++I)
    {
        auto& O=Manifest.Objects.AddDefaulted_GetRef();
        O.Key=FGuLiHybridManifest::MakeStableKey(Manifest.MapPackage,EGuLiHybridObjectKind::Ship,FString(TEXT("xuntian/range/"))+Names[I]);
        O.Definition=TargetDef; O.Health=Health; O.Ordinal=10+I; O.Team=EGuLiTeam::Blue;
        O.Location=Stations[I].GetLocation(); O.FacingYawDegrees=Stations[I].Rotator().Yaw;
    }
    int32 I=4;
    for (auto& O:Manifest.Objects)
    {
        if (O.Definition!=TEXT("unit/1") || O.Team!=EGuLiTeam::Blue) continue;
        O.Location=Stations[I].GetLocation();
        const FString Prefix=TEXT("deployment/")+O.Key.StableId.ToString(EGuidFormats::Digits);
        Manifest.Numbers.Add(Prefix+TEXT("/start_idle"),1); Manifest.Numbers.Add(Prefix+TEXT("/automatic_fire"),0);
        if (++I==6) break;
    }
    if (I!=6) {Error=TEXT("巡天试飞需要两个蓝方地面目标。");return false;}
    return FGuLiHybridManifest::Write(Manifest,File,Error);
}
