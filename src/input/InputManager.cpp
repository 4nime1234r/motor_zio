#include "zio/input/InputManager.h"

#include <algorithm>
#include <array>

#include "zio/events/EventBus.h"

#if defined(ZIO_PLATFORM_WINDOWS)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <xinput.h>
#else
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#endif

namespace zio::input
{

namespace
{
constexpr std::size_t kKeyCodeCount     = static_cast<std::size_t>(events::KeyCode::Z) + 1;
constexpr std::size_t kMouseButtonCount = static_cast<std::size_t>(events::MouseButton::Extra2) + 1;
}

struct GamepadSlot
{
    bool connected = false;
    std::array<bool, kGamepadButtonCount> down{};
    std::array<bool, kGamepadButtonCount> pressed{};
    std::array<bool, kGamepadButtonCount> released{};
    std::array<float, kGamepadAxisCount> axes{};
};

struct InputManager::Impl
{
    events::EventBus* bus                  = nullptr;
    events::ListenerId keyListener         = events::kInvalidListenerId;
    events::ListenerId mouseButtonListener = events::kInvalidListenerId;
    events::ListenerId mouseMoveListener   = events::kInvalidListenerId;
    events::ListenerId mouseScrollListener = events::kInvalidListenerId;

    std::array<bool, kKeyCodeCount> keysDown{};
    std::array<bool, kKeyCodeCount> keysPressed{};
    std::array<bool, kKeyCodeCount> keysReleased{};

    std::array<bool, kMouseButtonCount> mouseButtonsDown{};
    std::array<bool, kMouseButtonCount> mouseButtonsPressed{};
    std::array<bool, kMouseButtonCount> mouseButtonsReleased{};

    math::Vec2 mousePosition;
    math::Vec2 mouseDelta;
    math::Vec2 mouseScrollDelta;

    std::array<GamepadSlot, kMaxGamepads> gamepads{};

    void wire(events::EventBus& eventBus)
    {
        bus = &eventBus;

        keyListener = bus->onKey(
            [this](const events::KeyEvent& e)
            {
                if (e.code == events::KeyCode::Unknown)
                    return;
                const auto index = static_cast<std::size_t>(e.code);
                if (e.down)
                {
                    if (!keysDown[index])
                        keysPressed[index] = true;
                    keysDown[index] = true;
                }
                else
                {
                    if (keysDown[index])
                        keysReleased[index] = true;
                    keysDown[index] = false;
                }
            });

        mouseButtonListener = bus->onMouseButton(
            [this](const events::MouseButtonEvent& e)
            {
                const auto index = static_cast<std::size_t>(e.button);
                mousePosition     = {e.x, e.y};
                if (e.down)
                {
                    if (!mouseButtonsDown[index])
                        mouseButtonsPressed[index] = true;
                    mouseButtonsDown[index] = true;
                }
                else
                {
                    if (mouseButtonsDown[index])
                        mouseButtonsReleased[index] = true;
                    mouseButtonsDown[index] = false;
                }
            });

        mouseMoveListener = bus->onMouseMove(
            [this](const events::MouseMoveEvent& e)
            {
                mousePosition = {e.x, e.y};
                mouseDelta += math::Vec2{e.dx, e.dy};
            });

        mouseScrollListener = bus->onMouseScroll([this](const events::MouseScrollEvent& e)
                                                  { mouseScrollDelta += math::Vec2{e.scrollX, e.scrollY}; });
    }

    void unwire()
    {
        if (!bus)
            return;
        bus->unsubscribe(keyListener);
        bus->unsubscribe(mouseButtonListener);
        bus->unsubscribe(mouseMoveListener);
        bus->unsubscribe(mouseScrollListener);
    }

    void beginTick()
    {
        keysPressed.fill(false);
        keysReleased.fill(false);
        mouseButtonsPressed.fill(false);
        mouseButtonsReleased.fill(false);
        mouseDelta       = math::Vec2::zero();
        mouseScrollDelta = math::Vec2::zero();
        pollGamepads();
    }

