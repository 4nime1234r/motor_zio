#pragma once

#include <cstddef>
#include <cstdint>

namespace zio::input
{

inline constexpr int kMaxGamepads = 4;

enum class GamepadButton : std::uint8_t
{
    DPadUp = 0,
    DPadDown,
    DPadLeft,
    DPadRight,
    Start,
    Back,
    LeftThumb,
    RightThumb,
    LeftShoulder,
    RightShoulder,
    A,
    B,
    X,
    Y,
};

inline constexpr std::size_t kGamepadButtonCount = static_cast<std::size_t>(GamepadButton::Y) + 1;

enum class GamepadAxis : std::uint8_t
{
    LeftX = 0,
    LeftY,
    RightX,
    RightY,
    LeftTrigger,
    RightTrigger,
};

inline constexpr std::size_t kGamepadAxisCount = static_cast<std::size_t>(GamepadAxis::RightTrigger) + 1;

}
