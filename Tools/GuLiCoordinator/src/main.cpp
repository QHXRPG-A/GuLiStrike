#include "Coordinator.h"
#include <charconv>
#include <csignal>
#include <chrono>
#include <iostream>
#include <functional>
#if defined(_WIN32)
#include <windows.h>
#endif

int main(int Count, char** Values)
{
    GuLiCoordinator::Config Config;
    bool ValidateOnly=false;
    std::string LocalStopEvent;
    for (int I = 1; I < Count; ++I)
    {
        const std::string Arg = Values[I];
        if (Arg == "--dev") Config.AllowDevelopmentIdentity = true;
        else if (Arg == "--validate-manifest") ValidateOnly=true;
        else if ((Arg == "--manifest" || Arg == "--database" || Arg == "--bind" || Arg == "--port"
            || Arg=="--expected-red" || Arg=="--expected-blue" || Arg=="--review-directory" || Arg=="--review-run"
            || Arg=="--local-stop-event") && I + 1 < Count)
        {
            const std::string Value = Values[++I];
            if (Arg == "--manifest") Config.Manifest = Value;
            if (Arg == "--database") Config.Database = Value;
            if (Arg == "--bind") Config.Address = Value;
            if (Arg=="--review-directory") Config.ReviewDirectory=Value;
            if (Arg=="--review-run") Config.ReviewRun=Value;
            if (Arg=="--local-stop-event") LocalStopEvent=Value;
            if (Arg=="--expected-red" || Arg=="--expected-blue")
            {
                unsigned Members=0; const auto Parsed=std::from_chars(Value.data(),Value.data()+Value.size(),Members);
                if (Parsed.ec!=std::errc{} || Parsed.ptr!=Value.data()+Value.size() || Members<1 || Members>3)
                { std::cerr<<"Expected team size must be 1..3\n"; return 2; }
                if (Arg=="--expected-red") Config.ExpectedRed=Members; else Config.ExpectedBlue=Members;
            }
            if (Arg == "--port")
            {
                unsigned Port = 0; const auto Result = std::from_chars(Value.data(), Value.data() + Value.size(), Port);
                if (Result.ec != std::errc{} || Result.ptr != Value.data() + Value.size() || Port == 0 || Port > 65535)
                { std::cerr << "Invalid port\n"; return 2; } Config.Port = static_cast<unsigned short>(Port);
            }
        }
        else { std::cerr << "Usage: GuLiCoordinator --manifest <export.json> [--dev] [--database <path>] [--port 8787]\n"; return 2; }
    }
    if (Config.Manifest.empty()) { std::cerr << "An exported gameplay manifest is required\n"; return 2; }
    if (ValidateOnly)
    {
        std::string Error;
        if (!GuLiCoordinator::Coordinator::ValidateManifestFile(Config.Manifest,Error))
        { std::cerr << "Manifest: " << Error << '\n'; return 1; }
        std::cout << "Manifest validated\n"; return 0;
    }
    if (Config.ReviewDirectory.empty()!=Config.ReviewRun.empty() || (!Config.ReviewRun.empty() && !Config.AllowDevelopmentIdentity))
    { std::cerr<<"Review requires --dev and both --review-directory/--review-run\n"; return 2; }
    if (!Config.ReviewRun.empty()) Config.MaxRooms=1;
    boost::asio::io_context Context(1); // All room transitions and SQLite commits are serialized.
#if defined(_WIN32)
    struct StopHandle {HANDLE Value=nullptr;~StopHandle(){if(Value) CloseHandle(Value);}} Stop;
    boost::asio::steady_timer StopTimer(Context);
    std::function<void()> CheckStop;
    if (!LocalStopEvent.empty())
    {
        const std::string Prefix="Local\\GuLiStrikePIEStop_";
        if (!Config.AllowDevelopmentIdentity || !LocalStopEvent.starts_with(Prefix)
            || LocalStopEvent.size()!=Prefix.size()+32
            || LocalStopEvent.substr(Prefix.size()).find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos
            || !(Stop.Value=OpenEventA(SYNCHRONIZE,FALSE,LocalStopEvent.c_str())))
        {std::cerr<<"Invalid owned local stop event\n";return 2;}
        CheckStop=[&]()
        {
            StopTimer.expires_after(std::chrono::milliseconds(250));
            StopTimer.async_wait([&](boost::system::error_code Error)
            {
                if (Error) return;
                if (WaitForSingleObject(Stop.Value,0)==WAIT_OBJECT_0)
                {std::cout<<"Owned local stop requested\n";Context.stop();}
                else CheckStop();
            });
        };
        CheckStop();
    }
#else
    if (!LocalStopEvent.empty()) {std::cerr<<"Local stop events require Windows\n";return 2;}
#endif
    boost::asio::signal_set Signals(Context, SIGINT, SIGTERM);
    Signals.async_wait([&](boost::system::error_code, int) { Context.stop(); });
    GuLiCoordinator::Coordinator Server(Context, Config); std::string Error;
    if (!Server.Start(Error)) { std::cerr << "Coordinator: " << Error << '\n'; return 1; }
    std::cout << "Coordinator ws://" << Config.Address << ':' << Config.Port << "/v1/match\n";
    Context.run(); return 0;
}
