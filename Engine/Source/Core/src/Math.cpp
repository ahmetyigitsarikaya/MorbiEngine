#include "Morbi/Core/Math/Math.hpp"

namespace Morbi::Math
{
    f32 Length(const Vec3& v) noexcept
    {
        return std::sqrt(LengthSquared(v));
    }

    Vec3 Normalize(const Vec3& v) noexcept
    {
        const f32 lenSq = LengthSquared(v);
        if (lenSq <= 1e-8f)
        {
            return Vec3::Zero();
        }
        const f32 invLen = 1.0f / std::sqrt(lenSq);
        return v * invLen;
    }

    Vec3 Lerp(const Vec3& a, const Vec3& b, f32 t) noexcept
    {
        return a + (b - a) * t;
    }

    Quat NormalizeQuat(const Quat& q) noexcept
    {
        const f32 lenSq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
        if (lenSq <= 1e-8f)
        {
            return Quat::Identity();
        }
        const f32 invLen = 1.0f / std::sqrt(lenSq);
        return { q.x * invLen, q.y * invLen, q.z * invLen, q.w * invLen };
    }

    Quat FromAxisAngle(const Vec3& axis, f32 radians) noexcept
    {
        const Vec3 normAxis = Normalize(axis);
        const f32 half = radians * 0.5f;
        const f32 s = std::sin(half);
        return NormalizeQuat({ normAxis.x * s, normAxis.y * s, normAxis.z * s, std::cos(half) });
    }

    Quat FromEulerDegrees(const Vec3& eulerDegrees) noexcept
    {
        const Vec3 rad = eulerDegrees * kDegToRad;
        const f32 cx = std::cos(rad.x * 0.5f);
        const f32 sx = std::sin(rad.x * 0.5f);
        const f32 cy = std::cos(rad.y * 0.5f);
        const f32 sy = std::sin(rad.y * 0.5f);
        const f32 cz = std::cos(rad.z * 0.5f);
        const f32 sz = std::sin(rad.z * 0.5f);

        return NormalizeQuat({
            sx * cy * cz - cx * sy * sz,
            cx * sy * cz + sx * cy * sz,
            cx * cy * sz - sx * sy * cz,
            cx * cy * cz + sx * sy * sz
        });
    }

    Quat Slerp(const Quat& a, const Quat& b, f32 t) noexcept
    {
        Quat q1 = NormalizeQuat(a);
        Quat q2 = NormalizeQuat(b);

        f32 dot = q1.x * q2.x + q1.y * q2.y + q1.z * q2.z + q1.w * q2.w;
        if (dot < 0.0f)
        {
            dot = -dot;
            q2 = { -q2.x, -q2.y, -q2.z, -q2.w };
        }

        if (dot > 0.9995f)
        {
            return NormalizeQuat({
                q1.x + t * (q2.x - q1.x),
                q1.y + t * (q2.y - q1.y),
                q1.z + t * (q2.z - q1.z),
                q1.w + t * (q2.w - q1.w)
            });
        }

        const f32 theta0 = std::acos(std::clamp(dot, -1.0f, 1.0f));
        const f32 theta = theta0 * t;
        const f32 sinTheta = std::sin(theta);
        const f32 sinTheta0 = std::sin(theta0);
        const f32 s0 = std::cos(theta) - dot * sinTheta / sinTheta0;
        const f32 s1 = sinTheta / sinTheta0;

        return NormalizeQuat({
            s0 * q1.x + s1 * q2.x,
            s0 * q1.y + s1 * q2.y,
            s0 * q1.z + s1 * q2.z,
            s0 * q1.w + s1 * q2.w
        });
    }

    Vec3 RotateVector(const Quat& q, const Vec3& v) noexcept
    {
        const Vec3 u{ q.x, q.y, q.z };
        const Vec3 uv = Cross(u, v);
        const Vec3 uuv = Cross(u, uv);
        return v + ((uv * q.w) + uuv) * 2.0f;
    }

    Mat4 MultiplyMat4(const Mat4& left, const Mat4& right) noexcept
    {
        Mat4 out{};
#if MORBI_SIMD_SSE2
        const __m128 r0 = _mm_load_ps(&right.m[0]);
        const __m128 r1 = _mm_load_ps(&right.m[4]);
        const __m128 r2 = _mm_load_ps(&right.m[8]);
        const __m128 r3 = _mm_load_ps(&right.m[12]);

        for (usize i = 0; i < 4; ++i)
        {
            const __m128 l0 = _mm_set1_ps(left.m[i * 4 + 0]);
            const __m128 l1 = _mm_set1_ps(left.m[i * 4 + 1]);
            const __m128 l2 = _mm_set1_ps(left.m[i * 4 + 2]);
            const __m128 l3 = _mm_set1_ps(left.m[i * 4 + 3]);

            __m128 row = _mm_add_ps(
                _mm_add_ps(_mm_mul_ps(l0, r0), _mm_mul_ps(l1, r1)),
                _mm_add_ps(_mm_mul_ps(l2, r2), _mm_mul_ps(l3, r3))
            );
            _mm_store_ps(&out.m[i * 4], row);
        }
#else
        for (usize r = 0; r < 4; ++r)
        {
            for (usize c = 0; c < 4; ++c)
            {
                out.At(r, c) =
                    left.At(r, 0) * right.At(0, c) +
                    left.At(r, 1) * right.At(1, c) +
                    left.At(r, 2) * right.At(2, c) +
                    left.At(r, 3) * right.At(3, c);
            }
        }
#endif
        return out;
    }

