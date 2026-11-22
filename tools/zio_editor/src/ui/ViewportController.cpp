#include "ui/ViewportController.h"

#include <algorithm>
#include <cmath>

#include "core/SelectionGizmo.h"
#include "core/ViewportGrid.h"
#include "zio/events/Types.h"
#include "zio/input/InputManager.h"
#include "zio/scene3d/Renderer3D.h"

namespace zio_editor
{

namespace
{

constexpr float kSnapStep            = 0.25f;
constexpr float kFastMultiplier      = 3.f;
constexpr float kMinFlySpeed         = 0.5f;
constexpr float kFlySpeedPerDistance = 0.6f;
constexpr float kMaxFrameTime        = 0.1f;
constexpr float kFocusDistance       = 2.5f;
constexpr float kMinFocus            = 1.5f;
constexpr float kMenuClickRadius     = 4.f;
constexpr float kRotateSnapRadians   = 0.2617994f;
constexpr float kScaleSnapStep       = 0.1f;
constexpr float kMinScaleFactor      = 0.01f;
constexpr float kRadiansToDegrees    = 57.2957795f;

bool ctrlDown()
{
    using zio::events::KeyCode;
    auto& input = zio::input::InputManager::instance();
    return input.isKeyDown(KeyCode::LeftCtrl) || input.isKeyDown(KeyCode::RightCtrl);
}

}

ViewportArea::ViewportArea()
{
    setMouseTransparent(true);
    viewport_ = add<zio::ui::ViewportWidget>();
    toolbar_ = add<zio::ui::Panel>();
    toolbar_->setBackground(zio::ui::Color{0.11f, 0.12f, 0.16f, 0.9f});
    toolbar_->setBorder(zio::ui::rgb(0xffffff, 0.12f), 1.f);
    toolbar_->setRadius(9.f);
    toolbar_->setPadding(zio::ui::Edges::all(5.f));
}

void ViewportArea::layout()
{
    viewport_->setRect(rect());
    const zio::ui::Vec2 size = toolbar_->minSize();
    toolbar_->setRect({rect().x + 12.f, rect().y + 12.f, size.x, size.y});
}

ViewportController::ViewportController(EditorContext& context) : context_(context) {}

std::unique_ptr<zio::ui::Widget> ViewportController::build()
{
    auto area = std::make_unique<ViewportArea>();
    area_     = area.get();

    auto* viewport = area_->viewport();
    viewport->setOnMouseDown([this](const zio::ui::MouseEvent& e) { onDown(e); });
    viewport->setOnMouseUp([this](const zio::ui::MouseEvent& e) { onUp(e); });
    viewport->setOnMouseMove([this](zio::ui::Vec2 p, zio::ui::Vec2 d) { onMove(p, d); });
    viewport->setOnScroll([this](zio::ui::Vec2 d, zio::ui::Modifiers) { onScroll(d); });

    auto* row = area_->toolbar()->add<zio::ui::HBox>();
    row->setSeparation(4.f);
    const std::pair<ToolIcon, GizmoMode> tools[] = {{ToolIcon::Move, GizmoMode::Move},
                                                     {ToolIcon::Rotate, GizmoMode::Rotate},
                                                     {ToolIcon::Scale, GizmoMode::Scale}};
    const char* tips[] = {"Mover (1)", "Rotar (2)", "Escalar (3)"};
    int index          = 0;
    for (const auto& [icon, gizmo] : tools)
    {
        auto* button = row->add<ToolButton>(icon, 34.f);
        button->setTooltip(tips[index++]);
        button->setOnClick([this, gizmo = gizmo]() { mode_ = gizmo; });
        toolButtons_.push_back(button);
    }
    focusButton_ = row->add<ToolButton>(ToolIcon::Focus, 34.f);
    focusButton_->setTooltip("Enfocar la selección (F)");
    focusButton_->setOnClick([this]() { focusSelection(); });
    return area;
}

zio::ui::Rect ViewportController::viewRect() const
{
    return area_ && area_->viewport() ? area_->viewport()->viewportRect() : zio::ui::Rect{};
}

zio::scene3d::Camera ViewportController::currentCamera() const
{
    return camera_.toCamera(context_.scene().camera());
}

Ray ViewportController::rayAt(zio::ui::Vec2 position) const
{
    const zio::ui::Rect rect = viewRect();
    const float width        = std::max(rect.w, 1.f);
    const float height       = std::max(rect.h, 1.f);
    const float x            = position.x - rect.x;
    const float y            = position.y - rect.y;
    return makeRay(currentCamera(), width / height, 2.f * x / width - 1.f, 1.f - 2.f * y / height);
}

void ViewportController::focusSelection()
{
    const zio::scene3d::Node3D* selected = context_.selected();
    if (!selected)
        return;
    zio::math::Vec3 center;
    float radius = 1.f;
    selectionFocus(*selected, center, radius);
    camera_.focus(center, std::max(radius * kFocusDistance, kMinFocus));
}

void ViewportController::syncToolbar()
{
    const bool playing = context_.playing();
    area_->toolbar()->setVisible(!playing);
    const GizmoMode modes[] = {GizmoMode::Move, GizmoMode::Rotate, GizmoMode::Scale};
    for (std::size_t i = 0; i < toolButtons_.size(); ++i)
        toolButtons_[i]->setActive(mode_ == modes[i]);
    focusButton_->setEnabled(context_.selected() != nullptr);
}

void ViewportController::onDown(const zio::ui::MouseEvent& event)
{
    if (context_.playing())
        return;
    mouse_ = event.position;

    if (event.button == zio::events::MouseButton::Right)
    {
        cameraDrag_    = CameraDrag::Orbit;
        pressPosition_ = event.position;
        menuMoved_     = false;
        return;
    }
    if (event.button == zio::events::MouseButton::Middle)
    {
        cameraDrag_ = CameraDrag::Pan;
        return;
    }
    if (event.button != zio::events::MouseButton::Left)
        return;

    const Ray ray                     = rayAt(event.position);
    zio::scene3d::Node3D* selected    = context_.selected();
    const zio::scene3d::Camera camera = currentCamera();
    if (selected && hoveredAxis_ >= 0)
    {
        const GizmoAxes axes = computeGizmoAxes(*selected, camera.position);
        if (beginGizmoDrag(*selected, axes, hoveredAxis_, ray))
            return;
    }

    zio::scene3d::Node3D* hit = pickNode(context_.scene().root(), ray);
    if (event.modifiers.ctrl)
        context_.toggleSelect(hit);
    else
        context_.select(hit);
}

void ViewportController::onUp(const zio::ui::MouseEvent& event)
{
    if (event.button == zio::events::MouseButton::Right && cameraDrag_ == CameraDrag::Orbit)
    {
        cameraDrag_ = CameraDrag::None;
        if (!menuMoved_ && !context_.playing() && !context_.hasPending())
        {
            if (zio::scene3d::Node3D* hit = pickNode(context_.scene().root(), rayAt(event.position)))
                context_.select(hit);
            if (onContextMenu_)
                onContextMenu_(event.position);
        }
        return;
    }
    if (event.button == zio::events::MouseButton::Middle)
        cameraDrag_ = CameraDrag::None;
    if (event.button == zio::events::MouseButton::Left)
        endGizmoDrag();
}

void ViewportController::onMove(zio::ui::Vec2 position, zio::ui::Vec2 delta)
{
    mouse_ = position;
    if (context_.playing())
        return;

    if (cameraDrag_ == CameraDrag::Orbit)
    {
        camera_.orbit(delta.x, delta.y);
        const zio::ui::Vec2 moved = position - pressPosition_;
        if (moved.length() >= kMenuClickRadius)
            menuMoved_ = true;
    }
    else if (cameraDrag_ == CameraDrag::Pan)
    {
        camera_.pan(delta.x, delta.y);
    }
    else if (gizmoDrag_.active() || rotateDrag_.active())
    {
        applyGizmoDrag(rayAt(position), ctrlDown());
    }
}

void ViewportController::onScroll(zio::ui::Vec2 delta)
{
    if (!context_.playing())
        camera_.zoom(delta.y);
}

bool ViewportController::beginGizmoDrag(zio::scene3d::Node3D& node, const GizmoAxes& axes, int axis, const Ray& ray)
{
    const zio::math::Vec3 direction = axes.direction[axis];
    bool started                    = false;
    if (mode_ == GizmoMode::Rotate)
    {
        started        = rotateDrag_.begin(axis, axes.origin, direction, ray);
        startRotation_ = node.rotation();
    }
    else
    {
        started        = gizmoDrag_.begin(axis, axes.origin, direction, ray);
        startScale_    = node.scale();
        startLength_   = axes.length;
        dragOrigin_    = axes.origin;
        dragDirection_ = direction;
    }
    groupStart_.clear();
    if (started && mode_ == GizmoMode::Move)
    {
        const auto roots = context_.selectionRoots();
        if (std::find(roots.begin(), roots.end(), &node) != roots.end())
            for (zio::scene3d::Node3D* other : roots)
                if (other != &node)
                    groupStart_.emplace_back(other, other->worldMatrix().transformPoint(zio::math::Vec3::zero()));
    }
    if (started)
        dragNode_ = &node;
    return started;
}

void ViewportController::endGizmoDrag()
{
    gizmoDrag_.end();
    rotateDrag_.end();
    dragNode_ = nullptr;
    groupStart_.clear();
}

void ViewportController::applyGizmoDrag(const Ray& ray, bool snap)
{
    if (!dragNode_)
        return;

    if (mode_ == GizmoMode::Rotate)
    {
        if (const auto angle = rotateDrag_.update(ray, snap ? kRotateSnapRadians : 0.f))
        {
            zio::math::Vec3 unit = zio::math::Vec3::unitX();
            if (rotateDrag_.axis() == 1)
                unit = zio::math::Vec3::unitY();
            else if (rotateDrag_.axis() == 2)
                unit = zio::math::Vec3::unitZ();
            dragNode_->setRotation(
                (startRotation_ * zio::math::Quaternion::fromAxisAngle(unit, *angle * kRadiansToDegrees)).normalized());
            context_.markDirty();
        }
        return;
    }

    const auto position = gizmoDrag_.update(ray, mode_ == GizmoMode::Move && snap ? kSnapStep : 0.f);
    if (!position)
        return;

    if (mode_ == GizmoMode::Move)
    {
        dragNode_->setPosition(localPositionFor(*dragNode_, *position));
        const zio::math::Vec3 delta = *position - dragOrigin_;
        for (const auto& [other, start] : groupStart_)
            other->setPosition(localPositionFor(*other, start + delta));
    }
    else
    {
        float factor = 1.f + (*position - dragOrigin_).dot(dragDirection_) / startLength_;
        if (snap)
            factor = std::round(factor / kScaleSnapStep) * kScaleSnapStep;
        factor = std::max(factor, kMinScaleFactor);

        zio::math::Vec3 scale = startScale_;
        switch (gizmoDrag_.axis())
        {
        case 0:
            scale.x *= factor;
            break;
        case 1:
            scale.y *= factor;
            break;
        default:
            scale.z *= factor;
            break;
        }
        dragNode_->setScale(scale);
    }
    context_.markDirty();
}

void ViewportController::updateHoveredAxis()
{
    hoveredAxis_ = -1;
    if (gizmoDrag_.active() || rotateDrag_.active())
    {
        hoveredAxis_ = activeAxis();
        return;
    }
    zio::scene3d::Node3D* selected = context_.selected();
    if (!selected || !hovered() || cameraDrag_ != CameraDrag::None || context_.playing())
        return;

    const zio::ui::Rect rect          = viewRect();
    const zio::scene3d::Camera camera = currentCamera();
    const GizmoAxes axes              = computeGizmoAxes(*selected, camera.position);
    const float x                     = mouse_.x - rect.x;
    const float y                     = mouse_.y - rect.y;
    hoveredAxis_ = mode_ == GizmoMode::Rotate ? hitTestRings(camera, rect.w, rect.h, axes, x, y)
                                              : hitTestGizmo(camera, rect.w, rect.h, axes, x, y);
}

void ViewportController::update(float dt, bool keysBlocked)
{
    if (!area_)
        return;

    if (context_.sceneRevision() != seenRevision_)
    {
        camera_.resetFrom(context_.scene().camera());
        seenRevision_ = context_.sceneRevision();
        cameraDrag_   = CameraDrag::None;
        endGizmoDrag();
    }

    if (dragNode_ && (!context_.selected() || context_.selected() != dragNode_))
        endGizmoDrag();
    if (context_.playing() && (gizmoDrag_.active() || rotateDrag_.active()))
        endGizmoDrag();

    syncToolbar();
    updateHoveredAxis();

    using zio::events::KeyCode;
    if (keysBlocked || context_.playing() || !(hovered() || focused()))
        return;

    auto& input = zio::input::InputManager::instance();
    if (ctrlDown())
        return;

    float forward = 0.f, right = 0.f, up = 0.f;
    if (input.isKeyDown(KeyCode::W))
        forward += 1.f;
    if (input.isKeyDown(KeyCode::S))
        forward -= 1.f;
    if (input.isKeyDown(KeyCode::D))
        right += 1.f;
    if (input.isKeyDown(KeyCode::A))
        right -= 1.f;
    if (input.isKeyDown(KeyCode::E))
        up += 1.f;
    if (input.isKeyDown(KeyCode::Q))
        up -= 1.f;
    if (forward == 0.f && right == 0.f && up == 0.f)
        return;

    const bool fast   = input.isKeyDown(KeyCode::LeftShift) || input.isKeyDown(KeyCode::RightShift);
    const float speed = std::max(kMinFlySpeed, camera_.distance() * kFlySpeedPerDistance) *
                        (fast ? kFastMultiplier : 1.f) * std::min(dt, kMaxFrameTime);
    camera_.fly(forward * speed, right * speed, up * speed);
}

namespace
{

void drawLightMarkers(zio::scene3d::Renderer3D& renderer, const zio::scene3d::Node3D& node)
{
    if (!node.isVisible())
        return;
    if (const auto* light = dynamic_cast<const zio::scene3d::LightNode*>(&node))
    {
        const zio::math::Mat4& w = light->worldMatrix();
        const zio::math::Vec3 p{w.m[0][3], w.m[1][3], w.m[2][3]};
        const zio::renderer::Color c = {light->color().r, light->color().g, light->color().b, 1.f};
        const float s                = 0.25f;
        renderer.drawLine(p - zio::math::Vec3{s, 0.f, 0.f}, p + zio::math::Vec3{s, 0.f, 0.f}, c, 2.f);
        renderer.drawLine(p - zio::math::Vec3{0.f, s, 0.f}, p + zio::math::Vec3{0.f, s, 0.f}, c, 2.f);
        renderer.drawLine(p - zio::math::Vec3{0.f, 0.f, s}, p + zio::math::Vec3{0.f, 0.f, s}, c, 2.f);
        if (light->lightType() == zio::scene3d::LightType::Spot)
        {
            const zio::math::Vec3 dir = w.transformVector({0.f, -1.f, 0.f}).normalized();
            renderer.drawLine(p, p + dir * std::min(light->range(), 3.f), c, 2.f);
        }
    }
    for (const auto& child : node.children())
        drawLightMarkers(renderer, *child);
}

}

void ViewportController::render(float dt)
{
    if (!area_)
        return;
    const zio::ui::Rect rect = viewRect();
    auto& scene              = context_.scene();
    auto& renderer           = zio::scene3d::Renderer3D::instance();

    if (context_.playing())
        scene.update(std::min(dt, kMaxFrameTime));
    else
        scene.update(0.f);

    if (rect.w < 1.f || rect.h < 1.f)
        return;

    const auto x = static_cast<std::uint32_t>(rect.x);
    const auto y = static_cast<std::uint32_t>(rect.y);
    const auto w = static_cast<std::uint32_t>(rect.w);
    const auto h = static_cast<std::uint32_t>(rect.h);

    if (context_.playing())
    {
        renderer.begin(scene.camera(), scene.light(), w, h, x, y);
        scene.root().visit(renderer);
        renderer.end();
        return;
    }

    const zio::scene3d::Camera camera = currentCamera();
    renderer.begin(camera, scene.light(), w, h, x, y);
    drawGrid(renderer, camera_.target(), camera_.distance());
    scene.root().visit(renderer);
    drawLightMarkers(renderer, scene.root());
    for (const zio::scene3d::Node3D* node : context_.selection())
        if (node != context_.selected())
            drawSelectionBox(renderer, *node);
    if (const zio::scene3d::Node3D* selected = context_.selected())
        drawSelection(renderer, *selected, camera.position, hoveredAxis_, activeAxis(), mode_);
    renderer.end();
}

}
