add_library(Morbi.Network SHARED
    src/Serialization/BitStream.cpp
    src/Protocol/ProtocolTypes.cpp
    src/Security/CryptoContext.cpp
    src/Security/ConnectionRateLimiter.cpp
    src/Session/SessionRegistry.cpp
    src/Replication/NetworkIdPool.cpp
    src/Replication/ReplicationContracts.cpp
    src/Replication/InterestManagement.cpp
    src/Replication/ReplicationManager.cpp
    src/Input/InputCommandPipeline.cpp
    src/RPC/RpcRouter.cpp
    src/Transport/NetworkSimulator.cpp
    src/Transport/EnetTransportBackend.cpp
    src/Connection/ConnectionManager.cpp
)

add_library(Morbi::Network ALIAS Morbi.Network)
target_compile_definitions(Morbi.Network PRIVATE MORBI_NETWORK_EXPORTS=1)

target_include_directories(Morbi.Network
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(Morbi.Network
    PUBLIC
        Morbi::Core
    PRIVATE
        ENet::ENet
        Sodium::Sodium
)

morbi_apply_target_options(Morbi.Network)
