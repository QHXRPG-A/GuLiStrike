#pragma once
#include <map>
#include <memory>
#include <utility>

namespace GuLiNet
{
/** Immutable published configuration. Replacing configuration never mutates a trial's version. */
template<class K,class V> class SharedCatalog
{
    using Map=std::map<K,V>;
    std::shared_ptr<const Map> Data=std::make_shared<Map>();
public:
    SharedCatalog()=default;
    SharedCatalog& operator=(Map Value) {Data=std::make_shared<const Map>(std::move(Value));return *this;}
    const Map& Values() const {return *Data;}
    operator const Map&() const {return *Data;}
    auto begin() const {return Data->begin();}
    auto end() const {return Data->end();}
    auto find(const K& Key) const {return Data->find(Key);}
    const V& at(const K& Key) const {return Data->at(Key);}
    bool contains(const K& Key) const {return Data->contains(Key);}
    bool empty() const {return Data->empty();}
    auto size() const {return Data->size();}
    void clear() {Data=std::make_shared<Map>();}
    auto emplace(K Key,V Value)
    {
        if (const auto I=Data->find(Key);I!=Data->end()) return std::pair{I,false};
        auto Updated=std::make_shared<Map>(*Data);Updated->emplace(Key,std::move(Value));Data=std::move(Updated);
        return std::pair{Data->find(Key),true};
    }
};
}
