#include "Morbi/Asset/AnimationEvaluator.hpp"
#include "Morbi/Core/Log.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>

namespace Morbi::Asset
{
    namespace
    {
        inline std::string CanonicalBoneName(std::string_view name)
        {
            const usize pos = name.find_last_of(":|/");
            std::string_view base = (pos != std::string_view::npos) ? name.substr(pos + 1) : name;
            const usize assimpPos = base.find("_$AssimpFbx$_");
            if (assimpPos != std::string_view::npos)
            {
                base = base.substr(0, assimpPos);
            }
            std::string result(base);
            std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) noexcept {
                return static_cast<char>(std::tolower(c));
            });
            return result;
        }

        inline std::string_view CC3ToMixamoAlias(std::string_view canonName) noexcept
        {
            if (canonName == "cc_base_hip") return "hips";
            if (canonName == "cc_base_waist") return "spine";
            if (canonName == "cc_base_spine01") return "spine1";
            if (canonName == "cc_base_spine02") return "spine2";
            if (canonName == "cc_base_necktwist01") return "neck";
            if (canonName == "cc_base_head") return "head";
            if (canonName == "cc_base_l_clavicle") return "leftshoulder";
            if (canonName == "cc_base_l_upperarm") return "leftarm";
            if (canonName == "cc_base_l_forearm") return "leftforearm";
            if (canonName == "cc_base_l_hand") return "lefthand";
            if (canonName == "cc_base_r_clavicle") return "rightshoulder";
            if (canonName == "cc_base_r_upperarm") return "rightarm";
            if (canonName == "cc_base_r_forearm") return "rightforearm";
            if (canonName == "cc_base_r_hand") return "righthand";
            if (canonName == "cc_base_l_thigh") return "leftupleg";
            if (canonName == "cc_base_l_calf") return "leftleg";
            if (canonName == "cc_base_l_foot") return "leftfoot";
            if (canonName == "cc_base_l_toebase") return "lefttoebase";
            if (canonName == "cc_base_r_thigh") return "rightupleg";
            if (canonName == "cc_base_r_calf") return "rightleg";
            if (canonName == "cc_base_r_foot") return "rightfoot";
            if (canonName == "cc_base_r_toebase") return "righttoebase";
            if (canonName == "cc_base_l_thumb1") return "lefthandthumb1";
            if (canonName == "cc_base_l_thumb2") return "lefthandthumb2";
            if (canonName == "cc_base_l_thumb3") return "lefthandthumb3";
            if (canonName == "cc_base_l_index1") return "lefthandindex1";
            if (canonName == "cc_base_l_index2") return "lefthandindex2";
            if (canonName == "cc_base_l_index3") return "lefthandindex3";
            if (canonName == "cc_base_l_mid1") return "lefthandmiddle1";
            if (canonName == "cc_base_l_mid2") return "lefthandmiddle2";
            if (canonName == "cc_base_l_mid3") return "lefthandmiddle3";
            if (canonName == "cc_base_l_ring1") return "lefthandring1";
            if (canonName == "cc_base_l_ring2") return "lefthandring2";
            if (canonName == "cc_base_l_ring3") return "lefthandring3";
            if (canonName == "cc_base_l_pinky1") return "lefthandpinky1";
            if (canonName == "cc_base_l_pinky2") return "lefthandpinky2";
            if (canonName == "cc_base_l_pinky3") return "lefthandpinky3";
            if (canonName == "cc_base_r_thumb1") return "righthandthumb1";
            if (canonName == "cc_base_r_thumb2") return "righthandthumb2";
            if (canonName == "cc_base_r_thumb3") return "righthandthumb3";
            if (canonName == "cc_base_r_index1") return "righthandindex1";
            if (canonName == "cc_base_r_index2") return "righthandindex2";
            if (canonName == "cc_base_r_index3") return "righthandindex3";
            if (canonName == "cc_base_r_mid1") return "righthandmiddle1";
            if (canonName == "cc_base_r_mid2") return "righthandmiddle2";
            if (canonName == "cc_base_r_mid3") return "righthandmiddle3";
            if (canonName == "cc_base_r_ring1") return "righthandring1";
            if (canonName == "cc_base_r_ring2") return "righthandring2";
            if (canonName == "cc_base_r_ring3") return "righthandring3";
            if (canonName == "cc_base_r_pinky1") return "righthandpinky1";
            if (canonName == "cc_base_r_pinky2") return "righthandpinky2";
            if (canonName == "cc_base_r_pinky3") return "righthandpinky3";
            return {};
        }

        inline Math::Mat4 TransposeRot3x3(const Math::Mat4& m) noexcept
        {
            Math::Mat4 r = Math::Mat4::Identity();
            for (int row = 0; row < 3; ++row)
            {
                for (int col = 0; col < 3; ++col)
                {
                    r.At(row, col) = m.At(col, row);
                }
            }
            return r;
        }
        template <typename TKey>
        inline usize FindKeyIndex(const std::vector<TKey>& keys, f32 time) noexcept
        {
            if (keys.empty() || time <= keys.front().Time)
            {
                return 0;
            }

            if (time >= keys.back().Time)
            {
                return keys.size() - 1;
            }

            auto it = std::upper_bound(
                keys.begin(),
                keys.end(),
                time,
                [](f32 t, const TKey& key) noexcept {
                    return t < key.Time;
                }
            );

            if (it == keys.begin())
            {
                return 0;
            }

            return static_cast<usize>(std::distance(keys.begin(), it) - 1);
        }

        inline Math::Vec3 ExtractScaleFromMat4(const Math::Mat4& m) noexcept
        {
            const f32 sx = Math::Length(Math::Vec3{ m.At(0, 0), m.At(0, 1), m.At(0, 2) });
            const f32 sy = Math::Length(Math::Vec3{ m.At(1, 0), m.At(1, 1), m.At(1, 2) });
            const f32 sz = Math::Length(Math::Vec3{ m.At(2, 0), m.At(2, 1), m.At(2, 2) });
            return {
                (sx > 1e-6f) ? sx : 1.0f,
                (sy > 1e-6f) ? sy : 1.0f,
                (sz > 1e-6f) ? sz : 1.0f
            };
        }

        inline Math::Quaternion QuatFromMat4(const Math::Mat4& rawMat) noexcept
        {
            const Math::Vec3 scale = ExtractScaleFromMat4(rawMat);
            Math::Mat4 m = rawMat;
            for (usize col = 0; col < 3; ++col)
            {
                m.At(0, col) /= scale.x;
                m.At(1, col) /= scale.y;
                m.At(2, col) /= scale.z;
            }

            // Extract rotation quaternion from orthonormalized row-major Mat4
            const f32 trace = m.At(0, 0) + m.At(1, 1) + m.At(2, 2);
            if (trace > 0.0f)
            {
                const f32 s = 0.5f / std::sqrt(trace + 1.0f);
                return Math::NormalizeQuat({
                    (m.At(1, 2) - m.At(2, 1)) * s,
                    (m.At(2, 0) - m.At(0, 2)) * s,
                    (m.At(0, 1) - m.At(1, 0)) * s,
                    0.25f / s
                });
            }
            else if (m.At(0, 0) > m.At(1, 1) && m.At(0, 0) > m.At(2, 2))
            {
                const f32 s = 2.0f * std::sqrt(1.0f + m.At(0, 0) - m.At(1, 1) - m.At(2, 2));
                return Math::NormalizeQuat({
                    0.25f * s,
                    (m.At(1, 0) + m.At(0, 1)) / s,
                    (m.At(2, 0) + m.At(0, 2)) / s,
                    (m.At(1, 2) - m.At(2, 1)) / s
                });
            }
            else if (m.At(1, 1) > m.At(2, 2))
            {
                const f32 s = 2.0f * std::sqrt(1.0f + m.At(1, 1) - m.At(0, 0) - m.At(2, 2));
                return Math::NormalizeQuat({
                    (m.At(1, 0) + m.At(0, 1)) / s,
                    0.25f * s,
                    (m.At(2, 1) + m.At(1, 2)) / s,
                    (m.At(2, 0) - m.At(0, 2)) / s
                });
            }
            else
            {
                const f32 s = 2.0f * std::sqrt(1.0f + m.At(2, 2) - m.At(0, 0) - m.At(1, 1));
                return Math::NormalizeQuat({
                    (m.At(2, 0) + m.At(0, 2)) / s,
                    (m.At(2, 1) + m.At(1, 2)) / s,
                    0.25f * s,
                    (m.At(0, 1) - m.At(1, 0)) / s
                });
            }
        }
    }

    void AnimationEvaluator::Initialize(
        std::vector<SkeletonNode> skeletonHierarchy,
        std::vector<BoneInfo> boneOffsets,
        const AnimationClip* initialClip)
    {
        m_SkeletonHierarchy = std::move(skeletonHierarchy);
        m_BoneOffsets = std::move(boneOffsets);
        m_CurrentClip = initialClip;
        m_CurrentTime = 0.0f;

        m_NodeNameToIndex.clear();
        m_CanonicalNodeNameToIndex.clear();
        m_NodeNameToIndex.reserve(m_SkeletonHierarchy.size());
        m_CanonicalNodeNameToIndex.reserve(m_SkeletonHierarchy.size());
        m_IsRootBone.assign(m_SkeletonHierarchy.size(), 0);
        bool hasHipsBone = false;
        for (usize i = 0; i < m_SkeletonHierarchy.size(); ++i)
        {
            const std::string canon = CanonicalBoneName(m_SkeletonHierarchy[i].Name);
            m_NodeNameToIndex[m_SkeletonHierarchy[i].Name] = static_cast<u32>(i);
            m_CanonicalNodeNameToIndex[canon] = static_cast<u32>(i);
            if (canon == "hips")
            {
                m_IsRootBone[i] = 1;
                hasHipsBone = true;
            }
        }

        if (!hasHipsBone)
        {
            for (usize i = 0; i < m_SkeletonHierarchy.size(); ++i)
            {
                if (m_SkeletonHierarchy[i].BoneIndex >= 0)
                {
                    bool hasBoneAncestor = false;
                    i32 parentIdx = m_SkeletonHierarchy[i].ParentIndex;
                    usize guard = 0;
                    while (parentIdx >= 0 && static_cast<usize>(parentIdx) < m_SkeletonHierarchy.size() && guard < m_SkeletonHierarchy.size())
                    {
                        const auto& parentNode = m_SkeletonHierarchy[static_cast<usize>(parentIdx)];
                        if (parentNode.BoneIndex >= 0)
                        {
                            hasBoneAncestor = true;
                            break;
                        }
                        parentIdx = parentNode.ParentIndex;
                        ++guard;
                    }
                    if (!hasBoneAncestor)
                    {
                        m_IsRootBone[i] = 1;
                    }
                }
            }
        }

        m_GlobalTransforms.assign(m_SkeletonHierarchy.size(), Math::Mat4::Identity());
        m_FinalBoneMatrices.assign(m_BoneOffsets.size(), Math::Mat4::Identity());

        const usize nodeCount = m_SkeletonHierarchy.size();
        m_LastLocalPos.assign(nodeCount, Math::Vec3::Zero());
        m_LastLocalRot.assign(nodeCount, Math::Quaternion::Identity());
        m_LastLocalScale.assign(nodeCount, Math::Vec3::One());
        m_SnapshotLocalPos.assign(nodeCount, Math::Vec3::Zero());
        m_SnapshotLocalRot.assign(nodeCount, Math::Quaternion::Identity());
        m_SnapshotLocalScale.assign(nodeCount, Math::Vec3::One());

        m_PrevClip = nullptr;
        m_PrevNodeChannels.clear();
        m_PrevTime = 0.0f;
        m_BlendActive = false;
        m_BlendFromSnapshot = false;
        m_BlendElapsed = 0.0f;
        m_BlendDuration = 0.0f;

        BindClipChannels(m_CurrentClip, m_NodeChannels, true);
        CalculateTransforms();
    }

    void AnimationEvaluator::Initialize(
        const ImportedMeshData& meshData,
        const AnimationClip* initialClip)
    {
        Initialize(meshData.SkeletonHierarchy, meshData.BoneOffsets, initialClip);
    }

    namespace
    {
        Math::Mat4 ComputeNodeBindGlobalRot(
            const std::vector<SkeletonNode>& nodes,
            u32 idx,
            std::vector<Math::Mat4>& cache,
            std::vector<u8>& visited)
        {
            if (idx >= nodes.size()) return Math::Mat4::Identity();
            if (visited[idx]) return cache[idx];
            visited[idx] = 1;
            const auto& n = nodes[idx];
            const Math::Mat4 localRot = Math::RotationMat4(QuatFromMat4(n.LocalTransform));
            if (n.ParentIndex >= 0 && static_cast<usize>(n.ParentIndex) < nodes.size())
            {
                const Math::Mat4 parentRot = ComputeNodeBindGlobalRot(
                    nodes, static_cast<u32>(n.ParentIndex), cache, visited);
                cache[idx] = Math::MultiplyMat4(localRot, parentRot);
            }
            else
            {
                cache[idx] = localRot;
            }
            return cache[idx];
        }
    }

    void AnimationEvaluator::SetRetargetSourceSkeleton(const ImportedMeshData& sourceMesh)
    {
        m_SourceNodes = sourceMesh.SkeletonHierarchy;
        if (m_SourceNodes.empty() || m_SkeletonHierarchy.empty())
        {
            m_HasRetargetSource = false;
            return;
        }

        m_SourceBindGlobalRot.assign(m_SourceNodes.size(), Math::Mat4::Identity());
        std::vector<u8> srcVisited(m_SourceNodes.size(), 0);
        std::unordered_map<std::string, u32> srcCanonToIdx;
        srcCanonToIdx.reserve(m_SourceNodes.size());
        for (usize i = 0; i < m_SourceNodes.size(); ++i)
        {
            (void)ComputeNodeBindGlobalRot(m_SourceNodes, static_cast<u32>(i), m_SourceBindGlobalRot, srcVisited);
            srcCanonToIdx.try_emplace(CanonicalBoneName(m_SourceNodes[i].Name), static_cast<u32>(i));
        }

        m_TargetBindGlobalRot.assign(m_SkeletonHierarchy.size(), Math::Mat4::Identity());
        std::vector<u8> dstVisited(m_SkeletonHierarchy.size(), 0);
        m_TargetToSourceNode.assign(m_SkeletonHierarchy.size(), -1);
        u32 mappedCount = 0;
        for (usize i = 0; i < m_SkeletonHierarchy.size(); ++i)
        {
            (void)ComputeNodeBindGlobalRot(m_SkeletonHierarchy, static_cast<u32>(i), m_TargetBindGlobalRot, dstVisited);
            const std::string canonTarget = CanonicalBoneName(m_SkeletonHierarchy[i].Name);
            const std::string_view alias = CC3ToMixamoAlias(canonTarget);
            if (!alias.empty())
            {
                if (auto it = srcCanonToIdx.find(std::string(alias)); it != srcCanonToIdx.end())
                {
                    m_TargetToSourceNode[i] = static_cast<i32>(it->second);
                    ++mappedCount;
                }
            }
        }

        // Both Mixamo Remy and CC3+ Amber global bind transforms (from RootNode) are already in Y-up skeleton space
        m_RetargetZUpToYUp = Math::Mat4::Identity();
        m_HasRetargetSource = (mappedCount > 0);
        if (m_HasRetargetSource)
        {
            Core::Log::Info("Animation", "Configured Mixamo->CC3+ retargeting for " + std::to_string(mappedCount) + " bones.");
        }
    }

    void AnimationEvaluator::ComputeRetargetedPose(
        const AnimationClip* clip,
        f32 timeTicks,
        std::vector<Math::Quaternion>& outLocalRots) const
    {
        const usize dstCount = m_SkeletonHierarchy.size();
        outLocalRots.assign(dstCount, Math::Quaternion::Identity());
        if (clip == nullptr || !m_HasRetargetSource || m_SourceNodes.empty())
        {
            return;
        }

        std::unordered_map<std::string, const BoneAnimationChannel*> chanByCanon;
        chanByCanon.reserve(clip->Channels.size());
        for (const auto& [name, ch] : clip->Channels)
        {
            chanByCanon.try_emplace(CanonicalBoneName(name), &ch);
        }

        const usize srcCount = m_SourceNodes.size();
        std::vector<Math::Mat4> srcAnimGlobal(srcCount, Math::Mat4::Identity());
        std::vector<u8> srcVisited(srcCount, 0);
        auto evalSrcGlobal = [&](auto& self, u32 idx) -> Math::Mat4 {
            if (idx >= srcCount) return Math::Mat4::Identity();
            if (srcVisited[idx]) return srcAnimGlobal[idx];
            srcVisited[idx] = 1;
            const auto& sn = m_SourceNodes[idx];
            Math::Quaternion localRot = QuatFromMat4(sn.LocalTransform);
            if (auto it = chanByCanon.find(CanonicalBoneName(sn.Name)); it != chanByCanon.end() && !it->second->Rotations.empty())
            {
                localRot = InterpolateRotation(*it->second, timeTicks, localRot);
            }
            const Math::Mat4 localMat = Math::RotationMat4(localRot);
            if (sn.ParentIndex >= 0 && static_cast<usize>(sn.ParentIndex) < srcCount)
            {
                srcAnimGlobal[idx] = Math::MultiplyMat4(localMat, self(self, static_cast<u32>(sn.ParentIndex)));
            }
            else
            {
                srcAnimGlobal[idx] = localMat;
            }
            return srcAnimGlobal[idx];
        };
        for (usize i = 0; i < srcCount; ++i)
        {
            (void)evalSrcGlobal(evalSrcGlobal, static_cast<u32>(i));
        }

        const Math::Mat4 invZToY = TransposeRot3x3(m_RetargetZUpToYUp);
        std::vector<Math::Mat4> dstAnimGlobal(dstCount, Math::Mat4::Identity());
        std::vector<u8> dstVisited(dstCount, 0);
        auto evalDstGlobal = [&](auto& self, u32 idx) -> Math::Mat4 {
            if (idx >= dstCount) return Math::Mat4::Identity();
            if (dstVisited[idx]) return dstAnimGlobal[idx];
            dstVisited[idx] = 1;
            const auto& dn = m_SkeletonHierarchy[idx];
            const i32 mappedSrc = (idx < m_TargetToSourceNode.size()) ? m_TargetToSourceNode[idx] : -1;
            if (mappedSrc >= 0 && static_cast<usize>(mappedSrc) < srcCount)
            {
                const Math::Mat4 deltaWorld = Math::MultiplyMat4(
                    TransposeRot3x3(m_SourceBindGlobalRot[static_cast<usize>(mappedSrc)]),
                    srcAnimGlobal[static_cast<usize>(mappedSrc)]);
                const Math::Mat4 deltaInTargetSpace = Math::MultiplyMat4(
                    Math::MultiplyMat4(m_RetargetZUpToYUp, deltaWorld),
                    invZToY);
                dstAnimGlobal[idx] = Math::MultiplyMat4(m_TargetBindGlobalRot[idx], deltaInTargetSpace);
            }
            else
            {
                const Math::Mat4 localMat = Math::RotationMat4(QuatFromMat4(dn.LocalTransform));
                if (dn.ParentIndex >= 0 && static_cast<usize>(dn.ParentIndex) < dstCount)
                {
                    dstAnimGlobal[idx] = Math::MultiplyMat4(localMat, self(self, static_cast<u32>(dn.ParentIndex)));
                }
                else
                {
                    dstAnimGlobal[idx] = localMat;
                }
            }
            return dstAnimGlobal[idx];
        };

        for (usize i = 0; i < dstCount; ++i)
        {
            const Math::Mat4 globalMat = evalDstGlobal(evalDstGlobal, static_cast<u32>(i));
            const i32 pIdx = m_SkeletonHierarchy[i].ParentIndex;
            if (pIdx >= 0 && static_cast<usize>(pIdx) < dstCount)
            {
                const Math::Mat4 parentGlobal = evalDstGlobal(evalDstGlobal, static_cast<u32>(pIdx));
                outLocalRots[i] = QuatFromMat4(Math::MultiplyMat4(globalMat, TransposeRot3x3(parentGlobal)));
            }
            else
            {
                outLocalRots[i] = QuatFromMat4(globalMat);
            }
        }
    }

    void AnimationEvaluator::FillGPUBuffer(PerObjectBuffer& outBuffer) const noexcept
    {
        for (auto& mat : outBuffer.BoneMatrices)
        {
            mat = Math::Mat4::Identity();
        }

        const usize bonesToCopy = std::min(m_FinalBoneMatrices.size(), static_cast<usize>(100));
        for (usize i = 0; i < bonesToCopy; ++i)
        {
            outBuffer.BoneMatrices[i] = m_FinalBoneMatrices[i];
        }
    }

    void AnimationEvaluator::BindClipChannels(
        const AnimationClip* clip,
        std::vector<const BoneAnimationChannel*>& outChannels,
        bool logSummary) const noexcept
    {
        outChannels.assign(m_SkeletonHierarchy.size(), nullptr);
        if (clip == nullptr)
        {
            return;
        }

        std::unordered_map<std::string, const BoneAnimationChannel*> canonicalMap;
        canonicalMap.reserve(clip->Channels.size());
        for (const auto& [chanName, chan] : clip->Channels)
        {
            const std::string canon = CanonicalBoneName(chanName);
            auto [it, inserted] = canonicalMap.try_emplace(canon, &chan);
            if (!inserted)
            {
                const usize curKeys = chan.Positions.size() + chan.Rotations.size() + chan.Scales.size();
                const usize prevKeys = it->second->Positions.size() + it->second->Rotations.size() + it->second->Scales.size();
                if (curKeys > prevKeys)
                {
                    it->second = &chan;
                }
            }
        }

        u32 matchedCount = 0;
        std::unordered_set<std::string> matchedChannels;
        for (usize i = 0; i < m_SkeletonHierarchy.size(); ++i)
        {
            const auto& nodeName = m_SkeletonHierarchy[i].Name;
            // 1. Direct match
            auto it = clip->Channels.find(nodeName);
            if (it != clip->Channels.end())
            {
                outChannels[i] = &it->second;
                if (logSummary) matchedChannels.insert(it->first);
                ++matchedCount;
                continue;
            }

            // 2. Canonical / prefix-agnostic match (e.g. mixamorig:Hips vs mixamorig9:Hips)
            const std::string canonNode = CanonicalBoneName(nodeName);
            auto canonIt = canonicalMap.find(canonNode);
            if (canonIt != canonicalMap.end())
            {
                outChannels[i] = canonIt->second;
                if (logSummary) matchedChannels.insert(canonIt->second->NodeName);
                ++matchedCount;
                continue;
            }

            // 3. CC3+ <-> Mixamo humanoid bone alias match
            const std::string_view alias = CC3ToMixamoAlias(canonNode);
            if (!alias.empty())
            {
                auto aliasIt = canonicalMap.find(std::string(alias));
                if (aliasIt != canonicalMap.end())
                {
                    outChannels[i] = aliasIt->second;
                    if (logSummary) matchedChannels.insert(aliasIt->second->NodeName);
                    ++matchedCount;
                }
            }
        }

        if (logSummary)
        {
            std::string unmatchedList;
            for (const auto& [chanName, chan] : clip->Channels)
            {
                if (matchedChannels.find(chanName) == matchedChannels.end() &&
                    matchedChannels.find(chan.NodeName) == matchedChannels.end())
                {
                    if (!unmatchedList.empty()) unmatchedList += ", ";
                    unmatchedList += chanName;
                }
            }

            Core::Log::Info("Animation", "RebindClipChannels: bound " + std::to_string(matchedCount) +
                " / " + std::to_string(m_SkeletonHierarchy.size()) + " skeleton nodes to clip '" +
                clip->Name + "' (" + std::to_string(clip->Channels.size()) + " channels). Unmatched: [" +
                unmatchedList + "]");
        }
    }

    void AnimationEvaluator::SetAnimation(const AnimationClip* clip) noexcept
    {
        m_BlendActive = false;
        m_BlendFromSnapshot = false;
        m_BlendElapsed = 0.0f;
        m_BlendDuration = 0.0f;
        m_PrevClip = nullptr;
        m_PrevNodeChannels.clear();

        m_CurrentClip = clip;
        m_CurrentTime = 0.0f;
        BindClipChannels(m_CurrentClip, m_NodeChannels, true);
        CalculateTransforms();
    }

    void AnimationEvaluator::CrossFade(const AnimationClip* clip, f32 blendDurationSeconds) noexcept
    {
        if (clip == m_CurrentClip && m_CurrentClip != nullptr)
        {
            return;
        }

        if (blendDurationSeconds <= 0.0001f || m_CurrentClip == nullptr || m_SkeletonHierarchy.empty())
        {
            SetAnimation(clip);
            return;
        }

        if (m_BlendActive && m_LastLocalPos.size() == m_SkeletonHierarchy.size())
        {
            // Interrupted mid-blend: freeze current blended local pose so rapid transitions never pop
            m_SnapshotLocalPos = m_LastLocalPos;
            m_SnapshotLocalRot = m_LastLocalRot;
            m_SnapshotLocalScale = m_LastLocalScale;
            m_BlendFromSnapshot = true;
            m_PrevClip = nullptr;
            m_PrevNodeChannels.clear();
        }
        else
        {
            m_PrevClip = m_CurrentClip;
            m_PrevTime = m_CurrentTime;
            m_PrevLoop = m_Loop;
            m_PrevPlaybackSpeed = m_PlaybackSpeed;
            std::swap(m_NodeChannels, m_PrevNodeChannels);
            m_BlendFromSnapshot = false;
        }

        m_CurrentClip = clip;
        m_CurrentTime = 0.0f;
        m_BlendActive = true;
        m_BlendElapsed = 0.0f;
        m_BlendDuration = blendDurationSeconds;

        BindClipChannels(m_CurrentClip, m_NodeChannels, true);
        CalculateTransforms();
    }

    f32 AnimationEvaluator::GetBlendWeight() const noexcept
    {
        if (!m_BlendActive || m_BlendDuration <= 0.0001f)
        {
            return 1.0f;
        }
        const f32 t = std::clamp(m_BlendElapsed / m_BlendDuration, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    void AnimationEvaluator::Stop() noexcept
    {
        m_IsPlaying = false;
        m_CurrentTime = 0.0f;
        CalculateTransforms();
    }

    void AnimationEvaluator::SetCurrentTimeTicks(f32 ticks)
    {
        m_CurrentTime = ticks;
        if (m_CurrentClip != nullptr && m_CurrentClip->Duration > 0.0f)
        {
            if (m_Loop)
            {
                m_CurrentTime = std::fmod(m_CurrentTime, m_CurrentClip->Duration);
                if (m_CurrentTime < 0.0f)
                {
                    m_CurrentTime += m_CurrentClip->Duration;
                }
            }
            else
            {
                m_CurrentTime = std::clamp(m_CurrentTime, 0.0f, m_CurrentClip->Duration);
            }
        }
        CalculateTransforms();
    }

    void AnimationEvaluator::SetCurrentTimeSeconds(f32 seconds)
    {
        const f32 tps = (m_CurrentClip && m_CurrentClip->TicksPerSecond > 0.0f)
            ? m_CurrentClip->TicksPerSecond
            : 24.0f;
        SetCurrentTimeTicks(seconds * tps);
    }

    f32 AnimationEvaluator::GetCurrentTimeSeconds() const noexcept
    {
        if (m_CurrentClip && m_CurrentClip->TicksPerSecond > 0.0f)
        {
            return m_CurrentTime / m_CurrentClip->TicksPerSecond;
        }
        return 0.0f;
    }

    namespace
    {
        void AdvanceClipTime(const AnimationClip* clip, f32& timeTicks, f32 deltaTime, f32 playbackSpeed, bool loop) noexcept
        {
            if (clip == nullptr)
            {
                return;
            }

            const f32 tps = (clip->TicksPerSecond > 0.0f) ? clip->TicksPerSecond : 24.0f;
            timeTicks += deltaTime * tps * playbackSpeed;

            const f32 duration = clip->Duration;
            if (duration > 0.0f)
            {
                if (loop)
                {
                    timeTicks = std::fmod(timeTicks, duration);
                    if (timeTicks < 0.0f)
                    {
                        timeTicks += duration;
                    }
                }
                else
                {
                    timeTicks = std::clamp(timeTicks, 0.0f, duration);
                }
            }
        }
    }

    void AnimationEvaluator::UpdateAnimation(f32 deltaTime)
    {
        if (m_CurrentClip == nullptr || !m_IsPlaying)
        {
            return;
        }

        AdvanceClipTime(m_CurrentClip, m_CurrentTime, deltaTime, m_PlaybackSpeed, m_Loop);

        if (m_BlendActive)
        {
            m_BlendElapsed += deltaTime;
            if (!m_BlendFromSnapshot && m_PrevClip != nullptr)
            {
                AdvanceClipTime(m_PrevClip, m_PrevTime, deltaTime, m_PrevPlaybackSpeed, m_PrevLoop);
            }

            if (m_BlendElapsed >= m_BlendDuration)
            {
                m_BlendActive = false;
                m_BlendFromSnapshot = false;
                m_PrevClip = nullptr;
                m_PrevNodeChannels.clear();
            }
        }

        CalculateTransforms();
    }

    void AnimationEvaluator::CalculateTransforms()
    {
        if (m_SkeletonHierarchy.empty())
        {
            return;
        }

        const usize nodeCount = m_SkeletonHierarchy.size();
        if (m_GlobalTransforms.size() != nodeCount)
        {
            m_GlobalTransforms.assign(nodeCount, Math::Mat4::Identity());
        }

        if (m_FinalBoneMatrices.size() != m_BoneOffsets.size())
        {
            m_FinalBoneMatrices.assign(m_BoneOffsets.size(), Math::Mat4::Identity());
        }

        if (m_LastLocalPos.size() != nodeCount)
        {
            m_LastLocalPos.assign(nodeCount, Math::Vec3::Zero());
            m_LastLocalRot.assign(nodeCount, Math::Quaternion::Identity());
            m_LastLocalScale.assign(nodeCount, Math::Vec3::One());
        }

        // Precompute retargeted local rotations if applying a Mixamo clip onto a CC3+ rig
        m_RetargetPoseActive = false;
        m_PrevRetargetPoseActive = false;
        if (m_HasRetargetSource && m_CurrentClip != nullptr && !m_CurrentClip->Channels.contains("CC_Base_Hip"))
        {
            ComputeRetargetedPose(m_CurrentClip, m_CurrentTime, m_RetargetLocalRots);
            m_RetargetPoseActive = (m_RetargetLocalRots.size() == nodeCount);
        }
        if (m_HasRetargetSource && m_BlendActive && !m_BlendFromSnapshot && m_PrevClip != nullptr && !m_PrevClip->Channels.contains("CC_Base_Hip"))
        {
            ComputeRetargetedPose(m_PrevClip, m_PrevTime, m_PrevRetargetLocalRots);
            m_PrevRetargetPoseActive = (m_PrevRetargetLocalRots.size() == nodeCount);
        }

        // Evaluate from all root nodes down the skeleton hierarchy
        for (usize i = 0; i < nodeCount; ++i)
        {
            if (m_SkeletonHierarchy[i].ParentIndex == -1)
            {
                EvaluateNode(static_cast<u32>(i), Math::Mat4::Identity());
            }
        }
    }

    void AnimationEvaluator::SampleNodeLocalTRS(
        u32 nodeIndex,
        const BoneAnimationChannel* channel,
        f32 timeTicks,
        Math::Vec3& outPos,
        Math::Quaternion& outRot,
        Math::Vec3& outScale) const noexcept
    {
        const auto& node = m_SkeletonHierarchy[nodeIndex];
        const Math::Vec3 defaultPos{ node.LocalTransform.At(3, 0), node.LocalTransform.At(3, 1), node.LocalTransform.At(3, 2) };
        const Math::Quaternion defaultRot = QuatFromMat4(node.LocalTransform);
        const Math::Vec3 defaultScale = ExtractScaleFromMat4(node.LocalTransform);

        if (channel == nullptr)
        {
            outPos = defaultPos;
            outRot = defaultRot;
            outScale = defaultScale;
            return;
        }

        const bool isSkinnedSkeleton = !m_BoneOffsets.empty();
        const bool isRootBone = (nodeIndex < m_IsRootBone.size()) && (m_IsRootBone[nodeIndex] != 0);

        outPos = defaultPos;
        if (!isSkinnedSkeleton)
        {
            outPos = InterpolatePosition(*channel, timeTicks, defaultPos);
        }
        else if (isRootBone && !channel->Positions.empty())
        {
            const Math::Vec3 rawAnimPos = InterpolatePosition(*channel, timeTicks, defaultPos);
            const Math::Vec3 baseAnimPos = channel->Positions.front().Position;
            const f32 restY = std::abs(defaultPos.y);
            const f32 animRestY = std::abs(baseAnimPos.y);
            const f32 restLen = (restY > 1e-3f) ? restY : Math::Length(defaultPos);
            const f32 animRestLen = (animRestY > 1e-3f) ? animRestY : Math::Length(baseAnimPos);
            const f32 ratio = (animRestLen > 1e-3f && restLen > 1e-3f) ? (restLen / animRestLen) : 1.0f;
            // Only apply unit retarget scaling for true scale mismatches (e.g. meters vs cm),
            // preserving intentional hip height offsets such as crouch poses in same-scale clips.
            const f32 retargetScale = (ratio < 0.25f || ratio > 4.0f) ? ratio : 1.0f;

            if (m_InPlaceRootMotion)
            {
                outPos = Math::Vec3{ defaultPos.x, rawAnimPos.y * retargetScale, defaultPos.z };
            }
            else
            {
                const Math::Vec3 animDelta = (rawAnimPos - baseAnimPos) * retargetScale;
                outPos = Math::Vec3{ defaultPos.x + animDelta.x, rawAnimPos.y * retargetScale, defaultPos.z + animDelta.z };
            }
        }

        const Math::Quaternion rotFallback = channel->Rotations.empty() ? defaultRot : Math::Quaternion::Identity();
        outRot = InterpolateRotation(*channel, timeTicks, rotFallback);
        outScale = isSkinnedSkeleton
            ? defaultScale
            : InterpolateScale(*channel, timeTicks, defaultScale);
    }

    void AnimationEvaluator::EvaluateNode(u32 nodeIndex, const Math::Mat4& parentGlobalTransform, u32 depth)
    {
        if (depth > 128 || nodeIndex >= m_SkeletonHierarchy.size())
        {
            return;
        }

        const auto& node = m_SkeletonHierarchy[nodeIndex];
        const BoneAnimationChannel* channel = (nodeIndex < m_NodeChannels.size()) ? m_NodeChannels[nodeIndex] : nullptr;

        Math::Mat4 localTransform = node.LocalTransform;
        if (channel != nullptr || m_BlendActive || m_RetargetPoseActive)
        {
            Math::Vec3 pos{};
            Math::Quaternion rot{};
            Math::Vec3 scale{};
            SampleNodeLocalTRS(nodeIndex, channel, m_CurrentTime, pos, rot, scale);
            if (m_RetargetPoseActive && nodeIndex < m_RetargetLocalRots.size())
            {
                pos = Math::Vec3{ node.LocalTransform.At(3, 0), node.LocalTransform.At(3, 1), node.LocalTransform.At(3, 2) };
                rot = m_RetargetLocalRots[nodeIndex];
                scale = ExtractScaleFromMat4(node.LocalTransform);
            }

            if (m_BlendActive)
            {
                const f32 alpha = GetBlendWeight();
                Math::Vec3 srcPos{};
                Math::Quaternion srcRot{};
                Math::Vec3 srcScale{};

                if (m_BlendFromSnapshot && nodeIndex < m_SnapshotLocalPos.size())
                {
                    srcPos = m_SnapshotLocalPos[nodeIndex];
                    srcRot = m_SnapshotLocalRot[nodeIndex];
                    srcScale = m_SnapshotLocalScale[nodeIndex];
                }
                else
                {
                    const BoneAnimationChannel* prevChannel = (nodeIndex < m_PrevNodeChannels.size())
                        ? m_PrevNodeChannels[nodeIndex]
                        : nullptr;
                    SampleNodeLocalTRS(nodeIndex, prevChannel, m_PrevTime, srcPos, srcRot, srcScale);
                    if (m_PrevRetargetPoseActive && nodeIndex < m_PrevRetargetLocalRots.size())
                    {
                        srcPos = Math::Vec3{ node.LocalTransform.At(3, 0), node.LocalTransform.At(3, 1), node.LocalTransform.At(3, 2) };
                        srcRot = m_PrevRetargetLocalRots[nodeIndex];
                        srcScale = ExtractScaleFromMat4(node.LocalTransform);
                    }
                }

                pos = Math::Lerp(srcPos, pos, alpha);
                rot = Math::Slerp(srcRot, rot, alpha);
                scale = Math::Lerp(srcScale, scale, alpha);
            }

            if (nodeIndex < m_LastLocalPos.size())
            {
                m_LastLocalPos[nodeIndex] = pos;
                m_LastLocalRot[nodeIndex] = rot;
                m_LastLocalScale[nodeIndex] = scale;
            }

            localTransform = Math::TRS(pos, rot, scale);
        }

        // GlobalTransform = LocalTransform * ParentGlobalTransform
        const Math::Mat4 globalTransform = Math::MultiplyMat4(localTransform, parentGlobalTransform);
        m_GlobalTransforms[nodeIndex] = globalTransform;

        // Extract GPU skinning bone palette: FinalMatrix = OffsetMatrix * GlobalTransform
        if (node.BoneIndex >= 0 && static_cast<usize>(node.BoneIndex) < m_BoneOffsets.size())
        {
            const Math::Mat4& offsetMatrix = m_BoneOffsets[node.BoneIndex].OffsetMatrix;
            m_FinalBoneMatrices[node.BoneIndex] = Math::MultiplyMat4(offsetMatrix, globalTransform);
        }

        for (u32 childIndex : node.Children)
        {
            EvaluateNode(childIndex, globalTransform, depth + 1);
        }
    }

    std::optional<Math::Mat4> AnimationEvaluator::GetNodeGlobalTransform(std::string_view nodeName) const noexcept
    {
        auto it = m_NodeNameToIndex.find(nodeName);
        if (it != m_NodeNameToIndex.end() && it->second < m_GlobalTransforms.size())
        {
            return m_GlobalTransforms[it->second];
        }

        try
        {
            const std::string canonTarget = CanonicalBoneName(nodeName);
            auto canonIt = m_CanonicalNodeNameToIndex.find(canonTarget);
            if (canonIt != m_CanonicalNodeNameToIndex.end() && canonIt->second < m_GlobalTransforms.size())
            {
                return m_GlobalTransforms[canonIt->second];
            }
        }
        catch (...)
        {
        }

        return std::nullopt;
    }

    Math::Vec3 AnimationEvaluator::InterpolatePosition(
        const BoneAnimationChannel& channel,
        f32 currentTime,
        const Math::Vec3& fallback) noexcept
    {
        if (channel.Positions.empty())
        {
            return fallback;
        }

        if (channel.Positions.size() == 1)
        {
            return channel.Positions[0].Position;
        }

        const usize p0 = FindKeyIndex(channel.Positions, currentTime);
        const usize p1 = p0 + 1;

        if (p1 >= channel.Positions.size())
        {
            return channel.Positions.back().Position;
        }

        const auto& key0 = channel.Positions[p0];
        const auto& key1 = channel.Positions[p1];

        const f32 deltaTime = key1.Time - key0.Time;
        const f32 factor = (deltaTime > 1e-6f)
            ? std::clamp((currentTime - key0.Time) / deltaTime, 0.0f, 1.0f)
            : 0.0f;

        return Math::Lerp(key0.Position, key1.Position, factor);
    }

    Math::Quaternion AnimationEvaluator::InterpolateRotation(
        const BoneAnimationChannel& channel,
        f32 currentTime,
        const Math::Quaternion& fallback) noexcept
    {
        if (channel.Rotations.empty())
        {
            return fallback;
        }

        if (channel.Rotations.size() == 1)
        {
            return Math::NormalizeQuat(channel.Rotations[0].Rotation);
        }

        const usize r0 = FindKeyIndex(channel.Rotations, currentTime);
        const usize r1 = r0 + 1;

        if (r1 >= channel.Rotations.size())
        {
            return Math::NormalizeQuat(channel.Rotations.back().Rotation);
        }

        const auto& key0 = channel.Rotations[r0];
        const auto& key1 = channel.Rotations[r1];

        const f32 deltaTime = key1.Time - key0.Time;
        const f32 factor = (deltaTime > 1e-6f)
            ? std::clamp((currentTime - key0.Time) / deltaTime, 0.0f, 1.0f)
            : 0.0f;

        const Math::Quaternion slerped = Math::Slerp(key0.Rotation, key1.Rotation, factor);
        return Math::NormalizeQuat(slerped);
    }

    Math::Vec3 AnimationEvaluator::InterpolateScale(
        const BoneAnimationChannel& channel,
        f32 currentTime,
        const Math::Vec3& fallback) noexcept
    {
        if (channel.Scales.empty())
        {
            return fallback;
        }

        if (channel.Scales.size() == 1)
        {
            return channel.Scales[0].Scale;
        }

        const usize s0 = FindKeyIndex(channel.Scales, currentTime);
        const usize s1 = s0 + 1;

        if (s1 >= channel.Scales.size())
        {
            return channel.Scales.back().Scale;
        }

        const auto& key0 = channel.Scales[s0];
        const auto& key1 = channel.Scales[s1];

        const f32 deltaTime = key1.Time - key0.Time;
        const f32 factor = (deltaTime > 1e-6f)
            ? std::clamp((currentTime - key0.Time) / deltaTime, 0.0f, 1.0f)
            : 0.0f;

        return Math::Lerp(key0.Scale, key1.Scale, factor);
    }
}
