#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Morbi/Asset/Animation.hpp"
#include "Morbi/Asset/ModelImporter.hpp"
#include "Morbi/Core/Export.hpp"
#include "Morbi/Core/Math/Math.hpp"
#include "Morbi/Core/Types.hpp"

namespace Morbi::Asset
{
    struct TransparentStringHash
    {
        using is_transparent = void;
        [[nodiscard]] usize operator()(std::string_view sv) const noexcept
        {
            return std::hash<std::string_view>{}(sv);
        }
    };

    // ============================================================================
    // PerObjectBuffer: GPU Skinning Constant Buffer (100 Bone Matrices = 6400 Bytes)
    // ============================================================================
    struct alignas(16) PerObjectBuffer
    {
        Math::Mat4 BoneMatrices[100];
    };
    static_assert(sizeof(PerObjectBuffer) == 6400, "PerObjectBuffer must be exactly 6400 bytes (100 float4x4 matrices).");

    // ============================================================================
    // AnimationEvaluator: Skeletal Animation Sampling & GPU Bone Matrix Evaluator
    // ============================================================================
    class MORBI_API AnimationEvaluator
    {
    public:
        AnimationEvaluator() = default;
        ~AnimationEvaluator() = default;

        // Initialize with skeleton hierarchy and inverse bind pose offset matrices
        void Initialize(
            std::vector<SkeletonNode> skeletonHierarchy,
            std::vector<BoneInfo> boneOffsets,
            const AnimationClip* initialClip = nullptr);

        void Initialize(
            const ImportedMeshData& meshData,
            const AnimationClip* initialClip = nullptr);

        // Configures a source skeleton (e.g., Mixamo Remy) for global-delta retargeting onto CC3+ rigs
        void SetRetargetSourceSkeleton(const ImportedMeshData& sourceMesh);

        // Populate 100-bone GPU ConstantBuffer palette with clamp safety & identity padding
        void FillGPUBuffer(PerObjectBuffer& outBuffer) const noexcept;

        // Assign active animation clip (nullptr evaluates bind pose / rest pose)
        void SetAnimation(const AnimationClip* clip) noexcept;

        // Smoothly crossfade from current clip/pose to target clip over blendDurationSeconds
        void CrossFade(const AnimationClip* clip, f32 blendDurationSeconds = 0.15f) noexcept;

        // Advance animation time and compute current GPU bone matrix palette
        void UpdateAnimation(f32 deltaTime);

        // Explicitly re-evaluate bone transforms for current time
        void CalculateTransforms();

        // Playback and timeline controls
        void Play() noexcept { m_IsPlaying = true; }
        void Pause() noexcept { m_IsPlaying = false; }
        void Stop() noexcept;
        void SetCurrentTimeTicks(f32 ticks);
        void SetCurrentTimeSeconds(f32 seconds);
        void SetPlaybackSpeed(f32 speed) noexcept { m_PlaybackSpeed = speed; }
        void SetLoop(bool loop) noexcept { m_Loop = loop; }
        void SetInPlaceRootMotion(bool inPlace) noexcept { m_InPlaceRootMotion = inPlace; }

        [[nodiscard]] bool IsPlaying() const noexcept { return m_IsPlaying; }
        [[nodiscard]] bool IsBlending() const noexcept { return m_BlendActive; }
        [[nodiscard]] f32 GetBlendWeight() const noexcept;
        [[nodiscard]] bool IsLooping() const noexcept { return m_Loop; }
        [[nodiscard]] bool IsInPlaceRootMotion() const noexcept { return m_InPlaceRootMotion; }
        [[nodiscard]] f32 GetPlaybackSpeed() const noexcept { return m_PlaybackSpeed; }
        [[nodiscard]] f32 GetCurrentTimeTicks() const noexcept { return m_CurrentTime; }
        [[nodiscard]] f32 GetCurrentTimeSeconds() const noexcept;
        [[nodiscard]] const AnimationClip* GetCurrentClip() const noexcept { return m_CurrentClip; }

        // Evaluated outputs
        [[nodiscard]] std::span<const Math::Mat4> GetFinalBoneMatrices() const noexcept { return m_FinalBoneMatrices; }
        [[nodiscard]] const std::vector<Math::Mat4>& GetFinalBoneMatricesVector() const noexcept { return m_FinalBoneMatrices; }
        [[nodiscard]] std::span<const Math::Mat4> GetGlobalTransforms() const noexcept { return m_GlobalTransforms; }
        [[nodiscard]] usize GetBoneCount() const noexcept { return m_BoneOffsets.size(); }
        [[nodiscard]] usize GetNodeCount() const noexcept { return m_SkeletonHierarchy.size(); }

