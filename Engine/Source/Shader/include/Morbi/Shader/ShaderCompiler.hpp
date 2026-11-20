#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Morbi/Core/Result.hpp"
#include "Morbi/Core/Types.hpp"
#include "Morbi/Shader/ShaderReflection.hpp"

namespace Morbi::Shader
{
    enum class ShaderModel : u8
    {
        SM6_0 = 0,
        SM6_1,
        SM6_2,
        SM6_3,
        SM6_4,
        SM6_5,
        SM6_6
    };

    struct ShaderMacro
    {
        std::string Name;
        std::string Value;
    };

    struct ShaderIncludeDependency
    {
        std::string CanonicalPath;
        u64 ContentHash = 0;
        u64 ByteSize = 0;

        [[nodiscard]] bool operator==(const ShaderIncludeDependency& other) const noexcept = default;
    };

    struct ShaderCompilerVersion
    {
        u32 Major = 0;
        u32 Minor = 0;
        u32 CommitCount = 0;
        std::string CommitHash;
        std::string DisplayString;
        u64 VersionHash = 0;

        [[nodiscard]] bool operator==(const ShaderCompilerVersion& other) const noexcept = default;
    };

    struct ShaderCompileRequest
    {
        std::string SourceCode;
        std::string VirtualFilePath = "InlineShader.hlsl";
        std::string EntryPoint = "main";
        ShaderStage Stage = ShaderStage::Vertex;
        ShaderTargetFormat TargetFormat = ShaderTargetFormat::DXIL;
        bool EnableOptimization = true;
        bool GenerateDebugInfo = false;
        std::vector<ShaderMacro> Defines;
        std::vector<std::filesystem::path> IncludeDirs;
        ShaderModel Model = ShaderModel::SM6_0;
        bool ValidateSpirV = true;
        std::vector<std::string> ExtraArguments;
    };

    struct CompiledShaderArtifact
    {
        ShaderStage Stage = ShaderStage::Vertex;
        ShaderTargetFormat TargetFormat = ShaderTargetFormat::DXIL;
        ShaderModel Model = ShaderModel::SM6_0;
        u64 ContentHash = 0;
        u64 CompilerVersionHash = 0;
        std::string CompilerVersionString;
        std::vector<u8> Bytecode;
        ShaderReflectionData Reflection;
        std::vector<ShaderIncludeDependency> IncludeDependencies;
        std::string CompilerMessages;
    };

    class DxcModuleContext;

    class MORBI_API ShaderCompiler
    {
    public:
        explicit ShaderCompiler(std::filesystem::path dxcToolDirectory = {});
        ~ShaderCompiler();

        ShaderCompiler(const ShaderCompiler&) = default;
        ShaderCompiler& operator=(const ShaderCompiler&) = default;
        ShaderCompiler(ShaderCompiler&&) noexcept = default;
        ShaderCompiler& operator=(ShaderCompiler&&) noexcept = default;

        [[nodiscard]] bool IsDXCAvailable() const noexcept;
        [[nodiscard]] bool IsSpirVValidatorAvailable() const noexcept;
        [[nodiscard]] const std::filesystem::path& GetDxcExecutablePath() const noexcept { return m_dxcExePath; }
        [[nodiscard]] const std::filesystem::path& GetDxcLibraryPath() const noexcept { return m_dxcLibPath; }
        [[nodiscard]] const std::filesystem::path& GetSpirVValidatorPath() const noexcept { return m_spirvValPath; }
        [[nodiscard]] const ShaderCompilerVersion& GetCompilerVersion() const noexcept;

        [[nodiscard]] Core::Result<CompiledShaderArtifact, std::string> Compile(const ShaderCompileRequest& request) const;
        [[nodiscard]] Core::Result<CompiledShaderArtifact, std::string> CompileFromFile(
            const std::filesystem::path& filePath,
            std::string_view entryPoint,
            ShaderStage stage,
            ShaderTargetFormat targetFormat,
            std::span<const ShaderMacro> defines = {},
            ShaderModel model = ShaderModel::SM6_0) const;

        [[nodiscard]] bool ValidateSpirVBytecode(
            std::span<const u8> spirvBytecode,
            std::string& outDiagnostics) const;

        [[nodiscard]] static const char* GetTargetProfileName(ShaderStage stage, ShaderModel model) noexcept;

        [[nodiscard]] static u64 ComputeRequestHash(
            const ShaderCompileRequest& request,
            const ShaderCompilerVersion* compilerVersion = nullptr,
            std::span<const ShaderIncludeDependency> includeDependencies = {}) noexcept;

        [[nodiscard]] static bool ComputeFileContentHash(
            const std::filesystem::path& filePath,
            u64& outHash,
            u64& outByteSize) noexcept;

        [[nodiscard]] Core::Result<ShaderReflectionData, std::string> ReflectBytecode(
            ShaderTargetFormat targetFormat,
            std::span<const u8> bytecode,
            std::optional<ShaderStage> expectedStage = std::nullopt,
            std::string_view expectedEntryPoint = {}) const;

        [[nodiscard]] static ShaderReflectionData ReflectSourceAndBinary(
            const ShaderCompileRequest& request,
            std::span<const u8> bytecode);

    private:
        std::filesystem::path m_dxcDirectory;
        std::filesystem::path m_dxcExePath;
        std::filesystem::path m_dxcLibPath;
        std::filesystem::path m_spirvValPath;
        std::shared_ptr<DxcModuleContext> m_dxcModule;
    };
}
