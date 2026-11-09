#include "Network/GuLiHybridAdapterRegistry.h"
#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "Network/Private/GuLiHybridComponentState.h"
#include "Network/Private/GuLiHybridReliablePayload.h"
#include "GuLiNetTransport.h"
#include "Gameplay/Building/GuLiHybridBuildingAdapter.h"
#include "Gameplay/Building/GuLiBuildingTypes.h"
#include "Engine/StaticMesh.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiHybridAdapterRegistryTests,
    "GuLiStrike.Network.AdapterRegistry",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiHybridAdapterRegistryTests::RunTest(const FString&)
{
    FGuLiHybridManifestData Data;FString Error;
    const FString Definition=TEXT("building/106");
    TestTrue(TEXT("A new definition reuses the existing factory capability"),
        FGuLiHybridAdapterRegistry::BindDefinition(Data,Definition,TEXT("ResourceFactory"),Error));
    FGuLiHybridStartupEntry Object;Object.Key.Kind=EGuLiHybridObjectKind::Building;Object.Definition=Definition;
    Data.Objects.Add(Object);
    auto& Spawn=Data.Rules.AddDefaulted_GetRef();Spawn.Kind=EGuLiHybridRuleKind::Spawn;
    Spawn.SpawnKind=EGuLiHybridObjectKind::Building;Spawn.TargetDefinition=Definition;
    TestTrue(TEXT("Both startup and dynamic spawn use the same declared capability"),FGuLiHybridAdapterRegistry::Validate(Data,Error));
    FGuLiHybridObjectState State;State.Key.Kind=EGuLiHybridObjectKind::Building;State.Definition=FName(*Definition);
    FGuLiHybridAdapterFamily Family;
    TestTrue(TEXT("A definition outside the old 1-6 range resolves"),FGuLiHybridAdapterRegistry::Resolve(Data,State,Family,Error));
    TestEqual(TEXT("Definition selects the declared family"),Family.Name,FName(TEXT("ResourceFactory")));
    TestFalse(TEXT("Conflicting family assignment is rejected"),
        FGuLiHybridAdapterRegistry::BindDefinition(Data,Definition,TEXT("PlacedBuilding"),Error));
    State.Key.Kind=EGuLiHybridObjectKind::Miner;
    TestFalse(TEXT("An object cannot borrow another kind's family"),FGuLiHybridAdapterRegistry::Resolve(Data,State,Family,Error));
    Data.Numbers.Add(TEXT("adapter/family/ResourceFactory/state_version"),999);
    TestFalse(TEXT("Incompatible recovery schema fails before gameplay"),FGuLiHybridAdapterRegistry::Validate(Data,Error));
    Data.Numbers.Add(TEXT("adapter/family/ResourceFactory/state_version"),Family.StateVersion);
    const auto GoodData=Data;
    Data.Strings.Remove(TEXT("adapter/actor_components"));
    TestFalse(TEXT("Family-only snapshots cannot start the common component bridge"),FGuLiHybridAdapterRegistry::Validate(Data,Error));
    Data=GoodData;Data.Strings.Add(TEXT("adapter/actor_components"),TEXT("999"));
    TestFalse(TEXT("An incompatible object envelope prevents start"),FGuLiHybridAdapterRegistry::Validate(Data,Error));
    Data=GoodData;
    Data.Numbers.Remove(TEXT("component/Construction/state_version"));
    TestFalse(TEXT("A required component capability cannot be omitted"),FGuLiHybridAdapterRegistry::Validate(Data,Error));
    Data=GoodData;Data.Numbers.Add(TEXT("component/Production/state_version"),999);
    TestFalse(TEXT("Production version incompatibility prevents start"),FGuLiHybridAdapterRegistry::Validate(Data,Error));
    Data=GoodData;Data.Numbers.Add(TEXT("component/Unknown/state_version"),1);
    TestFalse(TEXT("An unavailable capability cannot be silently ignored"),FGuLiHybridComponentState::ValidateContracts(Data,Error));
    Data=GoodData;Data.Numbers.Add(TEXT("component/Construction/class"),1);
    TestFalse(TEXT("Contract keys cannot change type"),FGuLiHybridComponentState::ValidateContracts(Data,Error));
    Data=GoodData;
    Data.Rules[0].TargetDefinition=TEXT("building/undeclared");
    TestFalse(TEXT("Dynamic definitions also require a declared contract"),FGuLiHybridAdapterRegistry::Validate(Data,Error));
    FGuLiBuildingDefinition Building;Building.DefinitionId=106;Building.Mesh=NewObject<UStaticMesh>();
    Building.DisplayName=FText::FromString(TEXT("Authored capability variant"));
    Building.Category=EGuLiBuildingCategory::Factory;
    TestEqual(TEXT("Factory capability does not depend on numeric building ID"),GetGuLiHybridBuildingFamily(Building),FName(TEXT("ResourceFactory")));
    Building.Category=EGuLiBuildingCategory::Tower;
    TestEqual(TEXT("A new ordinary building uses the shared family"),GetGuLiHybridBuildingFamily(Building),FName(TEXT("PlacedBuilding")));
    Building.Category=EGuLiBuildingCategory::Stronghold;
    TestTrue(TEXT("Territory capture cannot silently become ordinary placed-building state"),GetGuLiHybridBuildingFamily(Building).IsNone());
    Building.Category=EGuLiBuildingCategory::Tower;
    Building.CompletionUnitTypeId=17;
    TestEqual(TEXT("Construction-to-unit uses the migrated placed-building family; manifest export validates its target unit"),
        GetGuLiHybridBuildingFamily(Building),FName(TEXT("PlacedBuilding")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiHybridSmallPeerPayloadTests,
    "GuLiStrike.Network.SmallPeerPayload",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiHybridSmallPeerPayloadTests::RunTest(const FString&)
{
    GuLiHybridPeerPayload::FDeflateContext Context;
    GuLiHybridPeerPayload::FDeflateContext FastContext(Z_BEST_SPEED);
    const auto CheckReuse=[&](const GuLiNet::Message& Message)
    {
        GuLiHybridPeerPayload::FDeflateContext FreshFast(Z_BEST_SPEED);
        GuLiNet::Bytes Fast,FreshFastBytes;
        const bool PackedFast=GuLiHybridPeerPayload::Pack(Message,Fast,nullptr,&FastContext);
        const bool PackedFreshFast=GuLiHybridPeerPayload::Pack(Message,FreshFastBytes,nullptr,&FreshFast);
        TestEqual(TEXT("Fast workspace reset preserves packing decision"),PackedFast,PackedFreshFast);
        if (PackedFast && PackedFreshFast)
        {
            TestTrue(TEXT("Fast streams retain no previous packet dictionary"),Fast==FreshFastBytes);
            auto Decoded=Message;Decoded.Body=Fast;uint64 PlainSize=0;
            TestTrue(TEXT("Fast packets keep bounded admission and exact application bytes"),
                GuLiHybridPeerPayload::PlainSize(Decoded,PlainSize) && PlainSize==Message.Body.size()
                && GuLiHybridPeerPayload::Unpack(Decoded) && Decoded.Body==Message.Body);
            Decoded.Body=Fast;Decoded.Body.pop_back();
            TestFalse(TEXT("Fast truncated packets remain rejected"),GuLiHybridPeerPayload::Unpack(Decoded));
            if (Fast.size()>8 && Fast[8]==2)
            {
                Decoded=Message;Decoded.Body=Fast;++Decoded.Header.Event.Words[3];
                TestFalse(TEXT("Fast dictionary packets still require exact identity"),GuLiHybridPeerPayload::Unpack(Decoded));
            }
        }
        GuLiNet::Bytes Fresh,Reused;size_t FreshReference=0,ReusedReference=0;
        const bool A=GuLiHybridPeerPayload::Pack(Message,Fresh,&FreshReference);
        const bool B=GuLiHybridPeerPayload::Pack(Message,Reused,&ReusedReference,&Context);
        TestEqual(TEXT("Reset workspace preserves the packing decision"),B,A);
        TestEqual(TEXT("Reset workspace preserves reference byte accounting"),ReusedReference,FreshReference);
        if (A && B)
        {
            TestTrue(TEXT("Reset workspace preserves every wire byte"),Fresh==Reused);
            auto Decoded=Message;Decoded.Body=Reused;
            TestTrue(TEXT("Reused workspace packets decode independently"),
                GuLiHybridPeerPayload::Unpack(Decoded) && Decoded.Body==Message.Body);
        }
    };
    for (const uint32 Size:{127u,128u,192u,511u,512u})
    {
        GuLiNet::Message M;M.Header.Kind=GuLiNet::MessageKind::Transaction;M.Body.resize(Size);
        for (uint32 I=0;I<Size;++I) M.Body[I]=uint8(I%16);
        CheckReuse(M);
        const auto Original=M.Body;GuLiNet::Bytes Encoded;
        const bool Packed=GuLiHybridPeerPayload::Pack(M,Encoded);
        TestEqual(TEXT("Small reliable compression threshold"),Packed,Size>=128);
        if (Packed)
        {
            TestTrue(TEXT("Encoding is smaller"),Encoded.size()<Original.size());
            M.Body=Encoded;uint64 Plain=0;
            TestTrue(TEXT("Admission reads expanded length without decode"),GuLiHybridPeerPayload::PlainSize(M,Plain));
            TestEqual(TEXT("Expanded bytes match original"),Plain,uint64(Size));
            TestTrue(TEXT("Small compressed message decodes"),GuLiHybridPeerPayload::Unpack(M));
            TestTrue(TEXT("Every original payload byte preserved"),M.Body==Original);
            M.Body=Encoded;M.Body[4]=0xff;
            TestFalse(TEXT("Unknown compressed schema rejected before allocation"),GuLiHybridPeerPayload::PlainSize(M,Plain));
            TestFalse(TEXT("Unknown compressed schema rejected before decode"),GuLiHybridPeerPayload::Unpack(M));
        }
    }
    GuLiNet::Message Escaped;Escaped.Header.Kind=GuLiNet::MessageKind::Transaction;GuLiNet::Writer W;
    W.U32(GuLiHybridPeerPayload::Tag);W.U32(123);Escaped.Body=W.Take();const auto Original=Escaped.Body;GuLiNet::Bytes Encoded;
    TestTrue(TEXT("Uncompressed tag collision still escaped"),GuLiHybridPeerPayload::Pack(Escaped,Encoded));Escaped.Body=std::move(Encoded);
    TestTrue(TEXT("Escaped body still decodes"),GuLiHybridPeerPayload::Unpack(Escaped)&&Escaped.Body==Original);
    GuLiNet::Message Identified;Identified.Header.Kind=GuLiNet::MessageKind::ObjectEvent;
    Identified.Header.Match={{0x94325187,0x71245632,0xaabc39d4,0xd0f12346}};
    Identified.Header.Sender={{0x86c01423,0xd873a9b0,0x36d87344,0x03ab51ff}};
    Identified.Header.Event={{0x59b90abc,0x8712439c,0x45ef1259,0x78591931}};
    Identified.Header.Object={GuLiNet::ObjectKind::Flight,{{0xe801abff,0x46dbb973,0x7b01f393,0x894a0136}},7};
    GuLiNet::Writer IdentityBody;IdentityBody.Identity(Identified.Header.Match);IdentityBody.Identity(Identified.Header.Sender);
    IdentityBody.Identity(Identified.Header.Event);IdentityBody.Key(Identified.Header.Object);
    IdentityBody.Text("event/"+Identified.Header.Event.ToString());IdentityBody.Text("linear_flight");
    for (uint32 I=0;I<300;++I) IdentityBody.U8(uint8((I*37+11)%251));
    Identified.Body=IdentityBody.Take();const auto Plain=Identified.Body;size_t ReferenceBytes=0;
    TestTrue(TEXT("Header dictionary packing succeeds"),GuLiHybridPeerPayload::Pack(Identified,Encoded,&ReferenceBytes));
    TestEqual(TEXT("Short old-baseline body remains uncompressed"),ReferenceBytes,Plain.size());
    TestTrue(TEXT("Header dictionary selected"),Encoded.size()>8 && Encoded[8]==2);
    Identified.Body=Encoded;++Identified.Header.Sequence;++Identified.Header.At;
    TestTrue(TEXT("Transport sequence and clock do not affect dictionary"),GuLiHybridPeerPayload::Unpack(Identified)&&Identified.Body==Plain);
    Identified.Body=Encoded;++Identified.Header.Event.Words[3];
    TestFalse(TEXT("Wrong identity dictionary cannot decode"),GuLiHybridPeerPayload::Unpack(Identified));
    --Identified.Header.Event.Words[3];Identified.Body=Encoded;Identified.Body.pop_back();
    TestFalse(TEXT("Truncated dictionary stream rejected"),GuLiHybridPeerPayload::Unpack(Identified));
    // Alternate dictionaries, incompressible data and large private payloads
    // through the same workspace; no prior packet may influence the wire.
    for (uint32 Pass=0;Pass<8;++Pass)
    {
        auto M=Identified;M.Body=Plain;
        M.Header.Event.Words[3]+=Pass;M.Header.Object.Generation+=Pass;
        CheckReuse(M);
        M.Header.Kind=GuLiNet::MessageKind::HandoffState;
        M.Body.resize(Pass%2?65536:4096);uint32 Noise=Pass+1;
        for (auto& Byte:M.Body) {Noise=1664525u*Noise+1013904223u;Byte=uint8(Noise>>24);}
        CheckReuse(M);
        for (size_t I=0;I<M.Body.size();++I) M.Body[I]=I%16<4?uint8((I/16)>>(8*(I%4))):uint8(I%7);
        CheckReuse(M);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiHybridReliableHistoryTests,
    "GuLiStrike.Network.ReliableHistory",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiHybridReliableHistoryTests::RunTest(const FString&)
{
    using namespace GuLiNet;
    using namespace GuLiHybridReliablePayload;
    const auto IdFor=[](uint32 N) {return Id{{0x17214321,0x23543612,0x67890981,N}};};
    std::vector<Message> Originals;uint32 Noise=43;Bytes Seed(4096);
    for (auto& Byte:Seed) {Noise=1664525u*Noise+1013904223u;Byte=uint8(Noise>>24);}
    for (uint32 N=0;N<5;++N)
    {
        Message M;M.Header.Kind=N%3==0?MessageKind::ObjectEvent:N%3==1?MessageKind::StateFragments:MessageKind::TransactionResult;
        M.Header.Match=IdFor(100);M.Header.Sender=IdFor(1);M.Header.Event=IdFor(200+N);
        M.Header.Object={ObjectKind::Flight,IdFor(300),1};M.Header.Epoch=1;M.Header.At=500+N;M.Body=Seed;
        if (N==1) for (size_t I=0;I<32;++I) ++M.Body[I];
        if (N==2) M.Body.assign(MaximumBodyBytes,42);
        if (N==3) M.Body.assign(128,55);
        Originals.push_back(std::move(M));
    }
    FPeerHistory Codec;Bytes FirstWire;
    for (const auto& Original:Originals)
    {
        Bytes Wire;
        if (!TestTrue(TEXT("Ordered stream chunk packs"),Pack(Original,Codec.Outgoing,Wire))) return false;
        if (FirstWire.empty()) FirstWire=Wire;
        auto Received=Original;Received.Body=Wire;uint64 Plain=0;bool Used=false;
        TestTrue(TEXT("Stream admission size is exact and bounded"),PlainSize(Received,Plain)&&Plain==Original.Body.size());
        auto Broken=Received;Broken.Body.pop_back();
        TestFalse(TEXT("Truncation rejected before consuming stream"),Unpack(Broken,Codec.Incoming,Used));
        Broken=Received;Broken.Body[9]^=1;
        TestFalse(TEXT("Wrong stream sequence rejected before decode"),Unpack(Broken,Codec.Incoming,Used));
        if (!TestTrue(TEXT("Exact application body recovered"),Unpack(Received,Codec.Incoming,Used)&&Used&&Received.Body==Original.Body)) return false;
        auto Retry=Original;Retry.Body=Wire;
        TestTrue(TEXT("Application backpressure reuses decoded chunk"),Unpack(Retry,Codec.Incoming,Used)&&Retry.Body==Original.Body);
        Codec.Outgoing.Commit(Original.Body);Codec.Incoming.Commit(Received.Body);
        TestEqual(TEXT("Admission keeps both sequences aligned"),Codec.Outgoing.Sequence,Codec.Incoming.Sequence);
        Retry=Original;Retry.Body=Wire;
        TestFalse(TEXT("Committed chunk cannot be reapplied"),Unpack(Retry,Codec.Incoming,Used));
    }
    FHistory Corrupt;auto Bad=Originals[0];Bad.Body=FirstWire;Bad.Body[17]^=1;bool Used=false;
    TestFalse(TEXT("Per-record checksum catches corruption"),Unpack(Bad,Corrupt,Used));
    TestTrue(TEXT("Corrupted streaming context cannot continue"),Corrupt.Failed);
    FHistory Restarted;Bad=Originals[0];Bad.Body=FirstWire;
    TestTrue(TEXT("Fresh connection decodes without old state"),Unpack(Bad,Restarted,Used)&&Bad.Body==Originals[0].Body);
    auto Ineligible=Originals[0];Bytes Wire;Ineligible.Header.Kind=MessageKind::Motion;
    TestFalse(TEXT("Unreliable motion cannot enter ordered stream"),Pack(Ineligible,Codec.Outgoing,Wire));
    Ineligible=Originals[0];Ineligible.Body.resize(MaximumBodyBytes+1);
    TestFalse(TEXT("Large checkpoints remain independently encoded"),Pack(Ineligible,Codec.Outgoing,Wire));
    Message Escaped;Escaped.Header.Kind=MessageKind::Transaction;Writer Escape;
    Escape.U32(Tag);Escaped.Body=Escape.Take();const auto EscapePlain=Escaped.Body;
    TestTrue(TEXT("Literal stream tag is escaped by independent codec"),GuLiHybridPeerPayload::Pack(Escaped,Wire));
    Escaped.Body=Wire;FHistory Literal;
    TestTrue(TEXT("Escaped stream tag preserves original bytes"),Unpack(Escaped,Literal,Used)&&!Used&&Escaped.Body==EscapePlain);

    QueueLimits Full;Full.MaxPendingMessages=0;PeerChannel Blocked(IdFor(901),Full);FHistory Reserved;
    bool EncoderRan=false;
    TestFalse(TEXT("Full transport rejects before changing compression state"),Blocked.QueueEncoded(Originals[0],0,Bound(Originals[0].Body.size()),
        [&](Bytes& B){EncoderRan=true;return Pack(Originals[0],Reserved,B);}));
    TestTrue(TEXT("Rejected reservation leaves stream untouched"),!EncoderRan&&Reserved.Sequence==0&&Reserved.Encoder==nullptr);

    QueueLimits Limits;Limits.BytesPerSecond.fill(8*1024*1024);Limits.ResendSeconds=.01;
    PeerChannel Sender(IdFor(900),Limits),Receiver(IdFor(900),Limits);FPeerHistory Transport;
    for (const auto& Original:Originals)
    {
        if (!TestTrue(TEXT("Reserved reliable stream admission"),Sender.QueueEncoded(Original,0,Bound(Original.Body.size()),
            [&](Bytes& B){return Pack(Original,Transport.Outgoing,B);}))) return false;
        Transport.Outgoing.Commit(Original.Body);
    }
    std::vector<Bytes> Packets,Acks;Bytes FirstAck,FirstBatch;
    size_t Delivered=0;bool Ready=false,Dropped=false,DroppedAck=false;
    const auto Ack=[&](std::span<const uint8> B) {Receiver.DeferAcknowledgment(B);return false;};
    const auto Apply=[&](const Message& M)
    {
        auto Decoded=M;bool Stream=false;
        if (!TestTrue(TEXT("Ordered delivery preserves stream decode"),Unpack(Decoded,Transport.Incoming,Stream)&&Stream)) return false;
        if (!Ready) return false;
        if (!TestTrue(TEXT("No duplicate application delivery"),Delivered<Originals.size())) return false;
        TestTrue(TEXT("Loss recovery preserves event and every application byte"),
            Decoded.Header.Event==Originals[Delivered].Header.Event&&Decoded.Body==Originals[Delivered].Body);
        Transport.Incoming.Commit(Decoded.Body);++Delivered;return true;
    };
    for (uint32 Round=0;Round<100;++Round)
    {
        const double At=(Round+1)*.02;
        Sender.Pump(At,[&](std::span<const uint8> B)
        {
            if (FirstBatch.empty() && B.size()>42 && (B[7]&16)) FirstBatch.assign(B.begin(),B.end());
            Packets.emplace_back(B.begin(),B.end());return true;
        });
        for (auto I=Packets.rbegin();I!=Packets.rend();++I)
        {
            if (!Dropped) {Dropped=true;continue;}
            Receiver.ReceiveWithAdmission(*I,At,Ack,Apply);Receiver.ReceiveWithAdmission(*I,At,Ack,Apply);
        }
        Packets.clear();Ready=Round>=5;Receiver.DrainDeliveries(Apply);
        Receiver.Pump(At,[&](std::span<const uint8> B){Acks.emplace_back(B.begin(),B.end());return true;});
        for (const auto& B:Acks)
        {
            if (FirstAck.empty()) FirstAck=B;
            if (!DroppedAck) {DroppedAck=true;continue;}
            Sender.Receive(B,At,[](auto){return true;},[](auto){});
        }
        Acks.clear();
        if (Delivered==Originals.size()&&Sender.Unacknowledged().empty()) break;
    }
    TestEqual(TEXT("Every stream record delivered once after recovery"),Delivered,Originals.size());
    TestTrue(TEXT("Fixture exercised data and ACK loss"),Dropped&&DroppedAck&&Sender.Metrics().RetransmitBytes>0);
    TestTrue(TEXT("Fixture exercised duplicates, admission retry and ACK coalescing"),Receiver.Metrics().DuplicatePackets>0
        &&Receiver.Metrics().DeliveryBackpressure>0&&Receiver.Metrics().AckBatchMessages>=Originals.size());
    TestTrue(TEXT("Reliable storage drains without channel failure"),Sender.Unacknowledged().empty()
        &&Receiver.Metrics().PendingDeliveryMessages==0&&!Sender.HasFailed()&&!Receiver.HasFailed());
    TestEqual(TEXT("Stream sequences agree after recovery"),Transport.Incoming.Sequence,Transport.Outgoing.Sequence);
    TestTrue(TEXT("Reliable records shared a datagram and retained individual ACKs"),Sender.Metrics().ReliableBatchMessages>=2);
    if (!FirstBatch.empty())
    {
        PeerChannel Victim(IdFor(900),Limits);size_t Applied=0,Acknowledged=0;
        ++FirstBatch[42]; // Declare one extra tail record without changing the valid packet envelope.
        TestFalse(TEXT("Malformed reliable batch tail is rejected atomically"),Victim.Receive(FirstBatch,.1,
            [&](auto){++Acknowledged;return true;},[&](auto){++Applied;}));
        TestTrue(TEXT("Invalid batch neither acknowledges nor applies a prefix"),Applied==0&&Acknowledged==0
            &&Victim.Metrics().PendingDeliveryMessages==0&&Victim.ReceivedThrough(Lane::Control)==0);
    }
    if (!FirstAck.empty())
    {
        PeerChannel Victim(IdFor(900),Limits);for (const auto& M:Originals) Victim.Queue(M,0);
        FirstAck.back()=0;const auto Before=Victim.Unacknowledged().size();
        TestFalse(TEXT("Malformed ACK set is rejected atomically"),Victim.Receive(FirstAck,.1,[](auto){return true;},[](auto){}));
        TestEqual(TEXT("Malformed ACK tail releases no reliable messages"),Victim.Unacknowledged().size(),Before);
    }
    return true;
}
#endif
