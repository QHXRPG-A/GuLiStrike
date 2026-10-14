#include "Network/Private/GuLiHybridReflectedState.h"
#include "Network/Private/GuLiHybridStateArchive.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilitySet.h"
#include "Gameplay/Ship/Build/GuLiShipBuildCatalog.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiHybridReflectedStateTests,
    "GuLiStrike.Network.ReflectedState",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiHybridReflectedStateTests::RunTest(const FString&)
{
    FGuLiGroupAbilityConfigSnapshot Config;Config.ShipInstanceId=FGuid::NewGuid();Config.MatchEpoch=17;
    Config.OwnerPlayerGuid=FGuid::NewGuid();Config.WingmanTypeId=TEXT("RecoveryMechanism");
    Config.FormationDefinitionChecksum=MAX_uint64-11;Config.SnapshotHash=(uint64(1)<<63)+29;
    FGuLiHybridStateArchive Write;TArray<uint8> Bytes;
    TestTrue(TEXT("Pure nested ability values encode"),FGuLiHybridReflectedState::Value(Write,Config) && Write.Finish(Bytes));
    FGuLiGroupAbilityConfigSnapshot Restored;FGuLiHybridStateArchive Read(Bytes);
    TestTrue(TEXT("Pure nested ability values decode"),FGuLiHybridReflectedState::Value(Read,Restored) && Read.Done());
    TestEqual(TEXT("64-bit checksum is exact"),Restored.FormationDefinitionChecksum,Config.FormationDefinitionChecksum);
    TestEqual(TEXT("64-bit hash is exact"),Restored.SnapshotHash,Config.SnapshotHash);
    TestEqual(TEXT("Stable identity survives"),Restored.ShipInstanceId,Config.ShipInstanceId);
    for (int32 Length=0;Length<Bytes.Num();++Length)
    {
        FGuLiGroupAbilityConfigSnapshot Cut;FGuLiHybridStateArchive Truncated(MakeArrayView(Bytes.GetData(),Length));
        if (FGuLiHybridReflectedState::Value(Truncated,Cut) && Truncated.Done())
        {AddError(FString::Printf(TEXT("Truncated private state accepted at %d bytes"),Length));break;}
    }
    auto Wrong=Bytes;Wrong[0]=99;FGuLiGroupAbilityConfigSnapshot WrongState;FGuLiHybridStateArchive WrongRead(Wrong);
    TestFalse(TEXT("Unknown value schema fails"),FGuLiHybridReflectedState::Value(WrongRead,WrongState) && WrongRead.Done());
    FGuLiShipRunBuildState Other;FGuLiHybridStateArchive WrongType(Bytes);
    TestFalse(TEXT("Another struct cannot consume the same fields"),FGuLiHybridReflectedState::Value(WrongType,Other) && WrongType.Done());
    FGuLiShipAbilityGrant ObjectGrant;FGuLiHybridStateArchive PointerWrite;TArray<uint8> PointerBytes;
    TestFalse(TEXT("Object fields require an explicit resource contract"),FGuLiHybridReflectedState::Value(PointerWrite,ObjectGrant) && PointerWrite.Finish(PointerBytes));
    FGuLiShipRunBuildState Large;Large.ChosenNodeIds.SetNum(4097);FGuLiHybridStateArchive LargeWrite;TArray<uint8> LargeBytes;
    TestFalse(TEXT("Collection capacity is bounded before allocation"),FGuLiHybridReflectedState::Value(LargeWrite,Large) && LargeWrite.Finish(LargeBytes));
    return true;
}
#endif
