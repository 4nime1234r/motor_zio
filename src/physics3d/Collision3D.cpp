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

bool sphereCenters(const Vec3& ca, float ra, const Vec3& cb, float rb, Manifold& m)
{
    const Vec3 d      = cb - ca;
    const float dist  = d.length();
    const float radii = ra + rb;
    if (dist >= radii)
        return false;
    const Vec3 n            = dist > kEpsilon ? d * (1.f / dist) : Vec3::unitY();
    const float pen         = radii - dist;
    m.normal                = n;
    m.count                 = 1;
    m.points[0].point       = ca + n * (ra - pen * 0.5f);
    m.points[0].penetration = pen;
    return true;
}

void segmentEnds(const CapsuleShape& c, const Pose& p, Vec3& a, Vec3& b)
{
    const Vec3 axis = p.rotation.rotate(Vec3::unitY()) * c.halfHeight;
    a               = p.position - axis;
    b               = p.position + axis;
}

Vec3 closestOnSegment(const Vec3& a, const Vec3& b, const Vec3& p)
{
    const Vec3 ab = b - a;
    const float len2 = ab.dot(ab);
    if (len2 < kEpsilon)
        return a;
    const float t = std::clamp((p - a).dot(ab) / len2, 0.f, 1.f);
    return a + ab * t;
}

void closestSegments(const Vec3& p1, const Vec3& q1, const Vec3& p2, const Vec3& q2, Vec3& c1, Vec3& c2)
{
    const Vec3 d1 = q1 - p1;
    const Vec3 d2 = q2 - p2;
    const Vec3 r  = p1 - p2;
    const float a = d1.dot(d1);
    const float e = d2.dot(d2);
    const float f = d2.dot(r);
    float s = 0.f, t = 0.f;
    if (a <= kEpsilon && e <= kEpsilon)
    {
        c1 = p1;
        c2 = p2;
        return;
    }
    if (a <= kEpsilon)
    {
        t = std::clamp(f / e, 0.f, 1.f);
    }
    else
    {
        const float c = d1.dot(r);
        if (e <= kEpsilon)
        {
            s = std::clamp(-c / a, 0.f, 1.f);
        }
        else
        {
            const float b     = d1.dot(d2);
            const float denom = a * e - b * b;
            s                 = denom > kEpsilon ? std::clamp((b * f - c * e) / denom, 0.f, 1.f) : 0.f;
            t                 = (b * s + f) / e;
            if (t < 0.f)
            {
                t = 0.f;
                s = std::clamp(-c / a, 0.f, 1.f);
            }
            else if (t > 1.f)
            {
                t = 1.f;
                s = std::clamp((b - c) / a, 0.f, 1.f);
            }
        }
    }
    c1 = p1 + d1 * s;
    c2 = p2 + d2 * t;
}

void mergeManifold(Manifold& dst, const Manifold& src)
{
    if (src.count == 0)
        return;
    if (dst.count == 0)
    {
        dst = src;
        return;
    }
    if (dst.normal.dot(src.normal) > 0.9f)
    {
        for (int i = 0; i < src.count && dst.count < 4; ++i)
            dst.points[dst.count++] = src.points[i];
        return;
    }
    if (src.points[0].penetration > dst.points[0].penetration)
        dst = src;
}

Vec3 closestOnTriangle(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c)
{
    const Vec3 ab = b - a, ac = c - a, ap = p - a;
    const float d1 = ab.dot(ap), d2 = ac.dot(ap);
    if (d1 <= 0.f && d2 <= 0.f)
        return a;
    const Vec3 bp = p - b;
    const float d3 = ab.dot(bp), d4 = ac.dot(bp);
    if (d3 >= 0.f && d4 <= d3)
        return b;
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.f && d1 >= 0.f && d3 <= 0.f)
        return a + ab * (d1 / (d1 - d3));
    const Vec3 cp = p - c;
    const float d5 = ab.dot(cp), d6 = ac.dot(cp);
    if (d6 >= 0.f && d5 <= d6)
        return c;
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.f && d2 >= 0.f && d6 <= 0.f)
        return a + ac * (d2 / (d2 - d6));
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.f && (d4 - d3) >= 0.f && (d5 - d6) >= 0.f)
        return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    const float denom = 1.f / (va + vb + vc);
    return a + ab * (vb * denom) + ac * (vc * denom);
}

void triangleAt(const MeshShape& mesh, const Pose& pose, std::size_t index, Vec3& a, Vec3& b, Vec3& c)
{
    a = pose.position + pose.rotation.rotate(mesh.data->triangles[index * 3]);
    b = pose.position + pose.rotation.rotate(mesh.data->triangles[index * 3 + 1]);
    c = pose.position + pose.rotation.rotate(mesh.data->triangles[index * 3 + 2]);
}

