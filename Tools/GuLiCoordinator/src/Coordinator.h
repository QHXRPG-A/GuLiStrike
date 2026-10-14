#pragma once
#include "Store.h"
#include "Identity.h"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <memory>

namespace GuLiCoordinator
{
struct Config
{
    std::string Address = "127.0.0.1";
    unsigned short Port = 8787;
    std::string Database = "guli-coordinator.sqlite3";
    std::string Manifest;
    bool AllowDevelopmentIdentity = false;
    std::size_t MaxRooms = 64;
    unsigned ExpectedRed = 3, ExpectedBlue = 3;
    std::string ReviewDirectory, ReviewRun;
};
class Coordinator
{
public:
    Coordinator(boost::asio::io_context& Context, Config Settings);
    ~Coordinator();
    bool Start(std::string& Error);
    static bool ValidateManifestFile(const std::string& Path, std::string& Error);
private:
    struct State;
    std::shared_ptr<State> Impl;
};
}
