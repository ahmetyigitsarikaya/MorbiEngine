add_executable(MorbiPhysicsTests
    PhysicsTests.cpp
)

target_link_libraries(MorbiPhysicsTests
    PRIVATE
        Morbi::Core
        Morbi::Scene
        Morbi::Physics
        Morbi::UI
)

morbi_apply_target_options(MorbiPhysicsTests)

add_test(NAME MorbiPhysicsTests COMMAND MorbiPhysicsTests)

add_executable(MorbiCSharpPhysicsTests
    CSharpPhysicsTests.cpp
)

target_link_libraries(MorbiCSharpPhysicsTests
    PRIVATE
        Morbi::Core
        Morbi::Scene
        Morbi::Physics
        Morbi::Audio
        Morbi::Script
)

morbi_apply_target_options(MorbiCSharpPhysicsTests)

add_test(NAME MorbiCSharpPhysicsTests COMMAND MorbiCSharpPhysicsTests)
