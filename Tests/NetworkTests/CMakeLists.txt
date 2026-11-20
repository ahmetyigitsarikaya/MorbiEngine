add_executable(MorbiNetworkTests
    NetworkTests.cpp
)

target_link_libraries(MorbiNetworkTests
    PRIVATE
        Morbi::Core
        Morbi::Network
        Morbi::Simulation
)

if(TARGET MorbiServer)
    add_dependencies(MorbiNetworkTests MorbiServer)
endif()

morbi_apply_target_options(MorbiNetworkTests)

add_test(NAME MorbiNetworkTests COMMAND MorbiNetworkTests)
