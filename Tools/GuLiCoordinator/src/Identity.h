#pragma once
#include "GuLiNetProtocol.h"
#include <functional>
#include <memory>

namespace GuLiCoordinator
{
class IdentityVerifier
{
public:
    using Completion = std::function<void(bool, std::string)>;
    IdentityVerifier();
    ~IdentityVerifier();
    void Verify(const GuLiNet::Hello& Request, Completion Callback);
    void Tick();
    bool Available() const;
private:
    struct State;
    std::unique_ptr<State> Impl;
};
}
