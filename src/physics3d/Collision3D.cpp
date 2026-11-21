#include "Collision3D.h"

#include <algorithm>
#include <cmath>

namespace zio::physics3d::detail
{

namespace
{

using math::Quaternion;
using math::Vec3;

constexpr float kEpsilon = 1e-6f;

float comp(const Vec3& v, int i)
{
    return i == 0 ? v.x : (i == 1 ? v.y : v.z);
}

Vec3 unitAxis(int i)
{
    return i == 0 ? Vec3::unitX() : (i == 1 ? Vec3::unitY() : Vec3::unitZ());
}

Vec3 withComp(Vec3 v, int i, float value)
{
    if (i == 0)
        v.x = value;
    else if (i == 1)
        v.y = value;
    else
        v.z = value;
    return v;
}

struct Obb
{
    Vec3 center;
    Vec3 axis[3];
    float half[3] = {0.f, 0.f, 0.f};
};

Obb makeObb(const BoxShape& box, const Pose& pose)
{
    Obb o;
    o.center = pose.position;
    for (int i = 0; i < 3; ++i)
    {
        o.axis[i] = pose.rotation.rotate(unitAxis(i));
        o.half[i] = comp(box.halfExtents, i);
    }
    return o;
}

float radiusOn(const Obb& o, const Vec3& axis)
{
    float r = 0.f;
    for (int i = 0; i < 3; ++i)
        r += o.half[i] * std::fabs(axis.dot(o.axis[i]));
    return r;
}

bool sphereSphere(const SphereShape& a, const Pose& pa, const SphereShape& b, const Pose& pb, Manifold& m)
{
    const Vec3 d      = pb.position - pa.position;
    const float dist  = d.length();
    const float radii = a.radius + b.radius;
    if (dist >= radii)
        return false;

    const Vec3 n            = dist > kEpsilon ? d * (1.f / dist) : Vec3::unitY();
    const float pen         = radii - dist;
    m.normal                = n;
    m.count                 = 1;
    m.points[0].point       = pa.position + n * (a.radius - pen * 0.5f);
    m.points[0].penetration = pen;
    return true;
}

bool sphereVsBox(const SphereShape& sphere, const Pose& sp, const BoxShape& box, const Pose& bp, Manifold& m)
{
    const Quaternion inverse = bp.rotation.conjugate();
    const Vec3 local         = inverse.rotate(sp.position - bp.position);
    const Vec3 he            = box.halfExtents;
    const Vec3 clamped{std::clamp(local.x, -he.x, he.x), std::clamp(local.y, -he.y, he.y),
                       std::clamp(local.z, -he.z, he.z)};
    const Vec3 diff = local - clamped;
    const float d2  = diff.dot(diff);

    Vec3 normalLocal;
    Vec3 pointLocal;
    float penetration = 0.f;
    if (d2 > 1e-12f)
    {
        if (d2 >= sphere.radius * sphere.radius)
            return false;
        const float dist = std::sqrt(d2);
        normalLocal      = diff * (1.f / dist);
        pointLocal       = clamped;
        penetration      = sphere.radius - dist;
    }
    else
    {
        int axis         = 0;
        float bestMargin = comp(he, 0) - std::fabs(comp(local, 0));
        for (int i = 1; i < 3; ++i)
        {
            const float margin = comp(he, i) - std::fabs(comp(local, i));
            if (margin < bestMargin)
            {
                bestMargin = margin;
                axis       = i;
            }
        }
        const float sign = comp(local, axis) >= 0.f ? 1.f : -1.f;
        normalLocal      = unitAxis(axis) * sign;
        pointLocal       = withComp(local, axis, sign * comp(he, axis));
        penetration      = sphere.radius + bestMargin;
    }

    m.normal                = bp.rotation.rotate(normalLocal);
    m.count                 = 1;
    m.points[0].point       = bp.position + bp.rotation.rotate(pointLocal);
    m.points[0].penetration = penetration;
    return true;
}

struct Polygon
{
    Vec3 v[16];
    int count = 0;
};

void clipPolygon(Polygon& poly, const Vec3& normal, float offset)
{
    Polygon out;
    for (int i = 0; i < poly.count; ++i)
    {
        const Vec3 a   = poly.v[i];
        const Vec3 b   = poly.v[(i + 1) % poly.count];
        const float da = normal.dot(a) - offset;
        const float db = normal.dot(b) - offset;
        if (da <= 0.f && out.count < 16)
            out.v[out.count++] = a;
        if (((da < 0.f && db > 0.f) || (da > 0.f && db < 0.f)) && out.count < 16)
        {
            const float t      = da / (da - db);
            out.v[out.count++] = a + (b - a) * t;
        }
    }
    poly = out;
}

void reduceContacts(const Vec3* points, const float* depths, int count, Manifold& m)
{
    if (count <= 4)
    {
        for (int i = 0; i < count; ++i)
            m.points[i] = {points[i], depths[i]};
        m.count = count;
        return;
    }

    int chosen[4] = {0, 0, 0, 0};
    for (int i = 1; i < count; ++i)
        if (depths[i] > depths[chosen[0]])
            chosen[0] = i;

    float farthest = -1.f;
    for (int i = 0; i < count; ++i)
    {
        const Vec3 d  = points[i] - points[chosen[0]];
        const float s = d.dot(d);
        if (s > farthest)
        {
            farthest  = s;
            chosen[1] = i;
        }
    }

    const Vec3 line = points[chosen[1]] - points[chosen[0]];
    Vec3 up         = Vec3::zero();
    for (int i = 0; i < count; ++i)
    {
        const Vec3 c = line.cross(points[i] - points[chosen[0]]);
        if (up.dot(up) < kEpsilon && c.dot(c) > kEpsilon)
            up = c.normalized();
    }

    float bestPositive = 0.f;
    float bestNegative = 0.f;
    chosen[2]          = chosen[0];
    chosen[3]          = chosen[1];
    for (int i = 0; i < count; ++i)
    {
        const float area = line.cross(points[i] - points[chosen[0]]).dot(up);
        if (area > bestPositive)
        {
            bestPositive = area;
            chosen[2]    = i;
        }
        if (area < bestNegative)
        {
            bestNegative = area;
            chosen[3]    = i;
        }
    }

    m.count = 0;
    for (int k = 0; k < 4; ++k)
    {
        bool duplicate = false;
        for (int j = 0; j < m.count; ++j)
            if ((m.points[j].point - points[chosen[k]]).length() < 1e-5f)
                duplicate = true;
        if (!duplicate)
            m.points[m.count++] = {points[chosen[k]], depths[chosen[k]]};
    }
}

bool boxBox(const BoxShape& boxA, const Pose& pa, const BoxShape& boxB, const Pose& pb, Manifold& m)
{
    const Obb A  = makeObb(boxA, pa);
    const Obb B  = makeObb(boxB, pb);
    const Vec3 d = B.center - A.center;

    float facePenetration = 1e30f;
    int faceAxis          = -1;
    for (int i = 0; i < 6; ++i)
    {
        const Vec3& axis    = i < 3 ? A.axis[i] : B.axis[i - 3];
        const float overlap = radiusOn(A, axis) + radiusOn(B, axis) - std::fabs(d.dot(axis));
        if (overlap < 0.f)
            return false;
        if (overlap < facePenetration)
        {
            facePenetration = overlap;
            faceAxis        = i;
        }
    }

    float edgePenetration = 1e30f;
    int edgeA             = -1;
    int edgeB             = -1;
    Vec3 edgeNormal;
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            Vec3 axis          = A.axis[i].cross(B.axis[j]);
            const float length = axis.length();
            if (length < 1e-4f)
                continue;
            axis                = axis * (1.f / length);
            const float overlap = radiusOn(A, axis) + radiusOn(B, axis) - std::fabs(d.dot(axis));
            if (overlap < 0.f)
                return false;
            if (overlap < edgePenetration)
            {
                edgePenetration = overlap;
                edgeA           = i;
                edgeB           = j;
                edgeNormal      = d.dot(axis) >= 0.f ? axis : axis * -1.f;
            }
        }
    }