bool sphereVsMesh(const Vec3& center, float radius, const MeshShape& mesh, const Pose& pose, Manifold& m)
{
    if (!mesh.data)
        return false;
    bool any = false;
    const std::size_t count = mesh.data->triangles.size() / 3;
    for (std::size_t i = 0; i < count; ++i)
    {
        Vec3 a, b, c;
        triangleAt(mesh, pose, i, a, b, c);
        if (center.x + radius < std::min({a.x, b.x, c.x}) || center.x - radius > std::max({a.x, b.x, c.x}) ||
            center.y + radius < std::min({a.y, b.y, c.y}) || center.y - radius > std::max({a.y, b.y, c.y}) ||
            center.z + radius < std::min({a.z, b.z, c.z}) || center.z - radius > std::max({a.z, b.z, c.z}))
            continue;
        const Vec3 closest = closestOnTriangle(center, a, b, c);
        const Vec3 diff    = center - closest;
        const float d2     = diff.dot(diff);
        if (d2 >= radius * radius)
            continue;
        const float dist = std::sqrt(d2);
        Vec3 normal;
        if (dist > kEpsilon)
        {
            normal = diff * (1.f / dist);
        }
        else
        {
            normal = (b - a).cross(c - a).normalized();
        }
        Manifold single;
        single.normal                = normal;
        single.count                 = 1;
        single.points[0].point       = closest;
        single.points[0].penetration = radius - dist;
        mergeManifold(m, single);
        any = true;
    }
    return any;
}

bool boxVsMesh(const BoxShape& box, const Pose& bp, const MeshShape& mesh, const Pose& mp, Manifold& m)
{
    if (!mesh.data)
        return false;
    const Obb obb = makeObb(box, bp);
    Vec3 corners[8];
    for (int i = 0; i < 8; ++i)
        corners[i] = obb.center + obb.axis[0] * ((i & 1) ? obb.half[0] : -obb.half[0]) +
                     obb.axis[1] * ((i & 2) ? obb.half[1] : -obb.half[1]) +
                     obb.axis[2] * ((i & 4) ? obb.half[2] : -obb.half[2]);
    const float maxDepth = std::min({obb.half[0], obb.half[1], obb.half[2]});
    bool any = false;
    const std::size_t count = mesh.data->triangles.size() / 3;
    for (std::size_t i = 0; i < count; ++i)
    {
        Vec3 a, b, c;
        triangleAt(mesh, mp, i, a, b, c);
        const Vec3 normal = (b - a).cross(c - a).normalized();
        for (const Vec3& corner : corners)
        {
            const float d = normal.dot(corner - a);
            if (d > 0.f || d < -maxDepth)
                continue;
            const Vec3 projected = corner - normal * d;
            const Vec3 closest   = closestOnTriangle(projected, a, b, c);
            if ((closest - projected).length() > 1e-3f)
                continue;
            Manifold single;
            single.normal                = normal;
            single.count                 = 1;
            single.points[0].point       = projected;
            single.points[0].penetration = -d;
            mergeManifold(m, single);
            any = true;
        }
    }
    return any;
}

