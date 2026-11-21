#include "zio/scripting/Script.h"

#include <algorithm>

namespace zio::scripting
{

Registry& Registry::instance()
{
    static Registry registry;
    return registry;
}

void Registry::setProvider(NamesProvider names, CreateProvider create)
{
    providerNames_  = std::move(names);
    providerCreate_ = std::move(create);
    ++version_;
}

bool Registry::add(const std::string& name, ScriptFactory factory)
{
    if (name.empty() || !factory)
        return false;
    ++version_;
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
    if (providerNames_)
        for (const std::string& provided : providerNames_())
            if (provided == name)
                return true;
    return false;
}

std::unique_ptr<Script> Registry::create(const std::string& name) const
{
    for (const Entry& entry : entries_)
        if (entry.name == name)
            return entry.factory();
    return providerCreate_ ? providerCreate_(name) : nullptr;
}

std::vector<std::string> Registry::names() const
{
    std::vector<std::string> out;
    for (const Entry& entry : entries_)
        out.push_back(entry.name);
    if (providerNames_)
        for (std::string& provided : providerNames_())
            if (std::find(out.begin(), out.end(), provided) == out.end())
                out.push_back(std::move(provided));
    return out;
}

}
