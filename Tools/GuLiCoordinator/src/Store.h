#pragma once
#include "GuLiNetRoom.h"
#include <string>

struct sqlite3;
namespace GuLiCoordinator
{
struct StoredRoom
{
    GuLiNet::Bytes Metadata;
    std::map<std::pair<GuLiNet::Id,GuLiNet::Id>,GuLiNet::Id> PairKeys;
};
// Only identity, room/seat/ownership metadata and peer connection keys are durable.
class Store
{
public:
    explicit Store(const std::string& Path);
    ~Store();
    Store(const Store&)=delete;
    Store& operator=(const Store&)=delete;
    bool Good() const {return Database && LastError.empty();}
    const std::string& Error() const {return LastError;}
    bool Save(GuLiNet::Id Match,const StoredRoom& Value);
    bool Load(GuLiNet::Id Match,StoredRoom& Value,bool& Found);
private:
    bool Fail();
    sqlite3* Database=nullptr;
    std::string LastError;
};
}
