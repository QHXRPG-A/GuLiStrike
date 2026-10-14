#include "Identity.h"
#include <cstdlib>
#include <chrono>
#include <map>
#include <string>
#include <utility>
#if GULI_COORDINATOR_EOS
#include "eos_sdk.h"
#include "eos_connect.h"
#endif

namespace GuLiCoordinator
{
namespace
{
std::string Environment(const char* Name)
{
#if defined(_WIN32)
    char* Value = nullptr; std::size_t Length = 0;
    if (_dupenv_s(&Value, &Length, Name) != 0 || !Value) return {};
    std::string Result(Value); std::free(Value); return Result;
#else
    const char* Value = std::getenv(Name); return Value ? Value : "";
#endif
}
}
struct IdentityVerifier::State
{
    std::string Product = Environment("GULI_EOS_PRODUCT_ID");
    std::string Sandbox = Environment("GULI_EOS_SANDBOX_ID");
    std::string Deployment = Environment("GULI_EOS_DEPLOYMENT_ID");
    std::string Client = Environment("GULI_EOS_CLIENT_ID");
    std::string Secret = Environment("GULI_EOS_CLIENT_SECRET");
    std::string AllowedClient = Environment("GULI_EOS_ALLOWED_GAME_CLIENT_ID");
#if GULI_COORDINATOR_EOS
    EOS_HPlatform Platform = nullptr;
    bool OwnsSDK = false;
    struct Request
    {
        State* Owner = nullptr;
        std::uint64_t Id = 0;
        Completion Done;
        std::string Token;
        EOS_ProductUserId Player = nullptr;
        std::chrono::steady_clock::time_point Deadline;
    };
    std::uint64_t NextRequest = 0;
    std::map<std::uint64_t, std::unique_ptr<Request>> Pending;
#endif
};
IdentityVerifier::IdentityVerifier() : Impl(std::make_unique<State>())
{
#if GULI_COORDINATOR_EOS
    auto& S = *Impl;
    if (S.Product.empty() || S.Sandbox.empty() || S.Deployment.empty() || S.Client.empty() || S.Secret.empty() || S.AllowedClient.empty()) return;
    EOS_InitializeOptions Init{}; Init.ApiVersion = EOS_INITIALIZE_API_LATEST; Init.ProductName = "GuLiCoordinator"; Init.ProductVersion = "0.1.0";
    const auto Result = EOS_Initialize(&Init); if (Result != EOS_EResult::EOS_Success && Result != EOS_EResult::EOS_AlreadyConfigured) return;
    S.OwnsSDK = Result == EOS_EResult::EOS_Success;
    EOS_Platform_Options Options{}; Options.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
    Options.ProductId = S.Product.c_str(); Options.SandboxId = S.Sandbox.c_str(); Options.DeploymentId = S.Deployment.c_str();
    Options.ClientCredentials.ClientId = S.Client.c_str(); Options.ClientCredentials.ClientSecret = S.Secret.c_str();
    Options.bIsServer = EOS_TRUE; Options.Flags = EOS_PF_DISABLE_OVERLAY;
    S.Platform = EOS_Platform_Create(&Options);
#endif
}
IdentityVerifier::~IdentityVerifier()
{
#if GULI_COORDINATOR_EOS
    if (Impl->Platform) EOS_Platform_Release(Impl->Platform);
    // Platform release cancels SDK callbacks; callback storage stays alive until then.
    Impl->Pending.clear();
    if (Impl->OwnsSDK) EOS_Shutdown();
#endif
}
bool IdentityVerifier::Available() const
{
#if GULI_COORDINATOR_EOS
    return Impl->Platform != nullptr;
#else
    return false;
#endif
}
void IdentityVerifier::Tick()
{
#if GULI_COORDINATOR_EOS
    if (Impl->Platform) EOS_Platform_Tick(Impl->Platform);
    const auto Now = std::chrono::steady_clock::now();
    for (auto& [Id, Request] : Impl->Pending)
    {
        (void)Id;
        if (Request->Done && Now >= Request->Deadline)
        {
            auto Done = std::move(Request->Done);
            Done(false, "EOS identity verification timed out");
        }
    }
#endif
}
void IdentityVerifier::Verify(const GuLiNet::Hello& R, Completion Callback)
{
    if (!Available() || R.Carrier != GuLiNet::Transport::EOS || R.ProductUserId.empty() || R.IdentityToken.empty())
    { Callback(false, "EOS identity verification is unavailable or credentials are missing"); return; }
#if GULI_COORDINATOR_EOS
    if (Impl->Pending.size() >= 256 || Impl->NextRequest == UINT64_MAX)
    { Callback(false, "EOS identity verification capacity exhausted"); return; }
    const auto Player = EOS_ProductUserId_FromString(R.ProductUserId.c_str());
    if (!Player) { Callback(false, "Invalid EOS ProductUserId"); return; }
    const auto RequestId = ++Impl->NextRequest;
    auto Value = std::make_unique<State::Request>();
    Value->Owner = Impl.get(); Value->Id = RequestId; Value->Done = std::move(Callback);
    Value->Token = R.IdentityToken; Value->Player = Player;
    Value->Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    auto* C = Value.get(); Impl->Pending.emplace(RequestId, std::move(Value));
    EOS_Connect_IdToken Token{}; Token.ApiVersion = EOS_CONNECT_IDTOKEN_API_LATEST;
    Token.ProductUserId = C->Player; Token.JsonWebToken = C->Token.c_str();
    EOS_Connect_VerifyIdTokenOptions Options{}; Options.ApiVersion = EOS_CONNECT_VERIFYIDTOKEN_API_LATEST; Options.IdToken = &Token;
    EOS_Connect_VerifyIdToken(EOS_Platform_GetConnectInterface(Impl->Platform), &Options, C,
        [](const EOS_Connect_VerifyIdTokenCallbackInfo* Info)
        {
            if (!EOS_EResult_IsOperationComplete(Info->ResultCode)) return;
            auto* Context = static_cast<State::Request*>(Info->ClientData);
            auto& S = *Context->Owner;
            auto Rq = std::move(S.Pending.at(Context->Id)); S.Pending.erase(Context->Id);
            const bool Valid = Info->ResultCode == EOS_EResult::EOS_Success && Info->ProductUserId == Rq->Player
                && Info->ProductId && S.Product == Info->ProductId && Info->SandboxId && S.Sandbox == Info->SandboxId
                && Info->DeploymentId && S.Deployment == Info->DeploymentId && Info->ClientId && S.AllowedClient == Info->ClientId;
            // Device ID legitimately has bIsAccountInfoPresent == false.
            if (Rq->Done) Rq->Done(Valid, Valid ? "" : "EOS token or product environment was rejected");
        });
#endif
}
}
