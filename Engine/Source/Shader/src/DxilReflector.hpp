#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "DxcRuntimeBridge.hpp"
#include "Morbi/Core/Result.hpp"
#include "Morbi/Core/Types.hpp"
#include "Morbi/Shader/ShaderReflection.hpp"

namespace Morbi::Shader::Detail
{
    /// Reflect compiled DirectX 12 DXIL container bytecode into unified ShaderReflectionData
    /// using DXC IDxcContainerReflection and ID3D12ShaderReflection.
    [[nodiscard]] Core::Result<ShaderReflectionData, std::string> ReflectDxilBinary(
        std::span<const u8> bytecode,
        IDxcUtils* utils,
        IDxcContainerReflection* containerReflection,
        std::optional<ShaderStage> expectedStage = std::nullopt,
        std::string_view expectedEntryPoint = {});
}
