#pragma once

#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "core/EditorCamera.h"
#include "core/EditorContext.h"
#include "core/Gizmo.h"
#include "core/Picking.h"
#include "ui/ToolButton.h"
#include "zio/math/Quaternion.h"
#include "zio/math/Vec3.h"
#include "zio/ui/Containers.h"
#include "zio/ui/Viewport.h"

namespace zio_editor
{

class ViewportArea final : public zio::ui::Widget
{
public:
    ViewportArea();

    zio::ui::ViewportWidget* viewport() const { return viewport_; }
    zio::ui::Panel* toolbar() const { return toolbar_; }

    void layout() override;

private:
    zio::ui::ViewportWidget* viewport_ = nullptr;
    zio::ui::Panel* toolbar_           = nullptr;
};

class ViewportController
{
public:
    explicit ViewportController(EditorContext& context);

    std::unique_ptr<zio::ui::Widget> build();
    void setContextMenuHandler(std::function<void(zio::ui::Vec2)> handler) { onContextMenu_ = std::move(handler); }

    void update(float dt, bool keysBlocked);
    void render(float dt);

    GizmoMode mode() const { return mode_; }
    void setMode(GizmoMode mode) { mode_ = mode; }
    void focusSelection();
    bool dragging() const { return gizmoDrag_.active() || rotateDrag_.active() || cameraDrag_ != CameraDrag::None; }
    bool hovered() const { return area_ && area_->viewport() && area_->viewport()->hovered(); }
    bool focused() const { return area_ && area_->viewport() && area_->viewport()->focused(); }

private:
    enum class CameraDrag
    {
        None,
        Orbit,
        Pan
    };

    int activeAxis() const
    {
        return gizmoDrag_.active() ? gizmoDrag_.axis() : (rotateDrag_.active() ? rotateDrag_.axis() : -1);
    }

    zio::ui::Rect viewRect() const;
    zio::scene3d::Camera currentCamera() const;
    Ray rayAt(zio::ui::Vec2 position) const;
    void onDown(const zio::ui::MouseEvent& event);
    void onUp(const zio::ui::MouseEvent& event);
    void onMove(zio::ui::Vec2 position, zio::ui::Vec2 delta);
    void onScroll(zio::ui::Vec2 delta);
    bool beginGizmoDrag(zio::scene3d::Node3D& node, const GizmoAxes& axes, int axis, const Ray& ray);
    void applyGizmoDrag(const Ray& ray, bool snap);
    void endGizmoDrag();
    void updateHoveredAxis();
    void syncToolbar();

    EditorContext& context_;
    ViewportArea* area_ = nullptr;
    EditorCamera camera_;
    std::uint64_t seenRevision_ = static_cast<std::uint64_t>(-1);
    CameraDrag cameraDrag_      = CameraDrag::None;
    zio::ui::Vec2 pressPosition_;
    zio::ui::Vec2 mouse_;
    bool menuMoved_   = false;
    GizmoMode mode_   = GizmoMode::Move;
    GizmoDrag gizmoDrag_;
    RotateDrag rotateDrag_;
    zio::scene3d::Node3D* dragNode_ = nullptr;
    int hoveredAxis_                = -1;
    zio::math::Quaternion startRotation_;
    zio::math::Vec3 startScale_;
    zio::math::Vec3 dragOrigin_;
    zio::math::Vec3 dragDirection_;
    float startLength_ = 1.f;
    std::vector<std::pair<zio::scene3d::Node3D*, zio::math::Vec3>> groupStart_;
    std::vector<ToolButton*> toolButtons_;
    ToolButton* focusButton_ = nullptr;
    std::function<void(zio::ui::Vec2)> onContextMenu_;
};

}