    const bool useEdge = edgeA >= 0 && edgePenetration + 0.005f < facePenetration * 0.95f;
    if (useEdge)
    {
        Vec3 pointA = A.center;
        Vec3 pointB = B.center;
        for (int k = 0; k < 3; ++k)
        {
            if (k != edgeA)
                pointA = pointA + A.axis[k] * ((edgeNormal.dot(A.axis[k]) >= 0.f ? 1.f : -1.f) * A.half[k]);
            if (k != edgeB)
                pointB = pointB + B.axis[k] * ((edgeNormal.dot(B.axis[k]) >= 0.f ? -1.f : 1.f) * B.half[k]);
        }
        const Vec3& u           = A.axis[edgeA];
        const Vec3& v           = B.axis[edgeB];
        const Vec3 r            = pointA - pointB;
        const float b           = u.dot(v);
        const float c           = u.dot(r);
        const float f           = v.dot(r);
        const float denominator = 1.f - b * b;
        const float s           = denominator > kEpsilon ? (b * f - c) / denominator : 0.f;
        const float t           = b * s + f;
        const Vec3 closestA     = pointA + u * s;
        const Vec3 closestB     = pointB + v * t;

        m.normal                = edgeNormal;
        m.count                 = 1;
        m.points[0].point       = (closestA + closestB) * 0.5f;
        m.points[0].penetration = edgePenetration;
        return true;
    }

