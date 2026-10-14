cmake_minimum_required(VERSION 3.24)
project(GuLiNetCore LANGUAGES CXX)

# Each gameplay extension registers itself. UBT discovers these sources as
# usual; CMake discovers the same leaf directory without a central type list.
file(GLOB GULI_GAMEPLAY_CAPABILITIES CONFIGURE_DEPENDS "Private/Capabilities/*.cpp")

add_library(GuLiNetCore STATIC
    Private/GuLiNetTrace.cpp
    Private/GuLiNetHash.cpp
    Private/GuLiNetManifest.cpp
    Private/GuLiNetClient.cpp
    Private/GuLiNetCommand.cpp
    Private/GuLiNetHandoff.cpp
    Private/GuLiNetPeerWork.cpp
    Private/GuLiNetGameplayTypes.cpp
    Private/GuLiNetOwnerState.cpp
    Private/GuLiNetOwnerFragments.cpp
    Private/GuLiNetOwnerTransactions.cpp
    Private/GuLiNetTypes.cpp
    Private/GuLiNetOwnership.cpp
    Private/GuLiNetRules.cpp
    Private/GuLiNetResourceLedger.cpp
    Private/GuLiNetMiningReservations.cpp
    ${GULI_GAMEPLAY_CAPABILITIES}
    Private/GuLiNetCapability.cpp
    Private/GuLiNetProtocol.cpp
    Private/GuLiNetTransport.cpp
    Private/GuLiNetRoom.cpp
    Private/GuLiNetRoomMetadata.cpp
    Private/GuLiNetWork.cpp
    Private/GuLiNetCooldown.cpp
    Private/GuLiNetUpgrade.cpp)
target_include_directories(GuLiNetCore PUBLIC Public)
target_compile_features(GuLiNetCore PUBLIC cxx_std_20)
target_compile_definitions(GuLiNetCore PUBLIC GULINETCORE_STANDALONE=1)
set_target_properties(GuLiNetCore PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(GuLiNetCore PRIVATE /W4 /permissive- /fp:precise)
else()
    target_compile_options(GuLiNetCore PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
endif()
