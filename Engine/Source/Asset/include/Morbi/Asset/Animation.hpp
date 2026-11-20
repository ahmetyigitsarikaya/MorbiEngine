#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "Morbi/Core/Export.hpp"
#include "Morbi/Core/Math/Math.hpp"
#include "Morbi/Core/Types.hpp"

namespace Morbi::Asset
{
    // ============================================================================
    // 1. Skeleton Hierarchy (Node Tree for Bone Transform Forward Kinematics)
    // ============================================================================
    struct SkeletonNode
    {
        std::string Name;
        Math::Mat4 LocalTransform = Math::Mat4::Identity();
        i32 ParentIndex = -1;
        std::vector<u32> Children;
        i32 BoneIndex = -1; // -1 if locator/root node, >= 0 if bound to mesh bone palette
    };

    // ============================================================================
    // 2. Keyframe Structures (Time in Ticks + Vector / Quaternion Values)
    // ============================================================================
    struct KeyPosition
    {
        f32 Time = 0.0f;
        Math::Vec3 Position = Math::Vec3::Zero();
    };

    struct KeyRotation
    {
        f32 Time = 0.0f;
        Math::Quaternion Rotation = Math::Quaternion::Identity();
    };

    struct KeyScale
    {
        f32 Time = 0.0f;
        Math::Vec3 Scale = Math::Vec3::One();
    };

    // ============================================================================
    // 3. Bone Animation Channel (Tracks for a Single Bone / Node in the Rig)
    // ============================================================================
    struct BoneAnimationChannel
    {
        std::string NodeName;
        std::vector<KeyPosition> Positions;
        std::vector<KeyRotation> Rotations;
        std::vector<KeyScale> Scales;
    };

    // ============================================================================
    // 4. Animation Clip (Full Skeletal Animation Take, e.g. Idle, Walk, Jumping)
    // ============================================================================
    struct AnimationClip
    {
        std::string Name = "DefaultClip";
        f32 Duration = 0.0f;        // Total duration in ticks
        f32 TicksPerSecond = 24.0f; // Playback rate in ticks per second
        std::unordered_map<std::string, BoneAnimationChannel> Channels;

        [[nodiscard]] f32 GetDurationSeconds() const noexcept
        {
            return (TicksPerSecond > 0.0f) ? (Duration / TicksPerSecond) : Duration;
        }
    };
}