    const bool referenceIsA = faceAxis < 3;
    const Obb& R            = referenceIsA ? A : B;
    const Obb& I            = referenceIsA ? B : A;
    const int referenceAxis = referenceIsA ? faceAxis : faceAxis - 3;

    const float referenceSign  = (I.center - R.center).dot(R.axis[referenceAxis]) >= 0.f ? 1.f : -1.f;
    const Vec3 referenceNormal = R.axis[referenceAxis] * referenceSign;
    const Vec3 referenceCenter = R.center + referenceNormal * R.half[referenceAxis];

    int incidentAxis = 0;
    float bestAlign  = -1.f;
    for (int k = 0; k < 3; ++k)
    {
        const float align = std::fabs(I.axis[k].dot(referenceNormal));
        if (align > bestAlign)
        {
            bestAlign    = align;
            incidentAxis = k;
        }
    }
    const float incidentSign  = I.axis[incidentAxis].dot(referenceNormal) > 0.f ? -1.f : 1.f;
    const Vec3 incidentCenter = I.center + I.axis[incidentAxis] * (incidentSign * I.half[incidentAxis]);
    const int u               = (incidentAxis + 1) % 3;
    const int w               = (incidentAxis + 2) % 3;

    Polygon polygon;
    polygon.count = 4;
    polygon.v[0]  = incidentCenter + I.axis[u] * I.half[u] + I.axis[w] * I.half[w];
    polygon.v[1]  = incidentCenter - I.axis[u] * I.half[u] + I.axis[w] * I.half[w];
    polygon.v[2]  = incidentCenter - I.axis[u] * I.half[u] - I.axis[w] * I.half[w];
    polygon.v[3]  = incidentCenter + I.axis[u] * I.half[u] - I.axis[w] * I.half[w];

    for (int k = 0; k < 3; ++k)
    {
        if (k == referenceAxis)
            continue;
        const float centerProjection = R.center.dot(R.axis[k]);
        clipPolygon(polygon, R.axis[k], centerProjection + R.half[k]);
        clipPolygon(polygon, R.axis[k] * -1.f, -centerProjection + R.half[k]);
    }