    Mat4 TranslationMat4(const Vec3& t) noexcept
    {
        Mat4 result = Mat4::Identity();
        result.At(3, 0) = t.x;
        result.At(3, 1) = t.y;
        result.At(3, 2) = t.z;
        return result;
    }

    Mat4 ScaleMat4(const Vec3& s) noexcept
    {
        Mat4 result = Mat4::Identity();
        result.At(0, 0) = s.x;
        result.At(1, 1) = s.y;
        result.At(2, 2) = s.z;
        return result;
    }

    Mat4 RotationMat4(const Quat& rotation) noexcept
    {
        const Quat q = NormalizeQuat(rotation);
        const f32 xx = q.x * q.x;
        const f32 yy = q.y * q.y;
        const f32 zz = q.z * q.z;
        const f32 xy = q.x * q.y;
        const f32 xz = q.x * q.z;
        const f32 yz = q.y * q.z;
        const f32 wx = q.w * q.x;
        const f32 wy = q.w * q.y;
        const f32 wz = q.w * q.z;

        Mat4 result = Mat4::Identity();
        result.At(0, 0) = 1.0f - 2.0f * (yy + zz);
        result.At(0, 1) = 2.0f * (xy + wz);
        result.At(0, 2) = 2.0f * (xz - wy);

        result.At(1, 0) = 2.0f * (xy - wz);
        result.At(1, 1) = 1.0f - 2.0f * (xx + zz);
        result.At(1, 2) = 2.0f * (yz + wx);

        result.At(2, 0) = 2.0f * (xz + wy);
        result.At(2, 1) = 2.0f * (yz - wx);
        result.At(2, 2) = 1.0f - 2.0f * (xx + yy);
        return result;
    }

    Mat4 TRS(const Vec3& position, const Quat& rotation, const Vec3& scale) noexcept
    {
        return MultiplyMat4(MultiplyMat4(ScaleMat4(scale), RotationMat4(rotation)), TranslationMat4(position));
    }

    Mat4 TRS(const Transform& transform) noexcept
    {
        return TRS(transform.Position, transform.Rotation, transform.Scale);
    }

    Mat4 PerspectiveLH_ZO(f32 fovRadians, f32 aspect, f32 nearZ, f32 farZ) noexcept
    {
        const f32 h = 1.0f / std::tan(fovRadians * 0.5f);
        const f32 w = h / aspect;
        const f32 range = farZ / (farZ - nearZ);

        Mat4 out{};
        out.At(0, 0) = w;
        out.At(1, 1) = h;
        out.At(2, 2) = range;
        out.At(2, 3) = 1.0f;
        out.At(3, 2) = -range * nearZ;
        return out;
    }

    Mat4 LookAtLH(const Vec3& eye, const Vec3& target, const Vec3& up) noexcept
    {
        const Vec3 zAxis = Normalize(target - eye);
        const Vec3 xAxis = Normalize(Cross(up, zAxis));
        const Vec3 yAxis = Cross(zAxis, xAxis);

        Mat4 out = Mat4::Identity();
        out.At(0, 0) = xAxis.x; out.At(0, 1) = yAxis.x; out.At(0, 2) = zAxis.x;
        out.At(1, 0) = xAxis.y; out.At(1, 1) = yAxis.y; out.At(1, 2) = zAxis.y;
        out.At(2, 0) = xAxis.z; out.At(2, 1) = yAxis.z; out.At(2, 2) = zAxis.z;
        out.At(3, 0) = -Dot(xAxis, eye);
        out.At(3, 1) = -Dot(yAxis, eye);
        out.At(3, 2) = -Dot(zAxis, eye);
        return out;
    }

    Vec3 TransformPoint(const Mat4& matrix, const Vec3& point) noexcept
    {
        return {
            point.x * matrix.At(0, 0) + point.y * matrix.At(1, 0) + point.z * matrix.At(2, 0) + matrix.At(3, 0),
            point.x * matrix.At(0, 1) + point.y * matrix.At(1, 1) + point.z * matrix.At(2, 1) + matrix.At(3, 1),
            point.x * matrix.At(0, 2) + point.y * matrix.At(1, 2) + point.z * matrix.At(2, 2) + matrix.At(3, 2)
        };
    }
}
