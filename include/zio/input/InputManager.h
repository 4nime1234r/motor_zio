#pragma once

#include <memory>

#include "zio/core/Platform.h"
#include "zio/events/Types.h"
#include "zio/input/Types.h"
#include "zio/math/Vec2.h"

namespace zio::input
{

class ZIO_API InputManager
{
public:
    InputManager();
    ~InputManager();

    InputManager(const InputManager&)            = delete;
    InputManager& operator=(const InputManager&) = delete;

    static InputManager& instance();

    void update();

    bool isKeyDown(events::KeyCode code) const;
    bool isKeyPressed(events::KeyCode code) const;
    bool isKeyReleased(events::KeyCode code) const;

    math::Vec2 mousePosition() const;
    math::Vec2 mouseDelta() const;
    math::Vec2 mouseScrollDelta() const;
    bool isMouseButtonDown(events::MouseButton button) const;
    bool isMouseButtonPressed(events::MouseButton button) const;
    bool isMouseButtonReleased(events::MouseButton button) const;

    bool isGamepadConnected(int index) const;
    bool isGamepadButtonDown(int index, GamepadButton button) const;
    bool isGamepadButtonPressed(int index, GamepadButton button) const;
    bool isGamepadButtonReleased(int index, GamepadButton button) const;
    float gamepadAxis(int index, GamepadAxis axis) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
