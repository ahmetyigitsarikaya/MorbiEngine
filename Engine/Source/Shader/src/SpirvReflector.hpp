#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "Morbi/Core/Result.hpp"
#include "Morbi/Core/Types.hpp"
#include "Morbi/Shader/ShaderReflection.hpp"

namespace Morbi::Shader::Detail
{
    /// Reflect compiled Vulkan 1.3 SPIR-V bytecode into unified ShaderReflectionData
    /// using Khronos SPIRV-Reflect (SPV_REFLECT_MODULE_FLAG_NO_COPY with RAII guard).
    [[nodiscard]] Core::Result<ShaderReflectionData, std::string> ReflectSpirvBinary(
        std::span<const u8> bytecode,
        std::optional<ShaderStage> expectedStage = std::nullopt,
        std::string_view expectedEntryPoint = {});
}
