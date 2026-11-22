#include "core/Picking.h"

#include <cmath>
#include <limits>

namespace zio_editor
{

namespace
{

constexpr float kEpsilon = 1e-9f;
constexpr float kMinHit  = 1e-4f;

struct Best
{
    zio::scene3d::Node3D* node = nullptr;
    float distance             = std::numeric_limits<float>::max();
};

void visit(zio::scene3d::Node3D& node, const Ray& ray, Best& best)
{
    if (!node.isVisible())
        return;

    if (auto* meshNode = dynamic_cast<zio::scene3d::MeshNode*>(&node))
    {
        if (const auto& mesh = meshNode->mesh())
        {
            const zio::math::Mat4& world = node.worldMatrix();
            const auto& vertices         = mesh->vertices;
            for (std::size_t i = 0; i + 2 < vertices.size(); i += 3)
            {
                float t = 0.f;
                if (intersectTriangle(ray, world.transformPoint(vertices[i].position),
                                      world.transformPoint(vertices[i + 1].position),
                                      world.transformPoint(vertices[i + 2].position), t) &&
                    t < best.distance)
                {
                    best.distance = t;
                    best.node     = &node;
                }
            }
        }
    }

    for (const auto& child : node.children())
        visit(*child, ray, best);
}

}

Ray makeRay(const zio::scene3d::Camera& camera, float aspect, float ndcX, float ndcY)
{
    const zio::math::Vec3 forward = (camera.target - camera.position).normalized();
    const zio::math::Vec3 right   = forward.cross(camera.up).normalized();
    const zio::math::Vec3 up      = right.cross(forward);
    const float tanHalf           = std::tan(camera.fovYDegrees * (3.14159265358979323846f / 180.f) * 0.5f);

    Ray ray;
    if (camera.orthographic)
    {
        ray.origin    = camera.position + right * (ndcX * camera.orthoSize * aspect) + up * (ndcY * camera.orthoSize);
        ray.direction = forward;
        return ray;
    }
    ray.origin    = camera.position;
    ray.direction = (forward + right * (ndcX * tanHalf * aspect) + up * (ndcY * tanHalf)).normalized();
    return ray;
}

bool intersectTriangle(const Ray& ray, zio::math::Vec3 a, zio::math::Vec3 b, zio::math::Vec3 c, float& distance)
{
    const zio::math::Vec3 e1 = b - a;
    const zio::math::Vec3 e2 = c - a;
    const zio::math::Vec3 h  = ray.direction.cross(e2);
    const float det          = e1.dot(h);
    if (std::fabs(det) < kEpsilon)
        return false;

    const float inv          = 1.f / det;
    const zio::math::Vec3 s  = ray.origin - a;
    const float u            = inv * s.dot(h);
    if (u < 0.f || u > 1.f)
        return false;

    const zio::math::Vec3 q = s.cross(e1);
    const float v           = inv * ray.direction.dot(q);
    if (v < 0.f || u + v > 1.f)
        return false;

    const float t = inv * e2.dot(q);
    if (!(t > kMinHit))
        return false;
    distance = t;
    return true;
}

zio::scene3d::Node3D* pickNode(zio::scene3d::Node3D& root, const Ray& ray, float* distance)
{
    Best best;
    visit(root, ray, best);
    if (best.node && distance)
        *distance = best.distance;
    return best.node;
}

}
