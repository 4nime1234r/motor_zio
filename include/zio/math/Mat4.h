#pragma once

#include <cmath>

#include "zio/math/Quaternion.h"
#include "zio/math/Vec3.h"

namespace zio::math
{

struct Mat4
{
    float m[4][4] = {
        {1.f, 0.f, 0.f, 0.f},
        {0.f, 1.f, 0.f, 0.f},
        {0.f, 0.f, 1.f, 0.f},
        {0.f, 0.f, 0.f, 1.f},
    };

    static constexpr Mat4 identity() { return Mat4{}; }

    static constexpr Mat4 translation(Vec3 t)
    {
        Mat4 result;
        result.m[0][3] = t.x;
        result.m[1][3] = t.y;
        result.m[2][3] = t.z;
        return result;
    }

    static constexpr Mat4 scale(Vec3 s)
    {
        Mat4 result;
        result.m[0][0] = s.x;
        result.m[1][1] = s.y;
        result.m[2][2] = s.z;
        return result;
    }

    static constexpr Mat4 rotation(const Quaternion& q)
    {
        const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
        const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
        const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

        Mat4 result;
        result.m[0][0] = 1.f - 2.f * (yy + zz);
        result.m[0][1] = 2.f * (xy - wz);
        result.m[0][2] = 2.f * (xz + wy);
        result.m[1][0] = 2.f * (xy + wz);
        result.m[1][1] = 1.f - 2.f * (xx + zz);
        result.m[1][2] = 2.f * (yz - wx);
        result.m[2][0] = 2.f * (xz - wy);
        result.m[2][1] = 2.f * (yz + wx);
        result.m[2][2] = 1.f - 2.f * (xx + yy);
        return result;
    }

    static constexpr Mat4 orthographic(float left, float right, float bottom, float top, float nearZ, float farZ)
    {
        Mat4 result;
        result.m[0][0] = 2.f / (right - left);
        result.m[0][3] = -(right + left) / (right - left);
        result.m[1][1] = 2.f / (top - bottom);
        result.m[1][3] = -(top + bottom) / (top - bottom);
        result.m[2][2] = 1.f / (farZ - nearZ);
        result.m[2][3] = -nearZ / (farZ - nearZ);
        return result;
    }

    static Mat4 perspective(float fovYDegrees, float aspect, float nearZ, float farZ)
    {
        const float f = 1.f / std::tan(fovYDegrees * (3.14159265358979323846f / 180.f) * 0.5f);
        Mat4 result;
        result.m[0][0] = f / aspect;
        result.m[1][1] = f;
        result.m[2][2] = farZ / (nearZ - farZ);
        result.m[2][3] = nearZ * farZ / (nearZ - farZ);
        result.m[3][2] = -1.f;
        result.m[3][3] = 0.f;
        return result;
    }

    static Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up)
    {
        const Vec3 f = (target - eye).normalized();
        const Vec3 s = f.cross(up).normalized();
        const Vec3 u = s.cross(f);
        Mat4 result;
        result.m[0][0] = s.x;  result.m[0][1] = s.y;  result.m[0][2] = s.z;  result.m[0][3] = -s.dot(eye);
        result.m[1][0] = u.x;  result.m[1][1] = u.y;  result.m[1][2] = u.z;  result.m[1][3] = -u.dot(eye);
        result.m[2][0] = -f.x; result.m[2][1] = -f.y; result.m[2][2] = -f.z; result.m[2][3] = f.dot(eye);
        return result;
    }

    constexpr Mat4 operator*(const Mat4& rhs) const
    {
        Mat4 result;
        for (int row = 0; row < 4; ++row)
        {
            for (int col = 0; col < 4; ++col)
            {
                float sum = 0.f;
                for (int k = 0; k < 4; ++k)
                    sum += m[row][k] * rhs.m[k][col];
                result.m[row][col] = sum;
            }
        }
        return result;
    }

    Mat4 inverseAffine() const
    {
        const float a = m[0][0], b = m[0][1], c = m[0][2];
        const float d = m[1][0], e = m[1][1], f = m[1][2];
        const float g = m[2][0], h = m[2][1], i = m[2][2];

        const float c00 = e * i - f * h;
        const float c01 = f * g - d * i;
        const float c02 = d * h - e * g;
        const float det = a * c00 + b * c01 + c * c02;
        if (std::fabs(det) < 1e-12f)
            return identity();

        const float inv = 1.f / det;
        Mat4 r;
        r.m[0][0] = c00 * inv;
        r.m[0][1] = (c * h - b * i) * inv;
        r.m[0][2] = (b * f - c * e) * inv;
        r.m[1][0] = c01 * inv;
        r.m[1][1] = (a * i - c * g) * inv;
        r.m[1][2] = (c * d - a * f) * inv;
        r.m[2][0] = c02 * inv;
        r.m[2][1] = (b * g - a * h) * inv;
        r.m[2][2] = (a * e - b * d) * inv;

        const float tx = m[0][3], ty = m[1][3], tz = m[2][3];
        r.m[0][3]      = -(r.m[0][0] * tx + r.m[0][1] * ty + r.m[0][2] * tz);
        r.m[1][3]      = -(r.m[1][0] * tx + r.m[1][1] * ty + r.m[1][2] * tz);
        r.m[2][3]      = -(r.m[2][0] * tx + r.m[2][1] * ty + r.m[2][2] * tz);
        return r;
    }

    constexpr Vec3 transformPoint(Vec3 p) const
    {
        return {
            m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + m[0][3],
            m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + m[1][3],
            m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z + m[2][3],
        };
    }

    constexpr Vec3 transformVector(Vec3 v) const
    {
        return {
            m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
            m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
            m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z,
        };
    }

    constexpr Mat4 transposed() const
    {
        Mat4 result;
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col)
                result.m[row][col] = m[col][row];
        return result;
    }

    const float* data() const { return &m[0][0]; }
};

}
