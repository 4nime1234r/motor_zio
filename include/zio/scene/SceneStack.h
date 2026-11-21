#pragma once

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "zio/core/Platform.h"
#include "zio/scene/IScene.h"

namespace zio::scene
{

class ZIO_API SceneStack
{
public:
    SceneStack()  = default;
    ~SceneStack() = default;

    SceneStack(const SceneStack&)            = delete;
    SceneStack& operator=(const SceneStack&) = delete;

    static SceneStack& instance();

    template <typename SceneT, typename... Args>
    SceneT* run(Args&&... args)
    {
        auto built  = std::make_unique<SceneT>(std::forward<Args>(args)...);
        SceneT* ptr = built.get();
        runScene(std::move(built));
        return ptr;
    }

    template <typename SceneT, typename... Args>
    SceneT* replace(Args&&... args)
    {
        auto built  = std::make_unique<SceneT>(std::forward<Args>(args)...);
        SceneT* ptr = built.get();
        replaceScene(std::move(built));
        return ptr;
    }

    template <typename SceneT, typename... Args>
    SceneT* push(Args&&... args)
    {
        auto built  = std::make_unique<SceneT>(std::forward<Args>(args)...);
        SceneT* ptr = built.get();
        pushScene(std::move(built));
        return ptr;
    }

    void pop();
    void popToRoot();

    void runScene(std::unique_ptr<IScene> scene);
    void replaceScene(std::unique_ptr<IScene> scene);
    void pushScene(std::unique_ptr<IScene> scene);

    void update(float dt);

    IScene* current() const;
    std::size_t depth() const;

private:
    enum class Action
    {
        None,
        Run,
        Replace,
        Push,
        Pop,
        PopToRoot,
    };

    void applyPending();

    std::vector<std::unique_ptr<IScene>> stack_;
    std::unique_ptr<IScene> pendingScene_;
    Action pendingAction_ = Action::None;
};

}
