#include "zio/scene3d/AnimationGraph.h"

namespace zio::scene3d
{

void AnimationGraph::addState(std::string name, std::string clip, bool loop, float speed)
{
    if (initial_.empty())
        initial_ = name;
    states_.push_back({std::move(name), std::move(clip), loop, speed});
}

void AnimationGraph::addTransition(GraphTransition transition)
{
    transitions_.push_back(std::move(transition));
}

float AnimationGraph::getFloat(const std::string& name) const
{
    const auto it = floats_.find(name);
    return it == floats_.end() ? 0.f : it->second;
}

bool AnimationGraph::getBool(const std::string& name) const
{
    const auto it = bools_.find(name);
    return it != bools_.end() && it->second;
}

const GraphState* AnimationGraph::find(const std::string& name) const
{
    for (const GraphState& state : states_)
        if (state.name == name)
            return &state;
    return nullptr;
}

bool AnimationGraph::satisfied(const GraphTransition& t) const
{
    switch (t.compare)
    {
    case Compare::Greater:
        return getFloat(t.parameter) > t.value;
    case Compare::Less:
        return getFloat(t.parameter) < t.value;
    case Compare::Equal:
        return getFloat(t.parameter) == t.value;
    case Compare::IsTrue:
        return getBool(t.parameter);
    case Compare::IsFalse:
        return !getBool(t.parameter);
    case Compare::Trigger:
    {
        const auto it = triggers_.find(t.parameter);
        return it != triggers_.end() && it->second;
    }
    }
    return false;
}

bool AnimationGraph::update(GraphChange& change)
{
    if (current_.empty())
    {
        const GraphState* start = find(initial_);
        if (!start)
            return false;
        current_      = start->name;
        change.state  = start->name;
        change.clip   = start->clip;
        change.loop   = start->loop;
        change.speed  = start->speed;
        change.fade   = 0.f;
        return true;
    }

    for (const GraphTransition& transition : transitions_)
    {
        if (transition.from != current_ && transition.from != "*")
            continue;
        if (transition.to == current_ || !satisfied(transition))
            continue;
        const GraphState* target = find(transition.to);
        if (!target)
            continue;
        if (transition.compare == Compare::Trigger)
            triggers_[transition.parameter] = false;
        current_      = target->name;
        change.state  = target->name;
        change.clip   = target->clip;
        change.loop   = target->loop;
        change.speed  = target->speed;
        change.fade   = transition.fade;
        return true;
    }
    return false;
}

}
