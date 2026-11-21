#pragma once

#include <functional>
#include <memory>
#include <string>
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
    virtual ~Script() = default;

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
    bool has(const std::string& name) const;
    std::unique_ptr<Script> create(const std::string& name) const;
    std::vector<std::string> names() const;

    using NamesProvider  = std::function<std::vector<std::string>()>;
    using CreateProvider = std::function<std::unique_ptr<Script>(const std::string&)>;
    void setProvider(NamesProvider names, CreateProvider create);
    void notifyChanged() { ++version_; }
    unsigned version() const { return version_; }

private:
    NamesProvider providerNames_;
    CreateProvider providerCreate_;
    unsigned version_ = 1;
    struct Entry
    {
        std::string name;
        ScriptFactory factory;
    };
    std::vector<Entry> entries_;
};

}

#define ZIO_SCRIPT(ClassName)                                                                                       \
    namespace                                                                                                       \
    {                                                                                                               \
    const bool zioScriptRegistered_##ClassName = ::zio::scripting::Registry::instance().add(                        \
        #ClassName, []() -> std::unique_ptr<::zio::scripting::Script> { return std::make_unique<ClassName>(); });   \
    }
