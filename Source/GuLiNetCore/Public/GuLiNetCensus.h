#pragma once

#include "GuLiNetGameplayTypes.h"

namespace GuLiNet
{
// The applied directory retains individual lives. Only live entries need a
// fresh simulation-authority check when a performance sample is taken.
struct ClientCensusIndex
{
    struct InactiveCounts
    {
        std::int64_t Total=0, Dormant=0, Dead=0, Destroyed=0;
        bool operator==(const InactiveCounts&) const=default;
    };
    std::set<ObjectKey> Alive;
    std::map<std::string,InactiveCounts> InactiveDefinitions;
    std::map<Team,InactiveCounts> InactiveTeams;

    void Update(const ObjectRecord* Previous,const ObjectRecord& Next)
    {
        const auto& A=Next.Grant;
        if (Previous && Previous->Grant.State==A.State &&
            (A.State==Life::Alive || (Previous->Grant.Dormant==A.Dormant &&
             Previous->Grant.Faction==A.Faction && Previous->Definition==Next.Definition))) return;

        auto Adjust=[](auto& Groups,const auto& Group,const Authority& Grant,int Delta)
        {
            auto& Counts=Groups[Group];
            Counts.Total+=Delta;
            if (Grant.Dormant) Counts.Dormant+=Delta;
            if (Grant.State==Life::Dead) Counts.Dead+=Delta;
            if (Grant.State==Life::Destroyed) Counts.Destroyed+=Delta;
            if (!Counts.Total) Groups.erase(Group);
        };
        if (Previous)
        {
            const auto& Old=Previous->Grant;
            if (Old.State==Life::Alive) Alive.erase(Old.Object);
            else
            {
                Adjust(InactiveDefinitions,Previous->Definition,Old,-1);
                Adjust(InactiveTeams,Old.Faction,Old,-1);
            }
        }
        if (A.State==Life::Alive) Alive.insert(A.Object);
        else
        {
            Adjust(InactiveDefinitions,Next.Definition,A,1);
            Adjust(InactiveTeams,A.Faction,A,1);
        }
    }
};
}
