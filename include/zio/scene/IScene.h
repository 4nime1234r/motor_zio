#pragma once

namespace zio::scene
{

class IScene
{
public:
    virtual ~IScene() = default;

    virtual void onEnter() {}
    virtual void onExit() {}

    virtual void onPause() {}
    virtual void onResume() {}

    virtual void update(float dt) {}
};

}
