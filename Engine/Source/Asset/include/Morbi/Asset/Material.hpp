#pragma once

#include <string>
#include <vector>

#include "Morbi/Asset/AssetRegistry.hpp"
#include "Morbi/Core/Export.hpp"
#include "Morbi/Core/Math/Math.hpp"
#include "Morbi/Core/Types.hpp"
#include "Morbi/RHI/IDevice.hpp"

namespace Morbi::Asset
{
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
    // ============================================================================
    // MaterialDescriptor (Data-Driven CPU/Asset Material Definition)
    // ============================================================================
    struct MaterialDescriptor
    {
        std::string Name = "DefaultMaterial";
        std::string AlbedoTexturePath;    // VFS path e.g. "assets://Textures/..."
        std::string NormalTexturePath;    // VFS path e.g. "assets://Textures/..."
        std::string SpecularTexturePath;  // VFS path e.g. "assets://Textures/..."
        Math::Vec4  BaseColorFactor{ 1.0f, 1.0f, 1.0f, 1.0f };
        f32         Roughness = 0.50f;
        f32         Metallic  = 0.05f;
    };

    // ============================================================================
    // MaterialInstance (GPU Bindless Material Bound to Submeshes / MeshRenderer)
    // ============================================================================
    struct MaterialInstance
    {
        std::string Name = "DefaultMaterial";
        AssetUUID   MaterialUUID{};

        RHI::TextureHandle AlbedoTexture   = RHI::TextureHandle::Invalid();
        RHI::TextureHandle NormalTexture   = RHI::TextureHandle::Invalid();
        RHI::TextureHandle SpecularTexture = RHI::TextureHandle::Invalid();

        RHI::BindlessResourceIndex AlbedoBindless{};
        RHI::BindlessResourceIndex NormalBindless{};
        RHI::BindlessResourceIndex SpecularBindless{};

        Math::Vec4 BaseColorFactor{ 1.0f, 1.0f, 1.0f, 1.0f };
        f32        Roughness = 0.50f;
        f32        Metallic  = 0.05f;

        [[nodiscard]] constexpr bool HasAlbedoTexture() const noexcept
        {
            return AlbedoTexture.IsValid() && AlbedoBindless.IsValid();
        }

        [[nodiscard]] constexpr bool HasNormalTexture() const noexcept
        {
            return NormalTexture.IsValid() && NormalBindless.IsValid();
        }

        [[nodiscard]] constexpr bool HasSpecularTexture() const noexcept
        {
            return SpecularTexture.IsValid() && SpecularBindless.IsValid();
        }
    };
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
}
