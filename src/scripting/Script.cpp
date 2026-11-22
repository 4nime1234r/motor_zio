#include "zio/scripting/Script.h"

#include <algorithm>

namespace zio::scripting
{

Script::~Script()
{
    Registry::instance().forget(this);
}

Registry& Registry::instance()
{
    static Registry* registry = new Registry;
    return *registry;
}

void Registry::forget(const Script* script)
{
    std::lock_guard<std::mutex> lock(liveMutex_);
    live_.erase(script);
}

bool Registry::inUse(const std::vector<std::string>& names) const
{
    std::lock_guard<std::mutex> lock(liveMutex_);
    for (const auto& item : live_)
        if (std::find(names.begin(), names.end(), item.second) != names.end())
            return true;
    return false;
}

bool Registry::remove(const std::string& name)
{
    for (auto it = entries_.begin(); it != entries_.end(); ++it)
        if (it->name == name)
        {
            entries_.erase(it);
            ++version_;
            return true;
        }
    return false;
}

void Registry::beginRecording()
{
    recorded_.clear();
    recording_ = true;
}

std::vector<std::string> Registry::endRecording()
{
    recording_ = false;
    return std::move(recorded_);
}

void Registry::setProvider(const std::string& key, NamesProvider names, CreateProvider create)
{
    ++version_;
    for (Provider& provider : providers_)
        if (provider.key == key)
        {
            provider.names  = std::move(names);
            provider.create = std::move(create);
            return;
        }
    providers_.push_back({key, std::move(names), std::move(create)});
}

void Registry::removeProvider(const std::string& key)
{
    providers_.erase(std::remove_if(providers_.begin(), providers_.end(),
                                    [&](const Provider& provider) { return provider.key == key; }),
                     providers_.end());
    ++version_;
}

bool Registry::add(const std::string& name, ScriptFactory factory)
{
    if (name.empty() || !factory)
        return false;
    ++version_;
    if (recording_)
        recorded_.push_back(name);
    for (Entry& entry : entries_)
        if (entry.name == name)
        {
            entry.factory = std::move(factory);
            return true;
        }
    entries_.push_back({name, std::move(factory)});
    return true;
}

bool Registry::has(const std::string& name) const
{
    for (const Entry& entry : entries_)
        if (entry.name == name)
            return true;
    for (const Provider& provider : providers_)
        if (provider.names)
            for (const std::string& provided : provider.names())
                if (provided == name)
                    return true;
    return false;
}

std::unique_ptr<Script> Registry::create(const std::string& name) const
{
    std::unique_ptr<Script> script;
    for (const Entry& entry : entries_)
        if (entry.name == name)
        {
            script = entry.factory();
            break;
        }
    if (!script)
        for (const Provider& provider : providers_)
            if (provider.create && (script = provider.create(name)))
                break;
    if (script)
    {
        std::lock_guard<std::mutex> lock(liveMutex_);
        live_[script.get()] = name;
    }
    return script;
}

std::vector<std::string> Registry::names() const
{
    std::vector<std::string> out;
    for (const Entry& entry : entries_)
        out.push_back(entry.name);
    for (const Provider& provider : providers_)
        if (provider.names)
            for (std::string& provided : provider.names())
                if (std::find(out.begin(), out.end(), provided) == out.end())
                    out.push_back(std::move(provided));
    return out;
}

}
