#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

#include "zio/core/Platform.h"
#include "zio/events/Types.h"

namespace zio::events
{

using ListenerId                        = std::uint32_t;
inline constexpr ListenerId kInvalidListenerId = 0;

class ZIO_API EventBus
{
public:
    EventBus();
    ~EventBus();

    EventBus(const EventBus&)            = delete;
    EventBus& operator=(const EventBus&) = delete;

    static EventBus& instance();

    using KeyCallback         = std::function<void(const KeyEvent&)>;
    using MouseButtonCallback = std::function<void(const MouseButtonEvent&)>;
    using MouseMoveCallback   = std::function<void(const MouseMoveEvent&)>;
    using MouseScrollCallback = std::function<void(const MouseScrollEvent&)>;
    using TouchCallback       = std::function<void(const TouchEvent&)>;

    ListenerId onKey(KeyCallback callback);
    ListenerId onMouseButton(MouseButtonCallback callback);
    ListenerId onMouseMove(MouseMoveCallback callback);
    ListenerId onMouseScroll(MouseScrollCallback callback);
    ListenerId onTouch(TouchCallback callback);

    void unsubscribe(ListenerId id);

    void postKey(KeyCode code, bool down, bool repeat = false);
    void postMouseButton(MouseButton button, float x, float y, bool down);
    void postMouseMove(float x, float y, float dx, float dy);
    void postMouseScroll(float x, float y, float scrollX, float scrollY);
    void postTouch(TouchPhase phase, const TouchPoint* points, std::size_t count);

    void dispatchPending();

private:
    template <typename Callback>
    struct Listener
    {
        ListenerId id;
        Callback callback;
        bool alive = true;
    };

    template <typename Callback>
    using ListenerList = std::vector<Listener<Callback>>;

    template <typename Callback>
    static bool markRemoved(ListenerList<Callback>& list, ListenerId id);

    template <typename Callback>
    static void compact(ListenerList<Callback>& list);

    template <typename EventT, typename Callback>
    static void dispatchList(const std::vector<EventT>& events, ListenerList<Callback>& listeners);

    ListenerId allocateId();

    ListenerId nextId_ = 1;

    ListenerList<KeyCallback> keyListeners_;
    ListenerList<MouseButtonCallback> mouseButtonListeners_;
    ListenerList<MouseMoveCallback> mouseMoveListeners_;
    ListenerList<MouseScrollCallback> mouseScrollListeners_;
    ListenerList<TouchCallback> touchListeners_;

    std::mutex queueMutex_;
    std::vector<KeyEvent> pendingKeys_;
    std::vector<MouseButtonEvent> pendingMouseButtons_;
    std::vector<MouseMoveEvent> pendingMouseMoves_;
    std::vector<MouseScrollEvent> pendingMouseScrolls_;
    std::vector<TouchEvent> pendingTouches_;

    std::vector<KeyEvent> drainKeys_;
    std::vector<MouseButtonEvent> drainMouseButtons_;
    std::vector<MouseMoveEvent> drainMouseMoves_;
    std::vector<MouseScrollEvent> drainMouseScrolls_;
    std::vector<TouchEvent> drainTouches_;

    bool dispatching_ = false;
};

}
