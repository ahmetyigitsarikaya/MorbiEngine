add_executable(MorbiRHIDescriptorTests
    RHIDescriptorTests.cpp
)

target_link_libraries(MorbiRHIDescriptorTests
    PRIVATE
        Morbi::Core
        Morbi::RHI
        Morbi::RHI.Vulkan
        Morbi::RHI.DX12
        Morbi::Render
        Morbi::Platform
        Morbi::Shader
)

morbi_apply_target_options(MorbiRHIDescriptorTests)

add_test(NAME MorbiRHIDescriptorTests COMMAND MorbiRHIDescriptorTests)

add_executable(MorbiRHIGenerationalHandleHashTests
    GenerationalHandleHashTests.cpp
)

target_link_libraries(MorbiRHIGenerationalHandleHashTests
    PRIVATE
        Morbi::Core
        Morbi::RHI
        Morbi::RHI.Vulkan
        Morbi::RHI.DX12
        Morbi::Render
        Morbi::Platform
        Morbi::Shader
)

morbi_apply_target_options(MorbiRHIGenerationalHandleHashTests)

add_test(NAME MorbiRHIGenerationalHandleHashTests COMMAND MorbiRHIGenerationalHandleHashTests)