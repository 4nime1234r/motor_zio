#include "zio/events/EventBus.h"

#include <algorithm>
#include <utility>

namespace zio::events
{

namespace
{
constexpr std::size_t kInitialQueueCapacity = 16;
}

EventBus::EventBus()
{
    pendingKeys_.reserve(kInitialQueueCapacity);
    pendingMouseButtons_.reserve(kInitialQueueCapacity);
    pendingMouseMoves_.reserve(kInitialQueueCapacity);
    pendingMouseScrolls_.reserve(kInitialQueueCapacity);
    pendingTouches_.reserve(kInitialQueueCapacity);
    pendingTexts_.reserve(kInitialQueueCapacity);

    drainKeys_.reserve(kInitialQueueCapacity);
    drainMouseButtons_.reserve(kInitialQueueCapacity);
    drainMouseMoves_.reserve(kInitialQueueCapacity);
    drainMouseScrolls_.reserve(kInitialQueueCapacity);
    drainTouches_.reserve(kInitialQueueCapacity);
    drainTexts_.reserve(kInitialQueueCapacity);
}

EventBus::~EventBus() = default;

EventBus& EventBus::instance()
{
    static EventBus global;
    return global;
}

template <typename Callback>
bool EventBus::markRemoved(ListenerList<Callback>& list, ListenerId id)
{
    for (auto& listener : list)
    {
        if (listener.id == id)
        {
            listener.alive = false;
            return true;
        }
    }
    return false;
}

template <typename Callback>
void EventBus::compact(ListenerList<Callback>& list)
{
    list.erase(std::remove_if(list.begin(), list.end(), [](const Listener<Callback>& l) { return !l.alive; }),
               list.end());
}

template <typename EventT, typename Callback>
void EventBus::dispatchList(const std::vector<EventT>& events, ListenerList<Callback>& listeners)
{
    const std::size_t count = listeners.size();
    for (const auto& event : events)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            auto& listener = listeners[i];
            if (listener.alive)
                listener.callback(event);
        }
    }
}

ListenerId EventBus::allocateId()
{
    ListenerId id = nextId_++;
    if (nextId_ == kInvalidListenerId)
        ++nextId_;
    return id;
}

ListenerId EventBus::onKey(KeyCallback callback)
{
    ListenerId id = allocateId();
    keyListeners_.push_back(Listener<KeyCallback>{id, std::move(callback)});
    return id;
}

ListenerId EventBus::onMouseButton(MouseButtonCallback callback)
{
    ListenerId id = allocateId();
    mouseButtonListeners_.push_back(Listener<MouseButtonCallback>{id, std::move(callback)});
    return id;
}

ListenerId EventBus::onMouseMove(MouseMoveCallback callback)
{
    ListenerId id = allocateId();
    mouseMoveListeners_.push_back(Listener<MouseMoveCallback>{id, std::move(callback)});
    return id;
}

ListenerId EventBus::onMouseScroll(MouseScrollCallback callback)
{
    ListenerId id = allocateId();
    mouseScrollListeners_.push_back(Listener<MouseScrollCallback>{id, std::move(callback)});
    return id;
}

ListenerId EventBus::onTouch(TouchCallback callback)
{
    ListenerId id = allocateId();
    touchListeners_.push_back(Listener<TouchCallback>{id, std::move(callback)});
    return id;
}

ListenerId EventBus::onText(TextCallback callback)
{
    ListenerId id = allocateId();
    textListeners_.push_back(Listener<TextCallback>{id, std::move(callback)});
    return id;
}

void EventBus::unsubscribe(ListenerId id)
{
    if (id == kInvalidListenerId)
        return;

    if (markRemoved(keyListeners_, id))
        return;
    if (markRemoved(mouseButtonListeners_, id))
        return;
    if (markRemoved(mouseMoveListeners_, id))
        return;
    if (markRemoved(mouseScrollListeners_, id))
        return;
    if (markRemoved(touchListeners_, id))
        return;
    markRemoved(textListeners_, id);
}

void EventBus::postKey(KeyCode code, bool down, bool repeat)
{
    std::lock_guard<std::mutex> lock(queueMutex_);
    pendingKeys_.push_back(KeyEvent{code, down, repeat});
}

void EventBus::postMouseButton(MouseButton button, float x, float y, bool down)
{
    std::lock_guard<std::mutex> lock(queueMutex_);
    pendingMouseButtons_.push_back(MouseButtonEvent{button, x, y, down});
}

void EventBus::postMouseMove(float x, float y, float dx, float dy)
{
    std::lock_guard<std::mutex> lock(queueMutex_);
    pendingMouseMoves_.push_back(MouseMoveEvent{x, y, dx, dy});
}

void EventBus::postMouseScroll(float x, float y, float scrollX, float scrollY)
{
    std::lock_guard<std::mutex> lock(queueMutex_);
    pendingMouseScrolls_.push_back(MouseScrollEvent{x, y, scrollX, scrollY});
}

void EventBus::postText(char32_t codepoint)
{
    std::lock_guard<std::mutex> lock(queueMutex_);
    pendingTexts_.push_back(TextEvent{codepoint});
}

void EventBus::postTouch(TouchPhase phase, const TouchPoint* points, std::size_t count)
{
    TouchEvent event;
    event.phase = phase;
    event.count = std::min(count, kMaxTouchPoints);
    for (std::size_t i = 0; i < event.count; ++i)
        event.points[i] = points[i];

    std::lock_guard<std::mutex> lock(queueMutex_);
    pendingTouches_.push_back(event);
}

void EventBus::dispatchPending()
{
    if (dispatching_)
        return;
    dispatching_ = true;
    struct ResetGuard
    {
        bool& flag;
        ~ResetGuard() { flag = false; }
    } resetGuard{dispatching_};

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        std::swap(pendingKeys_, drainKeys_);
        std::swap(pendingMouseButtons_, drainMouseButtons_);
        std::swap(pendingMouseMoves_, drainMouseMoves_);
        std::swap(pendingMouseScrolls_, drainMouseScrolls_);
        std::swap(pendingTouches_, drainTouches_);
        std::swap(pendingTexts_, drainTexts_);
    }

    dispatchList(drainKeys_, keyListeners_);
    drainKeys_.clear();
    compact(keyListeners_);

    dispatchList(drainMouseButtons_, mouseButtonListeners_);
    drainMouseButtons_.clear();
    compact(mouseButtonListeners_);

    dispatchList(drainMouseMoves_, mouseMoveListeners_);
    drainMouseMoves_.clear();
    compact(mouseMoveListeners_);

    dispatchList(drainMouseScrolls_, mouseScrollListeners_);
    drainMouseScrolls_.clear();
    compact(mouseScrollListeners_);

    dispatchList(drainTexts_, textListeners_);
    drainTexts_.clear();
    compact(textListeners_);

    dispatchList(drainTouches_, touchListeners_);
    drainTouches_.clear();
    compact(touchListeners_);
}

}
