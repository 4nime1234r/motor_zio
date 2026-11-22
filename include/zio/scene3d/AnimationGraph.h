#pragma once

#include <map>
#include <string>
#include <vector>

#include "zio/core/Platform.h"

namespace zio::scene3d
{

enum class Compare
{
    Greater,
    Less,
    Equal,
    IsTrue,
    IsFalse,
    Trigger
};

struct GraphState
{
    std::string name;
    std::string clip;
    bool loop    = true;
    float speed  = 1.f;
};

struct GraphTransition
{
    std::string from;
    std::string to;
    std::string parameter;
    Compare compare = Compare::Trigger;
    float value     = 0.f;
    float fade      = 0.2f;
};

struct GraphChange
{
    std::string state;
    std::string clip;
    bool loop   = true;
    float speed = 1.f;
    float fade  = 0.f;
};

class ZIO_API AnimationGraph
{
public:
    void addState(std::string name, std::string clip, bool loop = true, float speed = 1.f);
    void addTransition(GraphTransition transition);
    void setInitial(const std::string& name) { initial_ = name; }

    void setFloat(const std::string& name, float value) { floats_[name] = value; }
    void setBool(const std::string& name, bool value) { bools_[name] = value; }
    void trigger(const std::string& name) { triggers_[name] = true; }
    float getFloat(const std::string& name) const;
    bool getBool(const std::string& name) const;

    bool update(GraphChange& change);
    void reset() { current_.clear(); }
    const std::string& currentState() const { return current_; }
    const std::vector<GraphState>& states() const { return states_; }

private:
    const GraphState* find(const std::string& name) const;
    bool satisfied(const GraphTransition& transition) const;

    std::vector<GraphState> states_;
    std::vector<GraphTransition> transitions_;
    std::string initial_;
    std::string current_;
    std::map<std::string, float> floats_;
    std::map<std::string, bool> bools_;
    std::map<std::string, bool> triggers_;
};

}
