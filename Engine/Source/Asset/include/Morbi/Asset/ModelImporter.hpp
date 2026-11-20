#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Morbi/Asset/Animation.hpp"
#include "Morbi/Asset/Material.hpp"
#include "Morbi/Core/Export.hpp"
#include "Morbi/Core/Math/Math.hpp"
#include "Morbi/Core/Result.hpp"
#include "Morbi/Core/Types.hpp"

namespace Morbi::Asset
{
#pragma pack(push, 1)
    // 92-byte tightly packed GPU Vertex Layout with MikkTSpace Tangent + Handedness Sign + Bitangent + Skinning Bones/Weights
    struct StaticMeshVertex
    {
        Math::Vec3 Position{ 0.0f, 0.0f, 0.0f };
        Math::Vec3 Normal{ 0.0f, 1.0f, 0.0f };
        Math::Vec2 UV{ 0.0f, 0.0f };
        Math::Vec3 Tangent{ 1.0f, 0.0f, 0.0f };
        f32        TangentSign = 1.0f; // MikkTSpace Handedness sign (+1.0f / -1.0f)
        Math::Vec3 Bitangent{ 0.0f, 0.0f, 1.0f };

        // Bone IDs and Weights for GPU Skeletal Skinning (up to 4 bones per vertex)
        i32        BoneIDs[4]{ -1, -1, -1, -1 };
        f32        BoneWeights[4]{ 0.0f, 0.0f, 0.0f, 0.0f };

        void AddBoneData(i32 boneId, f32 weight) noexcept
        {
            if (boneId < 0 || weight <= 0.0f) return;

            // 1. Accumulate weight if bone is already mapped to this vertex
            for (int i = 0; i < 4; ++i)
            {
                if (BoneIDs[i] == boneId)
                {
                    BoneWeights[i] += weight;
                    return;
                }
            }

            // 2. Assign to first empty slot
            for (int i = 0; i < 4; ++i)
            {
                if (BoneIDs[i] < 0)
                {
                    BoneIDs[i] = boneId;
                    BoneWeights[i] = weight;
                    return;
                }
            }

            // 3. If all 4 slots are occupied, replace the smallest weight if the new weight is greater
            int minIdx = 0;
            for (int i = 1; i < 4; ++i)
            {
                if (BoneWeights[i] < BoneWeights[minIdx])
                {
                    minIdx = i;
                }
            }
            if (weight > BoneWeights[minIdx])
            {
                BoneIDs[minIdx] = boneId;
                BoneWeights[minIdx] = weight;
            }
        }

        void NormalizeBoneWeights() noexcept
        {
            const f32 total = BoneWeights[0] + BoneWeights[1] + BoneWeights[2] + BoneWeights[3];
            if (total > 1e-6f)
            {
                const f32 inv = 1.0f / total;
                BoneWeights[0] *= inv;
                BoneWeights[1] *= inv;
                BoneWeights[2] *= inv;
                BoneWeights[3] *= inv;
            }
        }
    };
    static_assert(sizeof(StaticMeshVertex) == 92, "StaticMeshVertex must be 92 bytes.");

    // Standard engine alias
    using Vertex = StaticMeshVertex;
#pragma pack(pop)

#if defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable: 4324)
#endif
    struct BoneInfo
    {
        std::string Name;
        i32 Id = -1;
        Math::Mat4 OffsetMatrix = Math::Mat4::Identity(); // Inverse bind pose matrix
    };
#if defined(_MSC_VER)
    #pragma warning(pop)
#endif

    struct SubmeshDescriptor
    {
        std::string Name = "DefaultSubmesh";
        u32 IndexOffset = 0;
        u32 IndexCount = 0;
        u32 MaterialIndex = 0;
        Math::Vec3 BoundsMin{ 0.0f, 0.0f, 0.0f };
        Math::Vec3 BoundsMax{ 0.0f, 0.0f, 0.0f };
    };

    struct ImportedMeshData
    {
        std::string Name = "UnnamedMesh";
        std::vector<StaticMeshVertex> Vertices;
        std::vector<u32> Indices;
        std::vector<SubmeshDescriptor> Submeshes;
        std::vector<MaterialDescriptor> Materials;
        std::unordered_map<std::string, i32> BoneMapping;
        std::vector<BoneInfo> BoneOffsets;
        std::vector<SkeletonNode> SkeletonHierarchy;
        std::vector<AnimationClip> Animations;
        Math::Vec3 BoundingBoxMin{ 0.0f, 0.0f, 0.0f };
        Math::Vec3 BoundingBoxMax{ 0.0f, 0.0f, 0.0f };
        bool TangentsComputed = false;
        bool HasSkeleton = false;
    };

    struct ModelImportOptions
    {
        bool GenerateMikkTSpaceTangents = true;
        bool FlipUVVertical = false;
        f32 UniformScale = 1.0f;
    };

    // ============================================================================
    // Multi-Format 3D Model Importer (.obj, .gltf, .glb, .fbx) + MikkTSpace Solver
    // ============================================================================
    class MORBI_API ModelImporter
    {
    public:
        [[nodiscard]] static Core::Result<ImportedMeshData, std::string> ImportFromFile(
            const std::filesystem::path& filePath,
            const ModelImportOptions& options = {});

        [[nodiscard]] static Core::Result<ImportedMeshData, std::string> ImportFromOBJText(
            std::string_view objText,
            std::string_view meshName = "OBJMesh",
            const ModelImportOptions& options = {});

        [[nodiscard]] static Core::Result<ImportedMeshData, std::string> ImportFromGLTFJson(
            std::string_view gltfJson,
            std::string_view meshName = "GLTFMesh",
            const ModelImportOptions& options = {});

        [[nodiscard]] static Core::Result<std::vector<AnimationClip>, std::string> ImportAnimationsFromFile(
            const std::filesystem::path& filePath);

        // MikkTSpace-compliant Tangent & Bitangent surface derivative solver
        // (Essential for tangent-space Normal Mapping in PBR shaders)
        static void ComputeMikkTSpaceTangents(
            std::span<StaticMeshVertex> vertices,
            std::span<const u32> indices);

        static void RecomputeBoundingBox(ImportedMeshData& mesh) noexcept;
    };
}