        // Socket attachment queries (e.g. "RightHand", "Head", "Spine") with zero allocation
        [[nodiscard]] std::optional<Math::Mat4> GetNodeGlobalTransform(std::string_view nodeName) const noexcept;

        // Keyframe interpolation helpers
        [[nodiscard]] static Math::Vec3 InterpolatePosition(
            const BoneAnimationChannel& channel,
            f32 currentTime,
            const Math::Vec3& fallback = Math::Vec3::Zero()) noexcept;

        [[nodiscard]] static Math::Quaternion InterpolateRotation(
            const BoneAnimationChannel& channel,
            f32 currentTime,
            const Math::Quaternion& fallback = Math::Quaternion::Identity()) noexcept;

        [[nodiscard]] static Math::Vec3 InterpolateScale(
            const BoneAnimationChannel& channel,
            f32 currentTime,
            const Math::Vec3& fallback = Math::Vec3::One()) noexcept;

    private:
        void EvaluateNode(u32 nodeIndex, const Math::Mat4& parentGlobalTransform, u32 depth = 0);
        void BindClipChannels(const AnimationClip* clip, std::vector<const BoneAnimationChannel*>& outChannels, bool logSummary) const noexcept;
        void SampleNodeLocalTRS(
            u32 nodeIndex,
            const BoneAnimationChannel* channel,
            f32 timeTicks,
            Math::Vec3& outPos,
            Math::Quaternion& outRot,
            Math::Vec3& outScale) const noexcept;
        void ComputeRetargetedPose(
            const AnimationClip* clip,
            f32 timeTicks,
            std::vector<Math::Quaternion>& outLocalRots) const;

    private:
        const AnimationClip* m_CurrentClip = nullptr;
        const AnimationClip* m_PrevClip = nullptr;
        std::vector<SkeletonNode> m_SkeletonHierarchy;
        std::vector<BoneInfo> m_BoneOffsets;
        std::unordered_map<std::string, u32, TransparentStringHash, std::equal_to<>> m_NodeNameToIndex;
        std::unordered_map<std::string, u32, TransparentStringHash, std::equal_to<>> m_CanonicalNodeNameToIndex;
        std::vector<const BoneAnimationChannel*> m_NodeChannels;     // O(1) pre-bound channel lookup (current)
        std::vector<const BoneAnimationChannel*> m_PrevNodeChannels; // O(1) pre-bound channel lookup (previous for crossfade)
        std::vector<u8> m_IsRootBone;                                // 1 if root/hips bone of skeleton, 0 otherwise

        std::vector<Math::Mat4> m_GlobalTransforms;  // Global transforms for every SkeletonNode
        std::vector<Math::Mat4> m_FinalBoneMatrices; // GPU bone palette: OffsetMatrix * GlobalTransform

        // Crossfade / inertial snapshot pose buffers (pre-sized to node count, zero per-frame allocations)
        std::vector<Math::Vec3> m_LastLocalPos;
        std::vector<Math::Quaternion> m_LastLocalRot;
        std::vector<Math::Vec3> m_LastLocalScale;
        std::vector<Math::Vec3> m_SnapshotLocalPos;
        std::vector<Math::Quaternion> m_SnapshotLocalRot;
        std::vector<Math::Vec3> m_SnapshotLocalScale;

        f32 m_CurrentTime = 0.0f;
        f32 m_PrevTime = 0.0f;
        f32 m_PlaybackSpeed = 1.0f;
        f32 m_PrevPlaybackSpeed = 1.0f;
        bool m_Loop = true;
        bool m_PrevLoop = true;
        bool m_IsPlaying = true;
        bool m_InPlaceRootMotion = true;

        bool m_BlendActive = false;
        bool m_BlendFromSnapshot = false;
        f32 m_BlendElapsed = 0.0f;
        f32 m_BlendDuration = 0.0f;

        // Global-delta retargeting state (Mixamo -> CC3+)
        bool m_HasRetargetSource = false;
        bool m_RetargetPoseActive = false;
        bool m_PrevRetargetPoseActive = false;
        std::vector<SkeletonNode> m_SourceNodes;
        std::vector<Math::Mat4> m_SourceBindGlobalRot;
        std::vector<Math::Mat4> m_TargetBindGlobalRot;
        std::vector<i32> m_TargetToSourceNode;
        Math::Mat4 m_RetargetZUpToYUp = Math::Mat4::Identity();
        mutable std::vector<Math::Quaternion> m_RetargetLocalRots;
        mutable std::vector<Math::Quaternion> m_PrevRetargetLocalRots;
    };
}
