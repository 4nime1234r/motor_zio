#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>

#include "zio/core/Platform.h"

namespace zio::prefs
{

class ZIO_API Prefs
{
public:
    explicit Prefs(std::filesystem::path filePath);
    ~Prefs();

    Prefs(const Prefs&)            = delete;
    Prefs& operator=(const Prefs&) = delete;

    static Prefs& instance();

    bool getBool(std::string_view key, bool defaultValue = false) const;
    void setBool(std::string_view key, bool value);

    int32_t getInt(std::string_view key, int32_t defaultValue = 0) const;
    void setInt(std::string_view key, int32_t value);

    int64_t getLargeInt(std::string_view key, int64_t defaultValue = 0) const;
    void setLargeInt(std::string_view key, int64_t value);

    float getFloat(std::string_view key, float defaultValue = 0.f) const;
    void setFloat(std::string_view key, float value);

    double getDouble(std::string_view key, double defaultValue = 0.0) const;
    void setDouble(std::string_view key, double value);

    std::string getString(std::string_view key, std::string_view defaultValue = {}) const;
    void setString(std::string_view key, std::string_view value);

    bool has(std::string_view key) const;
    void remove(std::string_view key);
    void clear();

    bool flush();

private:
    using Value = std::variant<bool, int32_t, int64_t, float, double, std::string>;

    struct TransparentStringHash
    {
        using is_transparent = void;
        std::size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
    };

    static constexpr std::uint32_t kMaxFieldLength = 1u << 20;

    void load();

    std::filesystem::path path_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, Value, TransparentStringHash, std::equal_to<>> values_;
    bool dirty_ = false;
};

}
