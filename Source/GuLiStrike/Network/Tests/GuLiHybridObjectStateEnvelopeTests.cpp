#include "Network/Private/GuLiHybridObjectStateEnvelope.h"
#include "Network/Private/GuLiHybridPeerPayload.h"

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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiHybridPeerPayloadTests,
    "GuLiStrike.Network.PeerPayload",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiHybridPeerPayloadTests::RunTest(const FString&)
{
    GuLiNet::Message Original;Original.Header.Kind=GuLiNet::MessageKind::HandoffState;Original.Body.resize(65536);
    for (size_t I=0;I<Original.Body.size();++I) Original.Body[I]=I%16<4?uint8((I/16)>>(8*(I%4))):uint8(I%7);
    GuLiNet::Bytes Encoded;
    TestTrue(TEXT("Repeated bounded private state uses lossless packing"),GuLiHybridPeerPayload::Pack(Original,Encoded));
    TestTrue(TEXT("Packing reduces actual queued bytes"),Encoded.size()<Original.Body.size());
    auto Decoded=Original;Decoded.Body=Encoded;
    TestTrue(TEXT("Application bytes return exactly before handoff validation"),GuLiHybridPeerPayload::Unpack(Decoded) && Decoded.Body==Original.Body);
    auto Truncated=Original;Truncated.Body=Encoded;if (!Truncated.Body.empty()) Truncated.Body.pop_back();
    TestFalse(TEXT("Truncated encoded data is rejected before applying state"),GuLiHybridPeerPayload::Unpack(Truncated));
    GuLiNet::Writer Invalid;Invalid.U32(GuLiHybridPeerPayload::Tag);Invalid.U32(1);Invalid.U8(1);Invalid.U32(uint32(GuLiNet::MaxMessageBytes+1));Invalid.Blob({});
    auto Oversized=Original;Oversized.Body=Invalid.Take();
    TestFalse(TEXT("Expanded size cannot exceed the existing message capacity"),GuLiHybridPeerPayload::Unpack(Oversized));
    GuLiNet::Writer Collision;Collision.U32(GuLiHybridPeerPayload::Tag);Collision.U8(17);
    auto Opaque=Original;Opaque.Header.Kind=GuLiNet::MessageKind::ObjectEvent;Opaque.Body=Collision.Take();
    TestTrue(TEXT("An opaque family payload matching the transport tag is escaped"),GuLiHybridPeerPayload::Pack(Opaque,Encoded));
    Decoded=Opaque;Decoded.Body=Encoded;
    TestTrue(TEXT("Escape unwraps once and preserves opaque family bytes"),GuLiHybridPeerPayload::Unpack(Decoded) && Decoded.Body==Opaque.Body);
    auto Small=Original;Small.Body={2,0,0,0};
    TestFalse(TEXT("Small ordinary state needs no packing"),GuLiHybridPeerPayload::Pack(Small,Encoded));
    TestTrue(TEXT("Raw state remains supported"),GuLiHybridPeerPayload::Unpack(Small) && Small.Body==GuLiNet::Bytes({2,0,0,0}));
    return true;
}
#endif
