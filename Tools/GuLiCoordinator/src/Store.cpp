#include "Store.h"
#include <sqlite3.h>

namespace GuLiCoordinator
{
namespace
{
struct Statement
{
    sqlite3_stmt* Value=nullptr;
    Statement(sqlite3* Db,const char* Sql) {sqlite3_prepare_v2(Db,Sql,-1,&Value,nullptr);}
    ~Statement() {if (Value) sqlite3_finalize(Value);}
};
}
Store::Store(const std::string& Path)
{
    if (sqlite3_open_v2(Path.c_str(),&Database,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK)
    {Fail();return;}
    sqlite3_busy_timeout(Database,2000);
    // Versioned metadata is deliberately separate from historical battle data.
    if (sqlite3_exec(Database,"PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL;"
        "CREATE TABLE IF NOT EXISTS room_metadata_v28(match TEXT PRIMARY KEY NOT NULL,metadata BLOB NOT NULL);",
        nullptr,nullptr,nullptr)!=SQLITE_OK) Fail();
}
Store::~Store() {if (Database) sqlite3_close(Database);}
bool Store::Fail() {LastError=Database?sqlite3_errmsg(Database):"Database unavailable";return false;}
bool Store::Save(GuLiNet::Id Match,const StoredRoom& V)
{
    if (!Good() || !Match.Valid() || V.Metadata.empty() || V.PairKeys.size()>2016) return false;
    GuLiNet::Writer W(40*1024*1024);W.U32(GuLiNet::ProtocolVersion);W.Blob(V.Metadata);W.U32(static_cast<std::uint32_t>(V.PairKeys.size()));
    for (const auto& [Pair,Key]:V.PairKeys) {W.Identity(Pair.first);W.Identity(Pair.second);W.Identity(Key);}
    const auto Bytes=W.Take();if (Bytes.empty()) return false;
    Statement S(Database,"INSERT INTO room_metadata_v28(match,metadata) VALUES(?,?) ON CONFLICT(match) DO UPDATE SET metadata=excluded.metadata");
    if (!S.Value) return Fail();const auto Id=Match.ToString();
    sqlite3_bind_text(S.Value,1,Id.c_str(),static_cast<int>(Id.size()),SQLITE_TRANSIENT);
    sqlite3_bind_blob64(S.Value,2,Bytes.data(),Bytes.size(),SQLITE_TRANSIENT);
    return sqlite3_step(S.Value)==SQLITE_DONE || Fail();
}
bool Store::Load(GuLiNet::Id Match,StoredRoom& Out,bool& Found)
{
    Found=false;if (!Good() || !Match.Valid()) return false;
    Statement S(Database,"SELECT metadata FROM room_metadata_v28 WHERE match=?");if (!S.Value) return Fail();
    const auto Id=Match.ToString();sqlite3_bind_text(S.Value,1,Id.c_str(),static_cast<int>(Id.size()),SQLITE_TRANSIENT);
    const auto Result=sqlite3_step(S.Value);if (Result==SQLITE_DONE) return true;if (Result!=SQLITE_ROW) return Fail();
    const auto* Data=static_cast<const std::uint8_t*>(sqlite3_column_blob(S.Value,0));const int Size=sqlite3_column_bytes(S.Value,0);
    if (!Data || Size<=0 || Size>40*1024*1024) {LastError="Invalid room metadata";return false;}
    GuLiNet::Reader R({Data,static_cast<std::size_t>(Size)});StoredRoom V;std::uint32_t Version,Count;
    if (!R.U32(Version) || Version!=GuLiNet::ProtocolVersion || !R.Blob(V.Metadata,32*1024*1024) || !R.U32(Count) || Count>2016)
    {LastError="Incompatible room metadata";return false;}
    for (std::uint32_t I=0;I<Count;++I)
    {
        GuLiNet::Id A,B,Key;
        if (!R.Identity(A) || !R.Identity(B) || !R.Identity(Key) || !A.Valid() || !B.Valid() || !(A<B) || !Key.Valid()
            || !V.PairKeys.emplace(std::make_pair(A,B),Key).second) {LastError="Invalid peer key metadata";return false;}
    }
    if (!R.Done()) {LastError="Trailing room metadata";return false;}
    Found=true;Out=std::move(V);return true;
}
}