    Vec3 points[16];
    float depths[16];
    int kept = 0;
    for (int i = 0; i < polygon.count; ++i)
    {
        const float depth = (referenceCenter - polygon.v[i]).dot(referenceNormal);
        if (depth >= 0.f)
        {
            points[kept] = polygon.v[i];
            depths[kept] = depth;
            ++kept;
        }
    }
    if (kept == 0)
        return false;

    m.normal = referenceIsA ? referenceNormal : referenceNormal * -1.f;
    reduceContacts(points, depths, kept, m);
    return m.count > 0;
}

}

bool collide(const Shape3D& a, const Pose& pa, const Shape3D& b, const Pose& pb, Manifold& out)
{
    out = Manifold{};
    if (const auto* sa = std::get_if<SphereShape>(&a))
    {
        if (const auto* sb = std::get_if<SphereShape>(&b))
            return sphereSphere(*sa, pa, *sb, pb, out);
        const bool hit = sphereVsBox(*sa, pa, std::get<BoxShape>(b), pb, out);
        if (hit)
            out.normal = out.normal * -1.f;
        return hit;
    }

    const auto& boxA = std::get<BoxShape>(a);
    if (const auto* sb = std::get_if<SphereShape>(&b))
        return sphereVsBox(*sb, pb, boxA, pa, out);
    return boxBox(boxA, pa, std::get<BoxShape>(b), pb, out);
}

Aabb computeAabb(const Shape3D& shape, const Pose& pose)
{
    if (const auto* sphere = std::get_if<SphereShape>(&shape))
    {
        const Vec3 r{sphere->radius, sphere->radius, sphere->radius};
        return {pose.position - r, pose.position + r};
    }

    const Obb o = makeObb(std::get<BoxShape>(shape), pose);
    Vec3 extent = Vec3::zero();
    for (int i = 0; i < 3; ++i)
        extent = extent + Vec3{std::fabs(o.axis[i].x), std::fabs(o.axis[i].y), std::fabs(o.axis[i].z)} * o.half[i];
    return {pose.position - extent, pose.position + extent};
}

bool raycastShape(const Shape3D& shape, const Pose& pose, Vec3 origin, Vec3 direction, float maxDistance,
                  float& distance, Vec3& normal)
{
    if (const auto* sphere = std::get_if<SphereShape>(&shape))
    {
        const Vec3 oc = origin - pose.position;
        const float b = oc.dot(direction);
        const float c = oc.dot(oc) - sphere->radius * sphere->radius;
        if (c > 0.f && b > 0.f)
            return false;
        const float discriminant = b * b - c;
        if (discriminant < 0.f)
            return false;
        const float t = -b - std::sqrt(discriminant);
        if (t < 0.f || t > maxDistance)
            return false;
        distance = t;
        normal   = (origin + direction * t - pose.position).normalized();
        return true;
    }

    const auto& box          = std::get<BoxShape>(shape);
    const Quaternion inverse = pose.rotation.conjugate();
    const Vec3 o             = inverse.rotate(origin - pose.position);
    const Vec3 dir           = inverse.rotate(direction);

    float tMin    = 0.f;
    float tMax    = maxDistance;
    int hitAxis   = -1;
    float hitSign = 1.f;
    for (int i = 0; i < 3; ++i)
    {
        const float oi = comp(o, i);
        const float di = comp(dir, i);
        const float h  = comp(box.halfExtents, i);
        if (std::fabs(di) < 1e-8f)
        {
            if (std::fabs(oi) > h)
                return false;
            continue;
        }
        float t1   = (-h - oi) / di;
        float t2   = (h - oi) / di;
        float sign = -1.f;
        if (t1 > t2)
        {
            std::swap(t1, t2);
            sign = 1.f;
        }
        if (t1 > tMin)
        {
            tMin    = t1;
            hitAxis = i;
            hitSign = sign;
        }
        tMax = std::min(tMax, t2);
        if (tMin > tMax)
            return false;
    }
    if (hitAxis < 0)
        return false;

    distance = tMin;
    normal   = pose.rotation.rotate(unitAxis(hitAxis) * hitSign);
    return true;
}

}