    void pollGamepads();
};

#if defined(ZIO_PLATFORM_WINDOWS)

namespace
{

float normalizeAxis(SHORT raw, SHORT deadzone)
{
    const float value = static_cast<float>(raw);
    const float dz     = static_cast<float>(deadzone);
    if (raw > deadzone)
        return std::min((value - dz) / (32767.f - dz), 1.f);
    if (raw < -deadzone)
        return std::max((value + dz) / (32768.f - dz), -1.f);
    return 0.f;
}

float normalizeTrigger(BYTE raw)
{
    constexpr BYTE kThreshold = XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
    if (raw <= kThreshold)
        return 0.f;
    return (static_cast<float>(raw) - kThreshold) / (255.f - kThreshold);
}

struct ButtonMap
{
    WORD xinputFlag;
    GamepadButton button;
};

constexpr ButtonMap kButtonMap[] = {
    {XINPUT_GAMEPAD_DPAD_UP, GamepadButton::DPadUp},
    {XINPUT_GAMEPAD_DPAD_DOWN, GamepadButton::DPadDown},
    {XINPUT_GAMEPAD_DPAD_LEFT, GamepadButton::DPadLeft},
    {XINPUT_GAMEPAD_DPAD_RIGHT, GamepadButton::DPadRight},
    {XINPUT_GAMEPAD_START, GamepadButton::Start},
    {XINPUT_GAMEPAD_BACK, GamepadButton::Back},
    {XINPUT_GAMEPAD_LEFT_THUMB, GamepadButton::LeftThumb},
    {XINPUT_GAMEPAD_RIGHT_THUMB, GamepadButton::RightThumb},
    {XINPUT_GAMEPAD_LEFT_SHOULDER, GamepadButton::LeftShoulder},
    {XINPUT_GAMEPAD_RIGHT_SHOULDER, GamepadButton::RightShoulder},
    {XINPUT_GAMEPAD_A, GamepadButton::A},
    {XINPUT_GAMEPAD_B, GamepadButton::B},
    {XINPUT_GAMEPAD_X, GamepadButton::X},
    {XINPUT_GAMEPAD_Y, GamepadButton::Y},
};

}

void InputManager::Impl::pollGamepads()
{
    for (int i = 0; i < kMaxGamepads; ++i)
    {
        GamepadSlot& slot = gamepads[static_cast<std::size_t>(i)];

        XINPUT_STATE state{};
        const bool connected = XInputGetState(static_cast<DWORD>(i), &state) == ERROR_SUCCESS;

        if (!connected)
        {
            slot.pressed.fill(false);
            if (slot.connected)
            {
                for (std::size_t b = 0; b < kGamepadButtonCount; ++b)
                    slot.released[b] = slot.down[b];
                slot.down.fill(false);
                slot.axes.fill(0.f);
            }
            else
            {
                slot.released.fill(false);
            }
            slot.connected = false;
            continue;
        }

        slot.connected      = true;
        const WORD buttons = state.Gamepad.wButtons;
        for (const auto& map : kButtonMap)
        {
            const auto index = static_cast<std::size_t>(map.button);
            const bool down    = (buttons & map.xinputFlag) != 0;
            slot.pressed[index]  = down && !slot.down[index];
            slot.released[index] = !down && slot.down[index];
            slot.down[index]     = down;
        }

        slot.axes[static_cast<std::size_t>(GamepadAxis::LeftX)] =
            normalizeAxis(state.Gamepad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        slot.axes[static_cast<std::size_t>(GamepadAxis::LeftY)] =
            normalizeAxis(state.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        slot.axes[static_cast<std::size_t>(GamepadAxis::RightX)] =
            normalizeAxis(state.Gamepad.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
        slot.axes[static_cast<std::size_t>(GamepadAxis::RightY)] =
            normalizeAxis(state.Gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
        slot.axes[static_cast<std::size_t>(GamepadAxis::LeftTrigger)]  = normalizeTrigger(state.Gamepad.bLeftTrigger);
        slot.axes[static_cast<std::size_t>(GamepadAxis::RightTrigger)] =
            normalizeTrigger(state.Gamepad.bRightTrigger);
    }
}

#else

namespace
{

float shapeAxis(float value, float deadzone)
{
    if (value > deadzone)
        return std::min((value - deadzone) / (1.f - deadzone), 1.f);
    if (value < -deadzone)
        return std::max((value + deadzone) / (1.f - deadzone), -1.f);
    return 0.f;
}

float shapeTrigger(float value)
{
    const float normalized = (value + 1.f) * 0.5f;
    constexpr float kThreshold = 0.12f;
    if (normalized <= kThreshold)
        return 0.f;
    return (normalized - kThreshold) / (1.f - kThreshold);
}

struct ButtonMap
{
    int glfwButton;
    GamepadButton button;
};

constexpr ButtonMap kButtonMap[] = {
    {GLFW_GAMEPAD_BUTTON_DPAD_UP, GamepadButton::DPadUp},
    {GLFW_GAMEPAD_BUTTON_DPAD_DOWN, GamepadButton::DPadDown},
    {GLFW_GAMEPAD_BUTTON_DPAD_LEFT, GamepadButton::DPadLeft},
    {GLFW_GAMEPAD_BUTTON_DPAD_RIGHT, GamepadButton::DPadRight},
    {GLFW_GAMEPAD_BUTTON_START, GamepadButton::Start},
    {GLFW_GAMEPAD_BUTTON_BACK, GamepadButton::Back},
    {GLFW_GAMEPAD_BUTTON_LEFT_THUMB, GamepadButton::LeftThumb},
    {GLFW_GAMEPAD_BUTTON_RIGHT_THUMB, GamepadButton::RightThumb},
    {GLFW_GAMEPAD_BUTTON_LEFT_BUMPER, GamepadButton::LeftShoulder},
    {GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER, GamepadButton::RightShoulder},
    {GLFW_GAMEPAD_BUTTON_A, GamepadButton::A},
    {GLFW_GAMEPAD_BUTTON_B, GamepadButton::B},
    {GLFW_GAMEPAD_BUTTON_X, GamepadButton::X},
    {GLFW_GAMEPAD_BUTTON_Y, GamepadButton::Y},
};

}

void InputManager::Impl::pollGamepads()
{
    for (int i = 0; i < kMaxGamepads; ++i)
    {
        GamepadSlot& slot = gamepads[static_cast<std::size_t>(i)];

        GLFWgamepadstate state{};
        const bool connected = glfwJoystickIsGamepad(GLFW_JOYSTICK_1 + i) && glfwGetGamepadState(GLFW_JOYSTICK_1 + i, &state);

        if (!connected)
        {
            slot.pressed.fill(false);
            if (slot.connected)
            {
                for (std::size_t b = 0; b < kGamepadButtonCount; ++b)
                    slot.released[b] = slot.down[b];
                slot.down.fill(false);
                slot.axes.fill(0.f);
            }
            else
            {
                slot.released.fill(false);
            }
            slot.connected = false;
            continue;
        }

        slot.connected = true;
        for (const auto& map : kButtonMap)
        {
            const auto index     = static_cast<std::size_t>(map.button);
            const bool down      = state.buttons[map.glfwButton] == GLFW_PRESS;
            slot.pressed[index]  = down && !slot.down[index];
            slot.released[index] = !down && slot.down[index];
            slot.down[index]     = down;
        }

        slot.axes[static_cast<std::size_t>(GamepadAxis::LeftX)] = shapeAxis(state.axes[GLFW_GAMEPAD_AXIS_LEFT_X], 0.24f);
        slot.axes[static_cast<std::size_t>(GamepadAxis::LeftY)] = shapeAxis(-state.axes[GLFW_GAMEPAD_AXIS_LEFT_Y], 0.24f);
        slot.axes[static_cast<std::size_t>(GamepadAxis::RightX)] = shapeAxis(state.axes[GLFW_GAMEPAD_AXIS_RIGHT_X], 0.27f);
        slot.axes[static_cast<std::size_t>(GamepadAxis::RightY)] = shapeAxis(-state.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y], 0.27f);
        slot.axes[static_cast<std::size_t>(GamepadAxis::LeftTrigger)]  = shapeTrigger(state.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER]);
        slot.axes[static_cast<std::size_t>(GamepadAxis::RightTrigger)] = shapeTrigger(state.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER]);
    }
}

#endif

InputManager::InputManager() : impl_(std::make_unique<Impl>())
{
    impl_->wire(events::EventBus::instance());
}

InputManager::~InputManager()
{
    impl_->unwire();
}

InputManager& InputManager::instance()
{
    static InputManager global;
    return global;
}

void InputManager::update()
{
    impl_->beginTick();
}

bool InputManager::isKeyDown(events::KeyCode code) const
{
    return impl_->keysDown[static_cast<std::size_t>(code)];
}

bool InputManager::isKeyPressed(events::KeyCode code) const
{
    return impl_->keysPressed[static_cast<std::size_t>(code)];
}

bool InputManager::isKeyReleased(events::KeyCode code) const
{
    return impl_->keysReleased[static_cast<std::size_t>(code)];
}

math::Vec2 InputManager::mousePosition() const
{
    return impl_->mousePosition;
}

math::Vec2 InputManager::mouseDelta() const
{
    return impl_->mouseDelta;
}

math::Vec2 InputManager::mouseScrollDelta() const
{
    return impl_->mouseScrollDelta;
}

bool InputManager::isMouseButtonDown(events::MouseButton button) const
{
    return impl_->mouseButtonsDown[static_cast<std::size_t>(button)];
}

bool InputManager::isMouseButtonPressed(events::MouseButton button) const
{
    return impl_->mouseButtonsPressed[static_cast<std::size_t>(button)];
}

bool InputManager::isMouseButtonReleased(events::MouseButton button) const
{
    return impl_->mouseButtonsReleased[static_cast<std::size_t>(button)];
}

bool InputManager::isGamepadConnected(int index) const
{
    if (index < 0 || index >= kMaxGamepads)
        return false;
    return impl_->gamepads[static_cast<std::size_t>(index)].connected;
}

bool InputManager::isGamepadButtonDown(int index, GamepadButton button) const
{
    if (index < 0 || index >= kMaxGamepads)
        return false;
    return impl_->gamepads[static_cast<std::size_t>(index)].down[static_cast<std::size_t>(button)];
}

bool InputManager::isGamepadButtonPressed(int index, GamepadButton button) const
{
    if (index < 0 || index >= kMaxGamepads)
        return false;
    return impl_->gamepads[static_cast<std::size_t>(index)].pressed[static_cast<std::size_t>(button)];
}

bool InputManager::isGamepadButtonReleased(int index, GamepadButton button) const
{
    if (index < 0 || index >= kMaxGamepads)
        return false;
    return impl_->gamepads[static_cast<std::size_t>(index)].released[static_cast<std::size_t>(button)];
}

float InputManager::gamepadAxis(int index, GamepadAxis axis) const
{
    if (index < 0 || index >= kMaxGamepads)
        return 0.f;
    return impl_->gamepads[static_cast<std::size_t>(index)].axes[static_cast<std::size_t>(axis)];
}

}
