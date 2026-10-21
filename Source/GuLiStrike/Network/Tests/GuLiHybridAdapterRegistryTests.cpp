#include "Network/GuLiHybridAdapterRegistry.h"
#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "Network/Private/GuLiHybridComponentState.h"
#include "Network/Private/GuLiHybridPeerPayload.h"
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
#endif
