#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "zio/core/Platform.h"

namespace zio::scene3d
{
class Node3D;
}

namespace zio::scripting
{

class ZIO_API Script
{
public:
    virtual ~Script();

    virtual void onStart() {}
    virtual void onUpdate(float dt) {}
    virtual void onDestroy() {}

    scene3d::Node3D& node() const { return *node_; }
    bool attached() const { return node_ != nullptr; }

private:
    friend class scene3d::Node3D;
    scene3d::Node3D* node_ = nullptr;
};

using ScriptFactory = std::function<std::unique_ptr<Script>()>;

class ZIO_API Registry
{
public:
    static Registry& instance();

    bool add(const std::string& name, ScriptFactory factory);
    bool remove(const std::string& name);
    void beginRecording();
    std::vector<std::string> endRecording();
    bool inUse(const std::vector<std::string>& names) const;
    void forget(const Script* script);
    bool has(const std::string& name) const;
    std::unique_ptr<Script> create(const std::string& name) const;
    std::vector<std::string> names() const;

    using NamesProvider  = std::function<std::vector<std::string>()>;
    using CreateProvider = std::function<std::unique_ptr<Script>(const std::string&)>;
    void setProvider(const std::string& key, NamesProvider names, CreateProvider create);
    void removeProvider(const std::string& key);
    void notifyChanged() { ++version_; }
    unsigned version() const { return version_; }

private:
    struct Provider
    {
        std::string key;
        NamesProvider names;
        CreateProvider create;
    };
    std::vector<Provider> providers_;
    unsigned version_ = 1;
    struct Entry
    {
        std::string name;
        ScriptFactory factory;
    };
    std::vector<Entry> entries_;
    std::vector<std::string> recorded_;
    bool recording_ = false;
    mutable std::mutex liveMutex_;
    mutable std::unordered_map<const Script*, std::string> live_;
};

}

#define ZIO_SCRIPT(ClassName)                                                                                       \
    namespace                                                                                                       \
    {                                                                                                               \
    const bool zioScriptRegistered_##ClassName = ::zio::scripting::Registry::instance().add(                        \
        #ClassName, []() -> std::unique_ptr<::zio::scripting::Script> { return std::make_unique<ClassName>(); });   \
    }
