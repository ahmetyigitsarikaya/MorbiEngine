#pragma once

#include <span>
#include <string>
#include <string_view>

#include "Morbi/Core/Result.hpp"
#include "Morbi/RHI/PipelineLayout.hpp"
#include "Morbi/Shader/ShaderReflection.hpp"

namespace Morbi::Shader
{
    struct PipelineLayoutBuildOptions
    {
        CrossBackendBindingPolicy Policy = CrossBackendBindingPolicy::StrictOneToOne();
        RHI::PipelineLayoutMode Mode = RHI::PipelineLayoutMode::Standalone;
        bool ValidateStageInterfaces = true;
        bool UseRootDescriptorForSingleDynamicCbv = false;
        std::string_view DebugName;
        RHI::PipelineLayoutLimits Limits{};
        RHI::BackendAPI TargetBackend = RHI::BackendAPI::Null;
    };

    class MORBI_API PipelineLayoutBuilder
    {
    public:
        [[nodiscard]] static Core::Result<RHI::PipelineLayoutDesc, std::string> BuildFromReflection(
            std::span<const ShaderReflectionData> stages,
            const PipelineLayoutBuildOptions& options = {});

        [[nodiscard]] static Core::Result<RHI::PipelineLayoutDesc, std::string> BuildFromDualBackendReflections(
            std::span<const ShaderReflectionData> spirvStages,
            std::span<const ShaderReflectionData> dxilStages,
            const PipelineLayoutBuildOptions& options = {});
    };
}