bool capsuleVsMesh(const CapsuleShape& capsule, const Pose& cp, const MeshShape& mesh, const Pose& mp, Manifold& m)
{
    Vec3 a, b;
    segmentEnds(capsule, cp, a, b);
    bool any = false;
    for (const Vec3& p : {a, (a + b) * 0.5f, b})
        any = sphereVsMesh(p, capsule.radius, mesh, mp, m) || any;
    return any;
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

namespace
{

void flipNormal(Manifold& m)
{
    m.normal = m.normal * -1.f;
}

bool boxVsCapsule(const BoxShape& box, const Pose& bp, const CapsuleShape& capsule, const Pose& cp, Manifold& out)
{
    Vec3 a, b;
    segmentEnds(capsule, cp, a, b);
    const SphereShape sphere{capsule.radius};
    Manifold ma, mb;
    const bool hitA = sphereVsBox(sphere, Pose{a, Quaternion::identity()}, box, bp, ma);
    const bool hitB = sphereVsBox(sphere, Pose{b, Quaternion::identity()}, box, bp, mb);
    if (hitA)
        mergeManifold(out, ma);
    if (hitB)
        mergeManifold(out, mb);
    if (hitA || hitB)
        return true;

    Vec3 q = (a + b) * 0.5f;
    const Quaternion inverse = bp.rotation.conjugate();
    for (int i = 0; i < 12; ++i)
    {
        const Vec3 local = inverse.rotate(q - bp.position);
        const Vec3 he    = box.halfExtents;
        const Vec3 onBox = bp.position + bp.rotation.rotate({std::clamp(local.x, -he.x, he.x),
                                                             std::clamp(local.y, -he.y, he.y),
                                                             std::clamp(local.z, -he.z, he.z)});
        q = closestOnSegment(a, b, onBox);
    }
    Manifold mid;
    if (sphereVsBox(sphere, Pose{q, Quaternion::identity()}, box, bp, mid))
    {
        mergeManifold(out, mid);
        return true;
    }
    return false;
}

}

bool collide(const Shape3D& a, const Pose& pa, const Shape3D& b, const Pose& pb, Manifold& out)
{
    out = Manifold{};
    const auto* sphereA  = std::get_if<SphereShape>(&a);
    const auto* sphereB  = std::get_if<SphereShape>(&b);
    const auto* boxA     = std::get_if<BoxShape>(&a);
    const auto* boxB     = std::get_if<BoxShape>(&b);
    const auto* capsuleA = std::get_if<CapsuleShape>(&a);
    const auto* capsuleB = std::get_if<CapsuleShape>(&b);
    const auto* meshA    = std::get_if<MeshShape>(&a);
    const auto* meshB    = std::get_if<MeshShape>(&b);

    if (meshA && meshB)
        return false;

    if (sphereA && sphereB)
        return sphereSphere(*sphereA, pa, *sphereB, pb, out);
    if (sphereA && boxB)
    {
        const bool hit = sphereVsBox(*sphereA, pa, *boxB, pb, out);
        if (hit)
            flipNormal(out);
        return hit;
    }
    if (boxA && sphereB)
        return sphereVsBox(*sphereB, pb, *boxA, pa, out);
    if (boxA && boxB)
        return boxBox(*boxA, pa, *boxB, pb, out);

    if (sphereA && capsuleB)
    {
        Vec3 s, e;
        segmentEnds(*capsuleB, pb, s, e);
        return sphereCenters(pa.position, sphereA->radius, closestOnSegment(s, e, pa.position), capsuleB->radius, out);
    }
    if (capsuleA && sphereB)
    {
        Vec3 s, e;
        segmentEnds(*capsuleA, pa, s, e);
        return sphereCenters(closestOnSegment(s, e, pb.position), capsuleA->radius, pb.position, sphereB->radius, out);
    }
    if (capsuleA && capsuleB)
    {
        Vec3 s1, e1, s2, e2, c1, c2;
        segmentEnds(*capsuleA, pa, s1, e1);
        segmentEnds(*capsuleB, pb, s2, e2);
        closestSegments(s1, e1, s2, e2, c1, c2);
        return sphereCenters(c1, capsuleA->radius, c2, capsuleB->radius, out);
    }
    if (boxA && capsuleB)
        return boxVsCapsule(*boxA, pa, *capsuleB, pb, out);
    if (capsuleA && boxB)
    {
        const bool hit = boxVsCapsule(*boxB, pb, *capsuleA, pa, out);
        if (hit)
            flipNormal(out);
        return hit;
    }

    if (meshB)
    {
        bool hit = false;
        if (sphereA)
            hit = sphereVsMesh(pa.position, sphereA->radius, *meshB, pb, out);
        else if (boxA)
            hit = boxVsMesh(*boxA, pa, *meshB, pb, out);
        else if (capsuleA)
            hit = capsuleVsMesh(*capsuleA, pa, *meshB, pb, out);
        if (hit)
            flipNormal(out);
        return hit;
    }
    if (meshA)
    {
        bool hit = false;
        if (sphereB)
            hit = sphereVsMesh(pb.position, sphereB->radius, *meshA, pa, out);
        else if (boxB)
            hit = boxVsMesh(*boxB, pb, *meshA, pa, out);
        else if (capsuleB)
            hit = capsuleVsMesh(*capsuleB, pb, *meshA, pa, out);
        return hit;
    }
    return false;
}

Aabb computeAabb(const Shape3D& shape, const Pose& pose)
{
    if (const auto* sphere = std::get_if<SphereShape>(&shape))
    {
        const Vec3 r{sphere->radius, sphere->radius, sphere->radius};
        return {pose.position - r, pose.position + r};
    }

    if (const auto* capsule = std::get_if<CapsuleShape>(&shape))
    {
        Vec3 a, b;
        segmentEnds(*capsule, pose, a, b);
        const Vec3 r{capsule->radius, capsule->radius, capsule->radius};
        return {Vec3{std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)} - r,
                Vec3{std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)} + r};
    }
    if (const auto* mesh = std::get_if<MeshShape>(&shape))
    {
        if (!mesh->data || mesh->data->triangles.empty())
            return {pose.position, pose.position};
        Vec3 lo = pose.position, hi = pose.position;
        bool first = true;
        for (int i = 0; i < 8; ++i)
        {
            const Vec3 local{(i & 1) ? mesh->data->boundsMax.x : mesh->data->boundsMin.x,
                             (i & 2) ? mesh->data->boundsMax.y : mesh->data->boundsMin.y,
                             (i & 4) ? mesh->data->boundsMax.z : mesh->data->boundsMin.z};
            const Vec3 w = pose.position + pose.rotation.rotate(local);
            if (first)
            {
                lo = hi = w;
                first   = false;
            }
            lo = {std::min(lo.x, w.x), std::min(lo.y, w.y), std::min(lo.z, w.z)};
            hi = {std::max(hi.x, w.x), std::max(hi.y, w.y), std::max(hi.z, w.z)};
        }
        return {lo, hi};
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

    if (const auto* capsule = std::get_if<CapsuleShape>(&shape))
    {
        const Quaternion inv = pose.rotation.conjugate();
        const Vec3 o         = inv.rotate(origin - pose.position);
        const Vec3 d         = inv.rotate(direction);
        float best           = maxDistance;
        bool hit             = false;
        Vec3 bestNormal;
        const auto sphereHit = [&](float cy) {
            const Vec3 oc = o - Vec3{0.f, cy, 0.f};
            const float b = oc.dot(d);
            const float c = oc.dot(oc) - capsule->radius * capsule->radius;
            const float disc = b * b - c;
            if (disc < 0.f)
                return;
            const float t = -b - std::sqrt(disc);
            if (t >= 0.f && t <= best)
            {
                const Vec3 p = o + d * t;
                if ((cy > 0.f && p.y >= cy) || (cy < 0.f && p.y <= cy))
                {
                    best       = t;
                    bestNormal = (p - Vec3{0.f, cy, 0.f}).normalized();
                    hit        = true;
                }
            }
        };
        const float a = d.x * d.x + d.z * d.z;
        if (a > 1e-10f)
        {
            const float b    = o.x * d.x + o.z * d.z;
            const float c    = o.x * o.x + o.z * o.z - capsule->radius * capsule->radius;
            const float disc = b * b - a * c;
            if (disc >= 0.f)
            {
                const float t = (-b - std::sqrt(disc)) / a;
                if (t >= 0.f && t <= best)
                {
                    const Vec3 p = o + d * t;
                    if (std::fabs(p.y) <= capsule->halfHeight)
                    {
                        best       = t;
                        bestNormal = Vec3{p.x, 0.f, p.z}.normalized();
                        hit        = true;
                    }
                }
            }
        }
        sphereHit(capsule->halfHeight);
        sphereHit(-capsule->halfHeight);
        if (!hit)
            return false;
        distance = best;
        normal   = pose.rotation.rotate(bestNormal);
        return true;
    }
    if (const auto* mesh = std::get_if<MeshShape>(&shape))
    {
        if (!mesh->data)
            return false;
        float best = maxDistance;
        bool hit   = false;
        Vec3 bestNormal;
        const std::size_t count = mesh->data->triangles.size() / 3;
        for (std::size_t i = 0; i < count; ++i)
        {
            Vec3 a, b, c;
            triangleAt(*mesh, pose, i, a, b, c);
            const Vec3 e1 = b - a, e2 = c - a;
            const Vec3 p  = direction.cross(e2);
            const float det = e1.dot(p);
            if (std::fabs(det) < 1e-9f)
                continue;
            const float inv = 1.f / det;
            const Vec3 tv   = origin - a;
            const float u   = tv.dot(p) * inv;
            if (u < 0.f || u > 1.f)
                continue;
            const Vec3 q  = tv.cross(e1);
            const float v = direction.dot(q) * inv;
            if (v < 0.f || u + v > 1.f)
                continue;
            const float t = e2.dot(q) * inv;
            if (t < 0.f || t > best)
                continue;
            best       = t;
            bestNormal = e1.cross(e2).normalized();
            if (bestNormal.dot(direction) > 0.f)
                bestNormal = bestNormal * -1.f;
            hit = true;
        }
        if (!hit)
            return false;
        distance = best;
        normal   = bestNormal;
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
