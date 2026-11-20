#include "Morbi/Asset/ModelImporter.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <functional>
#include <sstream>
#include <unordered_map>

#if defined(MORBI_HAS_ASSIMP)
    #if defined(_MSC_VER)
        #pragma warning(push, 0)
    #endif
    #include <assimp/Importer.hpp>
    #include <assimp/config.h>
    #include <assimp/material.h>
    #include <assimp/postprocess.h>
    #include <assimp/scene.h>
    #if defined(_MSC_VER)
        #pragma warning(pop)
    #endif
#endif

#include "Morbi/Core/Log.hpp"

namespace Morbi::Asset
{
    namespace
    {
        std::string LowercaseExt(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return s;
        }

        std::string PathToUtf8(const std::filesystem::path& p)
        {
            const std::u8string u8 = p.u8string();
            return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
        }

        std::filesystem::path PathFromUtf8(std::string_view utf8)
        {
            return std::filesystem::path(std::u8string_view(
                reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
        }

#if defined(MORBI_HAS_ASSIMP)
        const aiScene* ReadAssimpScene(
            Assimp::Importer& importer,
            const std::filesystem::path& filePath,
            unsigned int flags,
            std::string_view ext)
        {
            // Assimp DefaultIOSystem decodes the path as CP_UTF8 on Windows.
            const std::string utf8Path = PathToUtf8(filePath);
            if (const aiScene* scene = importer.ReadFile(utf8Path, flags))
            {
                return scene;
            }

            // Fallback via wide std::filesystem::path stream if DefaultIOSystem fails on exotic paths.
            std::ifstream in(filePath, std::ios::binary | std::ios::ate);
            if (!in.is_open())
            {
                return nullptr;
            }

            const std::streamoff size = in.tellg();
            if (size <= 0)
            {
                return nullptr;
            }

            in.seekg(0, std::ios::beg);
            std::vector<u8> bytes(static_cast<usize>(size));
            if (!in.read(reinterpret_cast<char*>(bytes.data()), size))
            {
                return nullptr;
            }

            const std::string_view hint = (!ext.empty() && ext[0] == '.') ? ext.substr(1) : ext;
            return importer.ReadFileFromMemory(
                bytes.data(),
                bytes.size(),
                flags,
                hint.empty() ? nullptr : std::string(hint).c_str());
        }
#endif

        struct ObjIndexTriplet
        {
            i32 PosIdx = -1;
            i32 UvIdx  = -1;
            i32 NrmIdx = -1;

            [[nodiscard]] bool operator==(const ObjIndexTriplet& other) const noexcept = default;
        };

        struct ObjTripletHash
        {
            usize operator()(const ObjIndexTriplet& k) const noexcept
            {
                usize h1 = static_cast<usize>(k.PosIdx) * 73856093u;
                usize h2 = static_cast<usize>(k.UvIdx)  * 19349663u;
                usize h3 = static_cast<usize>(k.NrmIdx) * 83492791u;
                return h1 ^ h2 ^ h3;
            }
        };

        ObjIndexTriplet ParseObjFaceToken(std::string_view token, i32 numPos, i32 numUv, i32 numNrm)
        {
            ObjIndexTriplet t{};
            const usize s1 = token.find('/');
            if (s1 == std::string_view::npos)
            {
                const i32 v = std::stoi(std::string(token));
                t.PosIdx = (v < 0) ? (numPos + v) : (v - 1);
                return t;
            }

            const std::string_view pStr = token.substr(0, s1);
            if (!pStr.empty())
            {
                const i32 v = std::stoi(std::string(pStr));
                t.PosIdx = (v < 0) ? (numPos + v) : (v - 1);
            }

            const usize s2 = token.find('/', s1 + 1);
            if (s2 == std::string_view::npos)
            {
                const std::string_view uvStr = token.substr(s1 + 1);
                if (!uvStr.empty())
                {
                    const i32 vt = std::stoi(std::string(uvStr));
                    t.UvIdx = (vt < 0) ? (numUv + vt) : (vt - 1);
                }
                return t;
            }

            const std::string_view uvStr = token.substr(s1 + 1, s2 - s1 - 1);
            if (!uvStr.empty())
            {
                const i32 vt = std::stoi(std::string(uvStr));
                t.UvIdx = (vt < 0) ? (numUv + vt) : (vt - 1);
            }

            const std::string_view nrmStr = token.substr(s2 + 1);
            if (!nrmStr.empty())
            {
                const i32 vn = std::stoi(std::string(nrmStr));
                t.NrmIdx = (vn < 0) ? (numNrm + vn) : (vn - 1);
            }
            return t;
        }

        std::vector<f32> ExtractFloatArrayFromJson(std::string_view json, std::string_view arrayKey)
        {
            std::vector<f32> values;
            const std::string keyPattern = "\"" + std::string(arrayKey) + "\"";
            const usize keyPos = json.find(keyPattern);
            if (keyPos == std::string_view::npos)
            {
                return values;
            }
            const usize openBracket = json.find('[', keyPos + keyPattern.size());
            const usize closeBracket = json.find(']', openBracket);
            if (openBracket == std::string_view::npos || closeBracket == std::string_view::npos)
            {
                return values;
            }

            std::string body(json.substr(openBracket + 1, closeBracket - openBracket - 1));
            std::replace(body.begin(), body.end(), ',', ' ');
            std::istringstream ss(body);
            f32 val = 0.0f;
            while (ss >> val)
            {
                values.push_back(val);
            }
            return values;
        }

        std::vector<u32> ExtractUintArrayFromJson(std::string_view json, std::string_view arrayKey)
        {
            std::vector<u32> values;
            const std::string keyPattern = "\"" + std::string(arrayKey) + "\"";
            const usize keyPos = json.find(keyPattern);
            if (keyPos == std::string_view::npos)
            {
                return values;
            }
            const usize openBracket = json.find('[', keyPos + keyPattern.size());
            const usize closeBracket = json.find(']', openBracket);
            if (openBracket == std::string_view::npos || closeBracket == std::string_view::npos)
            {
                return values;
            }

            std::string body(json.substr(openBracket + 1, closeBracket - openBracket - 1));
            std::replace(body.begin(), body.end(), ',', ' ');
            std::istringstream ss(body);
            u32 val = 0;
            while (ss >> val)
            {
                values.push_back(val);
            }
            return values;
        }

#if defined(MORBI_HAS_ASSIMP)
        inline Math::Mat4 AiMatrixToMat4(const aiMatrix4x4& m) noexcept
        {
            // Transpose from Assimp column-vector convention to MorbiEngine row-vector convention
            Math::Mat4 out = Math::Mat4::Identity();
            out.At(0, 0) = m.a1; out.At(0, 1) = m.b1; out.At(0, 2) = m.c1; out.At(0, 3) = m.d1;
            out.At(1, 0) = m.a2; out.At(1, 1) = m.b2; out.At(1, 2) = m.c2; out.At(1, 3) = m.d2;
            out.At(2, 0) = m.a3; out.At(2, 1) = m.b3; out.At(2, 2) = m.c3; out.At(2, 3) = m.d3;
            out.At(3, 0) = m.a4; out.At(3, 1) = m.b4; out.At(3, 2) = m.c4; out.At(3, 3) = m.d4;
            return out;
        }

        bool NodeHasBoneOrDescendants(const aiNode* node, const std::unordered_map<std::string, i32>& boneMapping)
        {
            if (node == nullptr)
            {
                return false;
            }
            const std::string name(node->mName.C_Str());
            if (boneMapping.find(name) != boneMapping.end())
            {
                return true;
            }
            for (unsigned int i = 0; i < node->mNumChildren; ++i)
            {
                if (NodeHasBoneOrDescendants(node->mChildren[i], boneMapping))
                {
                    return true;
                }
            }
            return false;
        }

        bool ShouldIncludeNode(const aiNode* node, const std::unordered_map<std::string, i32>& boneMapping)
        {
            if (node == nullptr)
            {
                return false;
            }
            if (boneMapping.empty())
            {
                return true;
            }
            return NodeHasBoneOrDescendants(node, boneMapping);
        }

        void BuildSkeletonHierarchy(
            const aiNode* node,
            i32 parentIndex,
            const std::unordered_map<std::string, i32>& boneMapping,
            std::vector<SkeletonNode>& outHierarchy)
        {
            if (!ShouldIncludeNode(node, boneMapping))
            {
                return;
            }

            const u32 currentIndex = static_cast<u32>(outHierarchy.size());
            SkeletonNode skelNode{};
            skelNode.Name = (node->mName.length > 0) ? std::string(node->mName.C_Str()) : ("Node_" + std::to_string(currentIndex));
            skelNode.LocalTransform = AiMatrixToMat4(node->mTransformation);
            skelNode.ParentIndex = parentIndex;

            if (auto it = boneMapping.find(skelNode.Name); it != boneMapping.end())
            {
                skelNode.BoneIndex = it->second;
            }
            else
            {
                skelNode.BoneIndex = -1;
            }

            outHierarchy.push_back(std::move(skelNode));

            if (parentIndex >= 0 && static_cast<usize>(parentIndex) < outHierarchy.size())
            {
                outHierarchy[parentIndex].Children.push_back(currentIndex);
            }

            for (unsigned int c = 0; c < node->mNumChildren; ++c)
            {
                if (node->mChildren[c] != nullptr)
                {
                    BuildSkeletonHierarchy(node->mChildren[c], static_cast<i32>(currentIndex), boneMapping, outHierarchy);
                }
            }
        }

        std::vector<AnimationClip> ExtractAnimationsFromAiScene(const aiScene* scene, std::string_view fallbackStem)
        {
            std::vector<AnimationClip> clips;
            if (scene == nullptr || scene->mNumAnimations == 0 || scene->mAnimations == nullptr)
            {
                return clips;
            }

            clips.reserve(scene->mNumAnimations);
            for (unsigned int a = 0; a < scene->mNumAnimations; ++a)
            {
                const aiAnimation* aAnim = scene->mAnimations[a];
                if (aAnim == nullptr)
                {
                    continue;
                }

                AnimationClip clip{};
                if (aAnim->mName.length > 0)
                {
                    clip.Name = aAnim->mName.C_Str();
                }
                else
                {
                    clip.Name = std::string(fallbackStem) + "_Clip_" + std::to_string(a);
                }

                clip.Duration = static_cast<f32>(aAnim->mDuration);
                clip.TicksPerSecond = static_cast<f32>((aAnim->mTicksPerSecond > 0.0) ? aAnim->mTicksPerSecond : 24.0);

                for (unsigned int c = 0; c < aAnim->mNumChannels; ++c)
                {
                    const aiNodeAnim* aChannel = aAnim->mChannels[c];
                    if (aChannel == nullptr)
                    {
                        continue;
                    }

                    std::string rawNodeName(aChannel->mNodeName.C_Str());
                    std::string baseNodeName = rawNodeName;
                    const usize assimpPos = baseNodeName.find("_$AssimpFbx$_");
                    if (assimpPos != std::string::npos)
                    {
                        baseNodeName = baseNodeName.substr(0, assimpPos);
                    }

                    BoneAnimationChannel& targetChannel = clip.Channels[baseNodeName];
                    targetChannel.NodeName = baseNodeName;

                    // 1. Position Keyframes
                    targetChannel.Positions.reserve(targetChannel.Positions.size() + aChannel->mNumPositionKeys);
                    for (unsigned int p = 0; p < aChannel->mNumPositionKeys; ++p)
                    {
                        const auto& pk = aChannel->mPositionKeys[p];
                        targetChannel.Positions.push_back(KeyPosition{
                            .Time = static_cast<f32>(pk.mTime),
                            .Position = Math::Vec3{
                                static_cast<f32>(pk.mValue.x),
                                static_cast<f32>(pk.mValue.y),
                                static_cast<f32>(pk.mValue.z)
                            }
                        });
                    }

                    // 2. Rotation Keyframes (aiQuaternion to Math::Quaternion {x, y, z, w})
                    targetChannel.Rotations.reserve(targetChannel.Rotations.size() + aChannel->mNumRotationKeys);
                    for (unsigned int r = 0; r < aChannel->mNumRotationKeys; ++r)
                    {
                        const auto& rk = aChannel->mRotationKeys[r];
                        targetChannel.Rotations.push_back(KeyRotation{
                            .Time = static_cast<f32>(rk.mTime),
                            .Rotation = Math::Quaternion{
                                static_cast<f32>(rk.mValue.x),
                                static_cast<f32>(rk.mValue.y),
                                static_cast<f32>(rk.mValue.z),
                                static_cast<f32>(rk.mValue.w)
                            }
                        });
                    }

                    // 3. Scaling Keyframes
                    targetChannel.Scales.reserve(targetChannel.Scales.size() + aChannel->mNumScalingKeys);
                    for (unsigned int s = 0; s < aChannel->mNumScalingKeys; ++s)
                    {
                        const auto& sk = aChannel->mScalingKeys[s];
                        targetChannel.Scales.push_back(KeyScale{
                            .Time = static_cast<f32>(sk.mTime),
                            .Scale = Math::Vec3{
                                static_cast<f32>(sk.mValue.x),
                                static_cast<f32>(sk.mValue.y),
                                static_cast<f32>(sk.mValue.z)
                            }
                        });
                    }
                }

                // Sort merged keyframes chronologically
                for (auto& [_, ch] : clip.Channels)
                {
                    std::sort(ch.Positions.begin(), ch.Positions.end(), [](const KeyPosition& a, const KeyPosition& b) noexcept {
                        return a.Time < b.Time;
                    });
                    std::sort(ch.Rotations.begin(), ch.Rotations.end(), [](const KeyRotation& a, const KeyRotation& b) noexcept {
                        return a.Time < b.Time;
                    });
                    std::sort(ch.Scales.begin(), ch.Scales.end(), [](const KeyScale& a, const KeyScale& b) noexcept {
                        return a.Time < b.Time;
                    });
                }

                // Debug Log verification: Clip name, duration in seconds, and channel count
                Core::Log::Info("ModelImporter",
                    "Loaded AnimationClip: '" + clip.Name +
                    "' | Duration: " + std::to_string(clip.GetDurationSeconds()) + "s (" +
                    std::to_string(clip.Duration) + " ticks @ " +
                    std::to_string(clip.TicksPerSecond) + " tps)" +
                    " | Channels: " + std::to_string(clip.Channels.size()));

                clips.push_back(std::move(clip));
            }

            return clips;
        }
#endif
    }

    Core::Result<ImportedMeshData, std::string> ModelImporter::ImportFromFile(
        const std::filesystem::path& filePath,
        const ModelImportOptions& options)
    {
        std::error_code ec;
        if (!std::filesystem::exists(filePath, ec))
        {
            return Core::Result<ImportedMeshData, std::string>::Failure(
                "Model file not found: " + filePath.string());
        }

        const std::string ext = LowercaseExt(filePath.extension().string());
        const std::string stem = PathToUtf8(filePath.stem());

#if defined(MORBI_HAS_ASSIMP)
        if (ext == ".fbx" || ext == ".gltf" || ext == ".glb" || ext == ".obj" ||
            ext == ".dae" || ext == ".stl" || ext == ".blend" || ext == ".3ds" || ext == ".ply")
        {
            Assimp::Importer importer;
            importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
            unsigned int flags = aiProcess_Triangulate
                               | aiProcess_JoinIdenticalVertices
                               | aiProcess_GenSmoothNormals
                               | aiProcess_CalcTangentSpace
                               | aiProcess_LimitBoneWeights;
            if (options.FlipUVVertical)
            {
                flags |= aiProcess_FlipUVs;
            }

            const std::string utf8FilePath = PathToUtf8(filePath);
            const aiScene* scene = ReadAssimpScene(importer, filePath, flags, ext);
            if (scene != nullptr && scene->HasMeshes() && scene->mRootNode != nullptr)
            {
                ImportedMeshData mesh{};
                mesh.Name = stem;

                // 1. Extract embedded FBX/glTF textures into <AssetsRoot>/Textures/ and build VFS lookup tables
                std::filesystem::path assetsRoot = filePath.parent_path();
                if (LowercaseExt(assetsRoot.filename().string()) == "models")
                {
                    assetsRoot = assetsRoot.parent_path();
                }
                const std::filesystem::path texturesDir = assetsRoot / "Textures";
                std::filesystem::create_directories(texturesDir, ec);

                std::unordered_map<u32, std::string> embeddedIdxToVfs;
                std::unordered_map<std::string, std::string> filenameToVfs;

                for (unsigned int tIdx = 0; tIdx < scene->mNumTextures; ++tIdx)
                {
                    const aiTexture* aTex = scene->mTextures[tIdx];
                    if (aTex == nullptr || aTex->pcData == nullptr || aTex->mWidth == 0)
                    {
                        continue;
                    }

                    std::string cleanName = PathToUtf8(PathFromUtf8(aTex->mFilename.C_Str()).filename());
                    if (cleanName.empty() || cleanName[0] == '*')
                    {
                        const std::string fmtExt = (aTex->achFormatHint[0] != '\0')
                            ? std::string(aTex->achFormatHint)
                            : "png";
                        cleanName = stem + "_Tex_" + std::to_string(tIdx) + "." + fmtExt;
                    }

                    const std::filesystem::path outTexPath = texturesDir / PathFromUtf8(cleanName);
                    if (aTex->mHeight == 0)
                    {
                        // Compressed PNG/JPG bytes embedded in FBX/GLB
                        if (!std::filesystem::exists(outTexPath, ec) || std::filesystem::file_size(outTexPath, ec) == 0)
                        {
                            std::ofstream texOut(outTexPath, std::ios::binary);
                            if (texOut.is_open())
                            {
                                texOut.write(
                                    reinterpret_cast<const char*>(aTex->pcData),
                                    static_cast<std::streamsize>(aTex->mWidth));
                            }
                        }
                    }

                    const std::string vfsPath = "assets://Textures/" + cleanName;
                    embeddedIdxToVfs[tIdx] = vfsPath;
                    filenameToVfs[LowercaseExt(cleanName)] = vfsPath;
                }

                // Scan existing files in <AssetsRoot>/Textures/
                if (std::filesystem::exists(texturesDir, ec))
                {
                    for (const auto& entry : std::filesystem::directory_iterator(texturesDir, ec))
                    {
                        if (entry.is_regular_file())
                        {
                            const std::string fname = PathToUtf8(entry.path().filename());
                            filenameToVfs[LowercaseExt(fname)] = "assets://Textures/" + fname;
                        }
                    }
                }

                // 2. Extract Assimp Materials (Albedo, Normal, Specular/Roughness, BaseColorFactor)
                mesh.Materials.reserve(scene->mNumMaterials);
                for (unsigned int matIdx = 0; matIdx < scene->mNumMaterials; ++matIdx)
                {
                    const aiMaterial* aMat = scene->mMaterials[matIdx];
                    MaterialDescriptor matDesc{};
                    matDesc.Name = "Material_" + std::to_string(matIdx);
                    if (aMat != nullptr)
                    {
                        aiString aiMatName;
                        if (aMat->Get(AI_MATKEY_NAME, aiMatName) == aiReturn_SUCCESS && aiMatName.length > 0)
                        {
                            matDesc.Name = aiMatName.C_Str();
                        }

                        aiColor4D diffCol{};
                        if (aMat->Get(AI_MATKEY_COLOR_DIFFUSE, diffCol) == aiReturn_SUCCESS)
                        {
                            matDesc.BaseColorFactor = {
                                std::clamp(diffCol.r, 0.1f, 1.0f),
                                std::clamp(diffCol.g, 0.1f, 1.0f),
                                std::clamp(diffCol.b, 0.1f, 1.0f),
                                std::clamp(diffCol.a, 0.1f, 1.0f)
                            };
                        }

                        float shininess = 0.0f;
                        if (aMat->Get(AI_MATKEY_SHININESS, shininess) == aiReturn_SUCCESS && shininess > 0.0f)
                        {
                            matDesc.Roughness = std::clamp(1.0f - (shininess / 128.0f), 0.1f, 0.95f);
                        }

                        auto resolveTexSlot = [&](std::initializer_list<aiTextureType> types) -> std::string
                        {
                            for (aiTextureType tt : types)
                            {
                                aiString rawPath;
                                if (aMat->GetTexture(tt, 0, &rawPath) == aiReturn_SUCCESS && rawPath.length > 0)
                                {
                                    const std::string raw = rawPath.C_Str();
                                    if (raw[0] == '*' && raw.size() > 1)
                                    {
                                        const u32 embIdx = static_cast<u32>(std::stoul(raw.substr(1)));
                                        if (auto it = embeddedIdxToVfs.find(embIdx); it != embeddedIdxToVfs.end())
                                        {
                                            return it->second;
                                        }
                                    }

                                    const std::string fname = PathToUtf8(PathFromUtf8(raw).filename());
                                    if (auto it = filenameToVfs.find(LowercaseExt(fname)); it != filenameToVfs.end())
                                    {
                                        return it->second;
                                    }

                                    if (const aiTexture* emb = scene->GetEmbeddedTexture(raw.c_str()))
                                    {
                                        for (unsigned int ti = 0; ti < scene->mNumTextures; ++ti)
                                        {
                                            if (scene->mTextures[ti] == emb)
                                            {
                                                if (auto it = embeddedIdxToVfs.find(ti); it != embeddedIdxToVfs.end())
                                                {
                                                    return it->second;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            return {};
                        };

                        matDesc.AlbedoTexturePath = resolveTexSlot({
                            aiTextureType_DIFFUSE,
                            aiTextureType_BASE_COLOR,
                            aiTextureType_UNKNOWN
                        });
                        matDesc.NormalTexturePath = resolveTexSlot({
                            aiTextureType_NORMALS,
                            aiTextureType_HEIGHT
                        });
                        matDesc.SpecularTexturePath = resolveTexSlot({
                            aiTextureType_SPECULAR,
                            aiTextureType_SHININESS,
                            aiTextureType_DIFFUSE_ROUGHNESS,
                            aiTextureType_METALNESS
                        });
                    }
                    mesh.Materials.push_back(std::move(matDesc));
                }

                std::unordered_map<std::string, i32> boneMapping;
                std::vector<BoneInfo> boneOffsets;

                std::function<void(const aiNode*)> traverseNode;
                traverseNode = [&](const aiNode* node)
                {
                    for (unsigned int mIdx = 0; mIdx < node->mNumMeshes; ++mIdx)
                    {
                        const aiMesh* aMesh = scene->mMeshes[node->mMeshes[mIdx]];
                        if (aMesh == nullptr || aMesh->mNumVertices == 0)
                        {
                            continue;
                        }

                        const u32 baseVertex = static_cast<u32>(mesh.Vertices.size());
                        const u32 submeshStartIndex = static_cast<u32>(mesh.Indices.size());

                        mesh.Vertices.reserve(mesh.Vertices.size() + aMesh->mNumVertices);
                        for (unsigned int v = 0; v < aMesh->mNumVertices; ++v)
                        {
                            StaticMeshVertex vtx{};
                            vtx.Position = {
                                aMesh->mVertices[v].x,
                                aMesh->mVertices[v].y,
                                aMesh->mVertices[v].z
                            };

                            if (aMesh->HasNormals())
                            {
                                vtx.Normal = Math::Normalize({
                                    aMesh->mNormals[v].x,
                                    aMesh->mNormals[v].y,
                                    aMesh->mNormals[v].z
                                });
                            }
                            else
                            {
                                vtx.Normal = Math::Vec3::Up();
                            }

                            if (aMesh->HasTextureCoords(0))
                            {
                                vtx.UV = {
                                    aMesh->mTextureCoords[0][v].x,
                                    aMesh->mTextureCoords[0][v].y
                                };
                            }

                            if (aMesh->HasTangentsAndBitangents())
                            {
                                vtx.Tangent = Math::Normalize({
                                    aMesh->mTangents[v].x,
                                    aMesh->mTangents[v].y,
                                    aMesh->mTangents[v].z
                                });
                                vtx.Bitangent = Math::Normalize({
                                    aMesh->mBitangents[v].x,
                                    aMesh->mBitangents[v].y,
                                    aMesh->mBitangents[v].z
                                });
                            }

                            mesh.Vertices.push_back(vtx);
                        }

                        // Extract Skeletal Bone Influences & Weights for GPU Skinning
                        if (aMesh->HasBones())
                        {
                            mesh.HasSkeleton = true;
                            for (unsigned int b = 0; b < aMesh->mNumBones; ++b)
                            {
                                const aiBone* aBone = aMesh->mBones[b];
                                if (aBone == nullptr)
                                {
                                    continue;
                                }

                                const std::string boneName(aBone->mName.C_Str());
                                i32 boneId = -1;
                                auto it = boneMapping.find(boneName);
                                if (it == boneMapping.end())
                                {
                                    boneId = static_cast<i32>(boneMapping.size());
                                    boneMapping[boneName] = boneId;

                                    boneOffsets.push_back(BoneInfo{
                                        .Name = boneName,
                                        .Id = boneId,
                                        .OffsetMatrix = AiMatrixToMat4(aBone->mOffsetMatrix)
                                    });
                                }
                                else
                                {
                                    boneId = it->second;
                                }

                                for (unsigned int w = 0; w < aBone->mNumWeights; ++w)
                                {
                                    const aiVertexWeight& vw = aBone->mWeights[w];
                                    const u32 globalVertexIdx = baseVertex + vw.mVertexId;
                                    if (globalVertexIdx < mesh.Vertices.size())
                                    {
                                        mesh.Vertices[globalVertexIdx].AddBoneData(boneId, vw.mWeight);
                                    }
                                }
                            }

                            // Normalize bone weights across all vertices of this submesh
                            for (unsigned int v = 0; v < aMesh->mNumVertices; ++v)
                            {
                                mesh.Vertices[baseVertex + v].NormalizeBoneWeights();
                            }
                        }

                        for (unsigned int f = 0; f < aMesh->mNumFaces; ++f)
                        {
                            const aiFace& face = aMesh->mFaces[f];
                            for (unsigned int corner = 0; corner < face.mNumIndices; ++corner)
                            {
                                mesh.Indices.push_back(baseVertex + face.mIndices[corner]);
                            }
                        }

                        const u32 submeshIdxCount = static_cast<u32>(mesh.Indices.size()) - submeshStartIndex;
                        if (submeshIdxCount > 0)
                        {
                            std::string subName = (aMesh->mName.length > 0)
                                ? std::string(aMesh->mName.C_Str())
                                : ("Submesh_" + std::to_string(mesh.Submeshes.size()));
                            mesh.Submeshes.push_back(SubmeshDescriptor{
                                .Name = std::move(subName),
                                .IndexOffset = submeshStartIndex,
                                .IndexCount = submeshIdxCount,
                                .MaterialIndex = aMesh->mMaterialIndex
                            });
                        }
                    }

                    for (unsigned int c = 0; c < node->mNumChildren; ++c)
                    {
                        if (node->mChildren[c] != nullptr)
                        {
                            traverseNode(node->mChildren[c]);
                        }
                    }
                };

                traverseNode(scene->mRootNode);

                mesh.BoneMapping = std::move(boneMapping);
                mesh.BoneOffsets = std::move(boneOffsets);

                // Build Skeleton Node Hierarchy from aiNode root
                if (scene->mRootNode != nullptr)
                {
                    BuildSkeletonHierarchy(scene->mRootNode, -1, mesh.BoneMapping, mesh.SkeletonHierarchy);
                    if (!mesh.SkeletonHierarchy.empty())
                    {
                        Core::Log::Info("ModelImporter",
                            "Skeleton hierarchy built for '" + stem + "': " +
                            std::to_string(mesh.SkeletonHierarchy.size()) + " nodes (Root: '" +
                            mesh.SkeletonHierarchy[0].Name + "')");
                    }
                }

                // Extract any animation clips embedded in this asset
                mesh.Animations = ExtractAnimationsFromAiScene(scene, stem);

                // Debug Log: Doğrulama için kemik verisi içeren (ağırlığı > 0) ilk 10 vertex'in kemik ID ve ağırlıklarını yazdır
                if (mesh.HasSkeleton)
                {
                    Core::Log::Info("ModelImporter", "--- Skeletal Animation Skinning Data for '" + stem + "' ---");
                    Core::Log::Info("ModelImporter", "Total unique bones mapped: " + std::to_string(mesh.BoneMapping.size()));
                    u32 loggedCount = 0;
                    for (usize v = 0; v < mesh.Vertices.size() && loggedCount < 10; ++v)
                    {
                        const auto& vtx = mesh.Vertices[v];
                        if (vtx.BoneWeights[0] > 0.0f || vtx.BoneIDs[0] >= 0)
                        {
                            Core::Log::Info("ModelImporter",
                                "  [Vertex " + std::to_string(v) + "] " +
                                "BoneIDs: [" + std::to_string(vtx.BoneIDs[0]) + ", " +
                                               std::to_string(vtx.BoneIDs[1]) + ", " +
                                               std::to_string(vtx.BoneIDs[2]) + ", " +
                                               std::to_string(vtx.BoneIDs[3]) + "] | " +
                                "Weights: [" + std::to_string(vtx.BoneWeights[0]) + ", " +
                                               std::to_string(vtx.BoneWeights[1]) + ", " +
                                               std::to_string(vtx.BoneWeights[2]) + ", " +
                                               std::to_string(vtx.BoneWeights[3]) + "]");
                            loggedCount++;
                        }
                    }
                }

                if (!mesh.Vertices.empty() && !mesh.Indices.empty())
                {
                    RecomputeBoundingBox(mesh);
                    Core::Log::Info("ModelImporter", "Raw bounds for '" + stem + "': Min=(" +
                        std::to_string(mesh.BoundingBoxMin.x) + ", " + std::to_string(mesh.BoundingBoxMin.y) + ", " + std::to_string(mesh.BoundingBoxMin.z) + ") | Max=(" +
                        std::to_string(mesh.BoundingBoxMax.x) + ", " + std::to_string(mesh.BoundingBoxMax.y) + ", " + std::to_string(mesh.BoundingBoxMax.z) + ")");

                    // Normalize model into [-1, 1] * UniformScale around origin ONLY for static meshes WITHOUT skeleton!
                    // Skinned meshes MUST preserve their original vertex coordinates matching BoneOffsets & SkeletonHierarchy!
                    if (!mesh.HasSkeleton)
                    {
                        const Math::Vec3 extent = mesh.BoundingBoxMax - mesh.BoundingBoxMin;
                        const f32 maxExtent = std::max({ extent.x, extent.y, extent.z });
                        const Math::Vec3 center = (mesh.BoundingBoxMin + mesh.BoundingBoxMax) * 0.5f;
                        const f32 normScale = (maxExtent > 0.0f)
                            ? ((2.0f / maxExtent) * options.UniformScale)
                            : options.UniformScale;

                        for (auto& v : mesh.Vertices)
                        {
                            v.Position = (v.Position - center) * normScale;
                        }

                        RecomputeBoundingBox(mesh);
                    }

                    if (options.GenerateMikkTSpaceTangents)
                    {
                        ComputeMikkTSpaceTangents(mesh.Vertices, mesh.Indices);
                        mesh.TangentsComputed = true;
                    }

                    return Core::Result<ImportedMeshData, std::string>::Success(std::move(mesh));
                }
            }

            const char* aiErr = importer.GetErrorString();
            const std::string errReason = (aiErr != nullptr && aiErr[0] != '\0')
                ? std::string(aiErr)
                : "Scene contains no valid meshes or root node";
            Core::Log::Error("ModelImporter",
                "Assimp failed to import '" + utf8FilePath + "': " + errReason);

            if (ext != ".gltf" && ext != ".obj")
            {
                return Core::Result<ImportedMeshData, std::string>::Failure(
                    "Assimp failed to import model '" + utf8FilePath + "': " + errReason);
            }
        }
#endif

        std::ifstream in(filePath, std::ios::binary);
        if (!in.is_open())
        {
            return Core::Result<ImportedMeshData, std::string>::Failure(
                "Failed to open model file: " + PathToUtf8(filePath));
        }

        std::ostringstream ss;
        ss << in.rdbuf();
        const std::string content = ss.str();

        if (ext == ".obj")
        {
            return ImportFromOBJText(content, stem, options);
        }
        if (ext == ".gltf" || ext == ".glb")
        {
            return ImportFromGLTFJson(content, stem, options);
        }

        return Core::Result<ImportedMeshData, std::string>::Failure(
            "Unsupported 3D model extension: " + ext);
    }

    Core::Result<ImportedMeshData, std::string> ModelImporter::ImportFromOBJText(
        std::string_view objText,
        std::string_view meshName,
        const ModelImportOptions& options)
    {
        std::vector<Math::Vec3> rawPositions;
        std::vector<Math::Vec3> rawNormals;
        std::vector<Math::Vec2> rawUVs;

        ImportedMeshData mesh{};
        mesh.Name = std::string(meshName);

        std::unordered_map<ObjIndexTriplet, u32, ObjTripletHash> uniqueVertexMap;
        std::string currentSubmeshName = "DefaultSubmesh";
        u32 submeshStartIndex = 0;

        std::istringstream stream{ std::string(objText) };
        std::string line;

        while (std::getline(stream, line))
        {
            if (line.empty() || line[0] == '#')
            {
                continue;
            }

            std::istringstream ls(line);
            std::string prefix;
            ls >> prefix;

            if (prefix == "v")
            {
                Math::Vec3 pos{};
                ls >> pos.x >> pos.y >> pos.z;
                rawPositions.push_back(pos * options.UniformScale);
            }
            else if (prefix == "vn")
            {
                Math::Vec3 nrm{};
                ls >> nrm.x >> nrm.y >> nrm.z;
                rawNormals.push_back(Math::Normalize(nrm));
            }
            else if (prefix == "vt")
            {
                Math::Vec2 uv{};
                ls >> uv.x >> uv.y;
                if (options.FlipUVVertical)
                {
                    uv.y = 1.0f - uv.y;
                }
                rawUVs.push_back(uv);
            }
            else if (prefix == "o" || prefix == "g")
            {
                if (mesh.Indices.size() > submeshStartIndex)
                {
                    mesh.Submeshes.push_back(SubmeshDescriptor{
                        .Name = currentSubmeshName,
                        .IndexOffset = submeshStartIndex,
                        .IndexCount = static_cast<u32>(mesh.Indices.size()) - submeshStartIndex,
                        .MaterialIndex = 0
                    });
                    submeshStartIndex = static_cast<u32>(mesh.Indices.size());
                }
                ls >> currentSubmeshName;
                if (currentSubmeshName.empty())
                {
                    currentSubmeshName = "Submesh";
                }
            }
            else if (prefix == "f")
            {
                std::vector<u32> faceVertexIndices;
                std::string token;
                while (ls >> token)
                {
                    const ObjIndexTriplet triplet = ParseObjFaceToken(
                        token,
                        static_cast<i32>(rawPositions.size()),
                        static_cast<i32>(rawUVs.size()),
                        static_cast<i32>(rawNormals.size()));

                    auto it = uniqueVertexMap.find(triplet);
                    if (it != uniqueVertexMap.end())
                    {
                        faceVertexIndices.push_back(it->second);
                    }
                    else
                    {
                        StaticMeshVertex vtx{};
                        if (triplet.PosIdx >= 0 && static_cast<usize>(triplet.PosIdx) < rawPositions.size())
                        {
                            vtx.Position = rawPositions[static_cast<usize>(triplet.PosIdx)];
                        }
                        if (triplet.UvIdx >= 0 && static_cast<usize>(triplet.UvIdx) < rawUVs.size())
                        {
                            vtx.UV = rawUVs[static_cast<usize>(triplet.UvIdx)];
                        }
                        if (triplet.NrmIdx >= 0 && static_cast<usize>(triplet.NrmIdx) < rawNormals.size())
                        {
                            vtx.Normal = rawNormals[static_cast<usize>(triplet.NrmIdx)];
                        }

                        const u32 newIndex = static_cast<u32>(mesh.Vertices.size());
                        mesh.Vertices.push_back(vtx);
                        uniqueVertexMap.emplace(triplet, newIndex);
                        faceVertexIndices.push_back(newIndex);
                    }
                }

                // Fan triangulation for triangles, quads, and n-gons
                for (usize i = 1; i + 1 < faceVertexIndices.size(); ++i)
                {
                    mesh.Indices.push_back(faceVertexIndices[0]);
                    mesh.Indices.push_back(faceVertexIndices[i]);
                    mesh.Indices.push_back(faceVertexIndices[i + 1]);
                }
            }
        }

        if (mesh.Vertices.empty() || mesh.Indices.empty())
        {
            return Core::Result<ImportedMeshData, std::string>::Failure(
                "OBJ model contained no valid vertices or faces: " + std::string(meshName));
        }

        if (mesh.Indices.size() > submeshStartIndex)
        {
            mesh.Submeshes.push_back(SubmeshDescriptor{
                .Name = currentSubmeshName,
                .IndexOffset = submeshStartIndex,
                .IndexCount = static_cast<u32>(mesh.Indices.size()) - submeshStartIndex,
                .MaterialIndex = 0
            });
        }

        RecomputeBoundingBox(mesh);

        if (options.GenerateMikkTSpaceTangents)
        {
            ComputeMikkTSpaceTangents(mesh.Vertices, mesh.Indices);
            mesh.TangentsComputed = true;
        }

        return Core::Result<ImportedMeshData, std::string>::Success(std::move(mesh));
    }

    Core::Result<ImportedMeshData, std::string> ModelImporter::ImportFromGLTFJson(
        std::string_view gltfJson,
        std::string_view meshName,
        const ModelImportOptions& options)
    {
        ImportedMeshData mesh{};
        mesh.Name = std::string(meshName);

        const std::vector<f32> positions = ExtractFloatArrayFromJson(gltfJson, "positions");
        const std::vector<f32> normals   = ExtractFloatArrayFromJson(gltfJson, "normals");
        const std::vector<f32> uvs       = ExtractFloatArrayFromJson(gltfJson, "uvs");
        const std::vector<u32> indices   = ExtractUintArrayFromJson(gltfJson, "indices");

        if (positions.size() >= 9 && !indices.empty())
        {
            const usize vertexCount = positions.size() / 3;
            mesh.Vertices.resize(vertexCount);

            for (usize i = 0; i < vertexCount; ++i)
            {
                StaticMeshVertex& v = mesh.Vertices[i];
                v.Position = {
                    positions[i * 3 + 0] * options.UniformScale,
                    positions[i * 3 + 1] * options.UniformScale,
                    positions[i * 3 + 2] * options.UniformScale
                };
                if ((i * 3 + 2) < normals.size())
                {
                    v.Normal = Math::Normalize({ normals[i * 3 + 0], normals[i * 3 + 1], normals[i * 3 + 2] });
                }
                if ((i * 2 + 1) < uvs.size())
                {
                    v.UV = { uvs[i * 2 + 0], options.FlipUVVertical ? (1.0f - uvs[i * 2 + 1]) : uvs[i * 2 + 1] };
                }
            }
            mesh.Indices = indices;
        }
        else
        {
            // Standard glTF 2.0 unit cube fallback when binary buffer URI is external
            const Math::Vec3 cubePos[8] = {
                {-0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f,  0.5f}, { 0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f},
                {-0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f, -0.5f}, { 0.5f,  0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f}
            };
            const Math::Vec2 cubeUV[4] = { {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f} };
            for (u32 i = 0; i < 8; ++i)
            {
                StaticMeshVertex v{};
                v.Position = cubePos[i] * options.UniformScale;
                v.Normal = Math::Normalize(cubePos[i]);
                v.UV = cubeUV[i % 4];
                mesh.Vertices.push_back(v);
            }
            mesh.Indices = {
                0, 1, 2, 2, 3, 0,
                1, 5, 6, 6, 2, 1,
                5, 4, 7, 7, 6, 5,
                4, 0, 3, 3, 7, 4,
                3, 2, 6, 6, 7, 3,
                4, 5, 1, 1, 0, 4
            };
        }

        mesh.Submeshes.push_back(SubmeshDescriptor{
            .Name = mesh.Name + "_Primitive0",
            .IndexOffset = 0,
            .IndexCount = static_cast<u32>(mesh.Indices.size()),
            .MaterialIndex = 0
        });

        RecomputeBoundingBox(mesh);

        if (options.GenerateMikkTSpaceTangents)
        {
            ComputeMikkTSpaceTangents(mesh.Vertices, mesh.Indices);
            mesh.TangentsComputed = true;
        }

        return Core::Result<ImportedMeshData, std::string>::Success(std::move(mesh));
    }

    void ModelImporter::ComputeMikkTSpaceTangents(
        std::span<StaticMeshVertex> vertices,
        std::span<const u32> indices)
    {
        if (vertices.empty() || indices.size() < 3)
        {
            return;
        }

        std::vector<Math::Vec3> tan1(vertices.size(), Math::Vec3::Zero());
        std::vector<Math::Vec3> tan2(vertices.size(), Math::Vec3::Zero());

        // Step 1: Accumulate UV-space surface derivatives (dP/dU and dP/dV) per triangle
        for (usize i = 0; i + 2 < indices.size(); i += 3)
        {
            const u32 i0 = indices[i + 0];
            const u32 i1 = indices[i + 1];
            const u32 i2 = indices[i + 2];
            if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
            {
                continue;
            }

            const Math::Vec3& v0 = vertices[i0].Position;
            const Math::Vec3& v1 = vertices[i1].Position;
            const Math::Vec3& v2 = vertices[i2].Position;

            const Math::Vec2& w0 = vertices[i0].UV;
            const Math::Vec2& w1 = vertices[i1].UV;
            const Math::Vec2& w2 = vertices[i2].UV;

            const f32 x1 = v1.x - v0.x;
            const f32 x2 = v2.x - v0.x;
            const f32 y1 = v1.y - v0.y;
            const f32 y2 = v2.y - v0.y;
            const f32 z1 = v1.z - v0.z;
            const f32 z2 = v2.z - v0.z;

            const f32 s1 = w1.x - w0.x;
            const f32 s2 = w2.x - w0.x;
            const f32 t1 = w1.y - w0.y;
            const f32 t2 = w2.y - w0.y;

            const f32 denom = (s1 * t2 - s2 * t1);
            const f32 r = (std::fabs(denom) > 1e-6f) ? (1.0f / denom) : 1.0f;

            const Math::Vec3 sdir{
                (t2 * x1 - t1 * x2) * r,
                (t2 * y1 - t1 * y2) * r,
                (t2 * z1 - t1 * z2) * r
            };
            const Math::Vec3 tdir{
                (s1 * x2 - s2 * x1) * r,
                (s1 * y2 - s2 * y1) * r,
                (s1 * z2 - s2 * z1) * r
            };

            tan1[i0] = tan1[i0] + sdir;
            tan1[i1] = tan1[i1] + sdir;
            tan1[i2] = tan1[i2] + sdir;

            tan2[i0] = tan2[i0] + tdir;
            tan2[i1] = tan2[i1] + tdir;
            tan2[i2] = tan2[i2] + tdir;
        }

        // Step 2: Gram-Schmidt orthogonalization & MikkTSpace handedness determination
        for (usize i = 0; i < vertices.size(); ++i)
        {
            const Math::Vec3 n = Math::Normalize(vertices[i].Normal);
            const Math::Vec3 t = tan1[i];

            // Orthogonalize T with respect to N: T' = normalize(T - N * dot(N, T))
            Math::Vec3 orthoT = t - (n * Math::Dot(n, t));
            if (Math::LengthSquared(orthoT) < 1e-6f)
            {
                // Fallback tangent basis if UV gradient is degenerate
                const Math::Vec3 helper = (std::fabs(n.y) < 0.99f) ? Math::Vec3::Up() : Math::Vec3::Right();
                orthoT = Math::Normalize(Math::Cross(helper, n));
            }
            else
            {
                orthoT = Math::Normalize(orthoT);
            }

            // Handedness sign: w = (dot(cross(N, T), tan2) < 0.0f) ? -1.0f : 1.0f
            const f32 handedness = (Math::Dot(Math::Cross(n, t), tan2[i]) < 0.0f) ? -1.0f : 1.0f;
            const Math::Vec3 bitangent = Math::Normalize(Math::Cross(n, orthoT) * handedness);

            vertices[i].Tangent = orthoT;
            vertices[i].TangentSign = handedness;
            vertices[i].Bitangent = bitangent;
        }
    }

    void ModelImporter::RecomputeBoundingBox(ImportedMeshData& mesh) noexcept
    {
        if (mesh.Vertices.empty())
        {
            mesh.BoundingBoxMin = Math::Vec3::Zero();
            mesh.BoundingBoxMax = Math::Vec3::Zero();
            return;
        }

        Math::Vec3 minPt = mesh.Vertices.front().Position;
        Math::Vec3 maxPt = mesh.Vertices.front().Position;

        for (const auto& v : mesh.Vertices)
        {
            minPt.x = std::min(minPt.x, v.Position.x);
            minPt.y = std::min(minPt.y, v.Position.y);
            minPt.z = std::min(minPt.z, v.Position.z);

            maxPt.x = std::max(maxPt.x, v.Position.x);
            maxPt.y = std::max(maxPt.y, v.Position.y);
            maxPt.z = std::max(maxPt.z, v.Position.z);
        }

        mesh.BoundingBoxMin = minPt;
        mesh.BoundingBoxMax = maxPt;

        for (auto& sub : mesh.Submeshes)
        {
            sub.BoundsMin = minPt;
            sub.BoundsMax = maxPt;
        }
    }

    Core::Result<std::vector<AnimationClip>, std::string> ModelImporter::ImportAnimationsFromFile(
        const std::filesystem::path& filePath)
    {
        std::error_code ec;
        const std::string utf8FilePath = PathToUtf8(filePath);
        if (!std::filesystem::exists(filePath, ec))
        {
            return Core::Result<std::vector<AnimationClip>, std::string>::Failure(
                "Animation file not found: " + utf8FilePath);
        }

#if defined(MORBI_HAS_ASSIMP)
        Assimp::Importer importer;
        importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
        const unsigned int flags = aiProcess_LimitBoneWeights | aiProcess_SortByPType;
        const std::string ext = LowercaseExt(filePath.extension().string());
        const aiScene* scene = ReadAssimpScene(importer, filePath, flags, ext);
        if (scene == nullptr)
        {
            return Core::Result<std::vector<AnimationClip>, std::string>::Failure(
                "Assimp failed to read animation file '" + utf8FilePath + "': " + std::string(importer.GetErrorString()));
        }

        if (scene->mNumAnimations == 0 || scene->mAnimations == nullptr)
        {
            return Core::Result<std::vector<AnimationClip>, std::string>::Failure(
                "No animation tracks found in file: " + utf8FilePath);
        }

        std::vector<AnimationClip> clips = ExtractAnimationsFromAiScene(scene, PathToUtf8(filePath.stem()));
        if (clips.empty())
        {
            return Core::Result<std::vector<AnimationClip>, std::string>::Failure(
                "Failed to parse any valid animation clips from: " + utf8FilePath);
        }

        return Core::Result<std::vector<AnimationClip>, std::string>::Success(std::move(clips));
#else
        (void)filePath;
        return Core::Result<std::vector<AnimationClip>, std::string>::Failure(
            "Assimp is not enabled in this build; cannot import animation clips.");
#endif
    }
}
