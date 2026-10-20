// Copyright Epic Games, Inc. All Rights Reserved.

#include "GuLiStrike.h"
#include "Modules/ModuleManager.h"
#include "Containers/Ticker.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

class FGuLiStrikeModule final : public FDefaultGameModuleImpl
{
public:
    virtual void StartupModule() override
    {
        FDefaultGameModuleImpl::StartupModule();
#if PLATFORM_WINDOWS && !UE_BUILD_SHIPPING
        FString Name;
        if (!FParse::Value(FCommandLine::Get(),TEXT("GuLiLocalStopEvent="),Name)) return;
        // An owned local helper opens its parent's unique event. This does not
        // create the acceptance subsystem or alter any gameplay preparation.
        const FString Prefix=TEXT("Local\\GuLiStrikePIEStop_");FGuid Room;
        if (!Name.StartsWith(Prefix) || Name.Len()!=Prefix.Len()+32
            || !FGuid::ParseExact(Name.Mid(Prefix.Len()),EGuidFormats::Digits,Room) || !Room.IsValid()
            || !(StopEvent=OpenEventW(SYNCHRONIZE,0,*Name)))
        {UE_LOG(LogTemp,Error,TEXT("[GuLiLocalPIE] Invalid owned stop event"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
        StopTick=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float)
        {
            if (WaitForSingleObject(StopEvent,0)!=WAIT_OBJECT_0) return true;
            UE_LOG(LogTemp,Display,TEXT("[GuLiLocalPIE] Owned stop requested; exiting normally"));
            FPlatformMisc::RequestExit(false);return false;
        }),0.25f);
#endif
    }
    virtual void ShutdownModule() override
    {
#if PLATFORM_WINDOWS && !UE_BUILD_SHIPPING
        FTSTicker::GetCoreTicker().RemoveTicker(StopTick);
        if (StopEvent) {CloseHandle(StopEvent);StopEvent=nullptr;}
#endif
        FDefaultGameModuleImpl::ShutdownModule();
    }
private:
#if PLATFORM_WINDOWS && !UE_BUILD_SHIPPING
    HANDLE StopEvent=nullptr;
    FTSTicker::FDelegateHandle StopTick;
#endif
};

IMPLEMENT_PRIMARY_GAME_MODULE( FGuLiStrikeModule, GuLiStrike, "GuLiStrike" );

DEFINE_LOG_CATEGORY(LogGuLiStrike)
DEFINE_LOG_CATEGORY(LogGuLiWingman)
DEFINE_LOG_CATEGORY(LogGuLiWingmanAI)
DEFINE_LOG_CATEGORY(LogGuLiWingmanRelay)
DEFINE_LOG_CATEGORY(LogGuLiBattleCombat)
DEFINE_LOG_CATEGORY(LogGuLiWingmanNet)
DEFINE_LOG_CATEGORY(LogGuLiWingmanQA)
