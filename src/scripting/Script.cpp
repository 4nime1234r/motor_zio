#include "zio/scripting/Script.h"

namespace zio::scripting
{

Registry& Registry::instance()
{
    static Registry registry;
    return registry;
}

bool Registry::add(const std::string& name, ScriptFactory factory)
{
    if (name.empty() || !factory)
        return false;
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
    return false;
}

std::unique_ptr<Script> Registry::create(const std::string& name) const
{
    for (const Entry& entry : entries_)
        if (entry.name == name)
            return entry.factory();
    return nullptr;
}

std::vector<std::string> Registry::names() const
{
    std::vector<std::string> out;
    for (const Entry& entry : entries_)
        out.push_back(entry.name);
    return out;
}

}
