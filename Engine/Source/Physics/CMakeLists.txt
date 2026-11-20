add_library(Morbi.Physics SHARED
    src/PhysicsWorld.cpp
    src/PhysicsQueries.cpp
    src/PhysicsJoints.cpp
    src/PhysicsCharacter.cpp
)

add_library(Morbi::Physics ALIAS Morbi.Physics)
target_compile_definitions(Morbi.Physics PRIVATE MORBI_PHYSICS_EXPORTS=1)

target_include_directories(Morbi.Physics
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(Morbi.Physics
    PUBLIC
        Morbi::Core
        Morbi::Scene
)

if(NOT TARGET Jolt)
    message(FATAL_ERROR "[Morbi.Physics] Jolt Physics target is missing; cannot build fake physics backend.")
endif()

target_link_libraries(Morbi.Physics PRIVATE Jolt)
target_compile_definitions(Morbi.Physics PUBLIC MORBI_HAS_JOLT_LIB=1)

morbi_apply_target_options(Morbi.Physics)
