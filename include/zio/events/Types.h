#pragma once

#include <cstddef>
#include <cstdint>

namespace zio::events
{

enum class KeyCode : std::uint16_t
{
    Unknown = 0,

    Escape,
    Tab,
    Enter,
    Backspace,
    Space,
    Delete,
    Insert,
    CapsLock,

    Home,
    End,
    PageUp,
    PageDown,

    Left,
    Right,
    Up,
    Down,

    LeftShift,
    RightShift,
    LeftCtrl,
    RightCtrl,
    LeftAlt,
    RightAlt,

    Minus,
    Equal,
    LeftBracket,
    RightBracket,
    BackSlash,
    Semicolon,
    Apostrophe,
    Grave,
    Comma,
    Period,
    Slash,

    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,

    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
};

enum class MouseButton : std::uint8_t
{
    Left = 0,
    Right,
    Middle,
    Extra1,
    Extra2,
};

enum class TouchPhase : std::uint8_t
{
    Began,
    Moved,
    Ended,
    Cancelled,
};

inline constexpr std::size_t kMaxTouchPoints = 15;

struct TouchPoint
{
    int id  = -1;
    float x = 0.f;
    float y = 0.f;
};

struct KeyEvent
{
    KeyCode code;
    bool down;
    bool repeat;
};

struct MouseButtonEvent
{
    MouseButton button;
    float x, y;
    bool down;
};

struct MouseMoveEvent
{
    float x, y;
    float dx, dy;
};

struct MouseScrollEvent
{
    float x, y;
    float scrollX, scrollY;
};

struct TextEvent
{
    char32_t codepoint = 0;
};

struct TouchEvent
{
    TouchPhase phase;
    TouchPoint points[kMaxTouchPoints];
    std::size_t count = 0;
};

}
