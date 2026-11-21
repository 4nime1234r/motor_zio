#include "zio/scene/SceneStack.h"

namespace zio::scene
{

SceneStack& SceneStack::instance()
{
    static SceneStack global;
    return global;
}

void SceneStack::runScene(std::unique_ptr<IScene> scene)
{
    pendingScene_  = std::move(scene);
    pendingAction_ = Action::Run;
}

void SceneStack::replaceScene(std::unique_ptr<IScene> scene)
{
    pendingScene_  = std::move(scene);
    pendingAction_ = Action::Replace;
}

void SceneStack::pushScene(std::unique_ptr<IScene> scene)
{
    pendingScene_  = std::move(scene);
    pendingAction_ = Action::Push;
}

void SceneStack::pop()
{
    pendingScene_.reset();
    pendingAction_ = Action::Pop;
}

void SceneStack::popToRoot()
{
    pendingScene_.reset();
    pendingAction_ = Action::PopToRoot;
}

void SceneStack::applyPending()
{
    if (pendingAction_ == Action::None)
        return;

    const Action action           = pendingAction_;
    std::unique_ptr<IScene> scene = std::move(pendingScene_);
    pendingAction_                 = Action::None;
    pendingScene_.reset();

    switch (action)
    {
    case Action::None:
        break;

    case Action::Run:
        stack_.clear();
        stack_.push_back(std::move(scene));
        stack_.back()->onEnter();
        break;

    case Action::Replace:
        if (!stack_.empty())
        {
            stack_.back()->onExit();
            stack_.pop_back();
        }
        stack_.push_back(std::move(scene));
        stack_.back()->onEnter();
        break;

    case Action::Push:
        if (!stack_.empty())
            stack_.back()->onPause();
        stack_.push_back(std::move(scene));
        stack_.back()->onEnter();
        break;

    case Action::Pop:
        if (!stack_.empty())
        {
            stack_.back()->onExit();
            stack_.pop_back();
        }
        if (!stack_.empty())
            stack_.back()->onResume();
        break;

    case Action::PopToRoot:
        while (stack_.size() > 1)
        {
            stack_.back()->onExit();
            stack_.pop_back();
        }
        if (!stack_.empty())
            stack_.back()->onResume();
        break;
    }
}

void SceneStack::update(float dt)
{
    applyPending();
    if (!stack_.empty())
        stack_.back()->update(dt);
}

IScene* SceneStack::current() const
{
    return stack_.empty() ? nullptr : stack_.back().get();
}

std::size_t SceneStack::depth() const
{
    return stack_.size();
}

}
