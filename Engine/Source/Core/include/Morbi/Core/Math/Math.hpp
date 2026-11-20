#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    #define MORBI_SIMD_SSE2 1
    #include <immintrin.h>
#else
    #define MORBI_SIMD_SSE2 0
#endif

#include "Morbi/Core/Types.hpp"

namespace Morbi::Math
{
    constexpr f32 kPi = 3.14159265358979323846f;
    constexpr f32 kDegToRad = kPi / 180.0f;
    constexpr f32 kRadToDeg = 180.0f / kPi;

    struct Vec2
    {
        f32 x = 0.0f;
        f32 y = 0.0f;
    };

    struct Vec3
    {
        f32 x = 0.0f;
        f32 y = 0.0f;
        f32 z = 0.0f;

        [[nodiscard]] static constexpr Vec3 Zero() noexcept { return { 0.0f, 0.0f, 0.0f }; }
        [[nodiscard]] static constexpr Vec3 One() noexcept { return { 1.0f, 1.0f, 1.0f }; }
        [[nodiscard]] static constexpr Vec3 Up() noexcept { return { 0.0f, 1.0f, 0.0f }; }
        [[nodiscard]] static constexpr Vec3 Down() noexcept { return { 0.0f, -1.0f, 0.0f }; }
        [[nodiscard]] static constexpr Vec3 Forward() noexcept { return { 0.0f, 0.0f, 1.0f }; }
        [[nodiscard]] static constexpr Vec3 Back() noexcept { return { 0.0f, 0.0f, -1.0f }; }
        [[nodiscard]] static constexpr Vec3 Right() noexcept { return { 1.0f, 0.0f, 0.0f }; }
        [[nodiscard]] static constexpr Vec3 Left() noexcept { return { -1.0f, 0.0f, 0.0f }; }
    };

    struct alignas(16) Vec4
    {
        f32 x = 0.0f;
        f32 y = 0.0f;
        f32 z = 0.0f;
        f32 w = 0.0f;
    };

    struct Quat
    {
        f32 x = 0.0f;
        f32 y = 0.0f;
        f32 z = 0.0f;
        f32 w = 1.0f;

        [[nodiscard]] static constexpr Quat Identity() noexcept { return { 0.0f, 0.0f, 0.0f, 1.0f }; }
    };

    using Quaternion = Quat;

    struct alignas(16) Mat4
    {
        f32 m[16]{};

        [[nodiscard]] constexpr f32& At(usize row, usize col) noexcept { return m[row * 4u + col]; }
        [[nodiscard]] constexpr const f32& At(usize row, usize col) const noexcept { return m[row * 4u + col]; }

        [[nodiscard]] static constexpr Mat4 Identity() noexcept
        {
            Mat4 result{};
            result.m[0] = 1.0f;
            result.m[5] = 1.0f;
            result.m[10] = 1.0f;
            result.m[15] = 1.0f;
            return result;
        }
    };

    // Contiguous 40-byte Transform (Position 12B + Rotation 16B + Scale 12B) for zero-copy C# Span<T> interop
    struct Transform
    {
        Vec3 Position = Vec3::Zero();
        Quat Rotation = Quat::Identity();
        Vec3 Scale = Vec3::One();
    };
    static_assert(sizeof(Transform) == 40, "Morbi::Math::Transform must be tightly packed (40 bytes) for C# Span<T> interop.");

    [[nodiscard]] constexpr Vec3 operator+(const Vec3& a, const Vec3& b) noexcept { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
    [[nodiscard]] constexpr Vec3 operator-(const Vec3& a, const Vec3& b) noexcept { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    [[nodiscard]] constexpr Vec3 operator-(const Vec3& v) noexcept { return { -v.x, -v.y, -v.z }; }
    [[nodiscard]] constexpr Vec3 operator*(const Vec3& v, f32 s) noexcept { return { v.x * s, v.y * s, v.z * s }; }
    [[nodiscard]] constexpr Vec3 operator*(f32 s, const Vec3& v) noexcept { return { v.x * s, v.y * s, v.z * s }; }
    [[nodiscard]] constexpr Vec3 operator/(const Vec3& v, f32 s) noexcept { return s != 0.0f ? Vec3{ v.x / s, v.y / s, v.z / s } : Vec3::Zero(); }

    [[nodiscard]] constexpr f32 Dot(const Vec3& a, const Vec3& b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
    [[nodiscard]] constexpr Vec3 Cross(const Vec3& a, const Vec3& b) noexcept
    {
        return {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        };
    }

    [[nodiscard]] constexpr f32 LengthSquared(const Vec3& v) noexcept { return Dot(v, v); }
    [[nodiscard]] f32 Length(const Vec3& v) noexcept;
    [[nodiscard]] Vec3 Normalize(const Vec3& v) noexcept;
    [[nodiscard]] Vec3 Lerp(const Vec3& a, const Vec3& b, f32 t) noexcept;

    [[nodiscard]] constexpr Quat MultiplyQuat(const Quat& a, const Quat& b) noexcept
    {
        return {
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
        };
    }
    [[nodiscard]] Quat NormalizeQuat(const Quat& q) noexcept;
    [[nodiscard]] Quat FromAxisAngle(const Vec3& axis, f32 radians) noexcept;
    [[nodiscard]] Quat FromEulerDegrees(const Vec3& eulerDegrees) noexcept;
    [[nodiscard]] Quat Slerp(const Quat& a, const Quat& b, f32 t) noexcept;
    [[nodiscard]] Vec3 RotateVector(const Quat& q, const Vec3& v) noexcept;

    // SIMD-accelerated Row-major Mat4 operations
    [[nodiscard]] Mat4 MultiplyMat4(const Mat4& left, const Mat4& right) noexcept;
    [[nodiscard]] Mat4 TranslationMat4(const Vec3& translation) noexcept;
    [[nodiscard]] Mat4 ScaleMat4(const Vec3& scale) noexcept;
    [[nodiscard]] Mat4 RotationMat4(const Quat& rotation) noexcept;
    [[nodiscard]] Mat4 TRS(const Vec3& position, const Quat& rotation, const Vec3& scale) noexcept;
    [[nodiscard]] Mat4 TRS(const Transform& transform) noexcept;
    [[nodiscard]] Mat4 PerspectiveLH_ZO(f32 fovRadians, f32 aspect, f32 nearZ, f32 farZ) noexcept;
    [[nodiscard]] Mat4 LookAtLH(const Vec3& eye, const Vec3& target, const Vec3& up) noexcept;
    [[nodiscard]] Vec3 TransformPoint(const Mat4& matrix, const Vec3& point) noexcept;
}
