#include "Network/Private/GuLiHybridObjectStateEnvelope.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiHybridObjectStateEnvelopeTests,
    "GuLiStrike.Network.ObjectStateEnvelope",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiHybridObjectStateEnvelopeTests::RunTest(const FString&)
{
    using namespace GuLiHybridObjectState;
    const GuLiNet::Bytes Family={1,2,3},Components={4,5,6},Applications={7},Entities={8};FParts Parts;
    const auto Actor=Encode(4,Family,true,Components,Applications,Entities);
    TestTrue(TEXT("Actor snapshot preserves family and shared component pieces"),
        Decode(Actor,4,Parts) && Parts.HasActor && Parts.Family==Family && Parts.Components==Components);
    TestFalse(TEXT("Old family version cannot be interpreted by the new adapter"),Decode(Actor,3,Parts));
    const auto Work=Encode(1,Family,false,{},Applications,Entities);
    TestTrue(TEXT("Actorless admitted work uses the same envelope without components"),
        Decode(Work,1,Parts) && !Parts.HasActor && Parts.Components.empty());
    TestTrue(TEXT("An actor cannot omit its required component envelope"),Encode(4,Family,true,{},Applications,Entities).empty());
    TestTrue(TEXT("Actorless work cannot hide component state"),Encode(1,Family,false,Components,Applications,Entities).empty());
    auto Corrupt=Actor;Corrupt[4]=99;
    TestFalse(TEXT("Unknown universal schema is rejected"),Decode(Corrupt,4,Parts));
    Corrupt=Actor;Corrupt[12]=2;
    TestFalse(TEXT("Invalid actor presence cannot bypass component validation"),Decode(Corrupt,4,Parts));
    Corrupt=Actor;Corrupt.push_back(0);
    TestFalse(TEXT("Trailing state cannot conceal an unregistered recovery fragment"),Decode(Corrupt,4,Parts));
    for (std::size_t End=0;End<Actor.size();++End)
        if (Decode(std::span<const uint8>(Actor.data(),End),4,Parts))
        {AddError(TEXT("Truncated object checkpoint was accepted"));break;}
    const GuLiNet::Bytes Oversized(MaximumPiece+1,0);
    TestTrue(TEXT("Oversized family state cannot allocate an unbounded checkpoint"),Encode(4,Oversized,true,Components,Applications,Entities).empty());
    TestTrue(TEXT("Oversized shared capabilities are rejected"),Encode(4,Family,true,Oversized,Applications,Entities).empty());
    const GuLiNet::Bytes Boundary(MaximumPiece,7);
    TestTrue(TEXT("Both independently bounded pieces remain decodable at capacity"),Decode(Encode(4,Boundary,true,Boundary,Applications,Entities),4,Parts));
    const auto Saved=Parts;
    TestFalse(TEXT("Corrupt input does not overwrite the caller's last validated state"),Decode(Corrupt,4,Parts));
    TestTrue(TEXT("Validated output remains intact after rejection"),Parts.Family==Saved.Family && Parts.Components==Saved.Components && Parts.HasActor==Saved.HasActor);
    return true;
}
#endif
