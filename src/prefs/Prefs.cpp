#include "zio/prefs/Prefs.h"

#include <cstring>
#include <fstream>

namespace zio::prefs
{

namespace
{
constexpr char kMagic[4]    = {'Z', 'P', 'R', 'F'};
constexpr uint32_t kVersion = 1;

enum class Tag : uint8_t
{
    Bool   = 0,
    Int32  = 1,
    Int64  = 2,
    Float  = 3,
    Double = 4,
    String = 5,
};

template <typename T>
void writePod(std::ofstream& out, const T& v)
{
    out.write(reinterpret_cast<const char*>(&v), sizeof(T));
}

template <typename T>
bool readPod(std::ifstream& in, T& v)
{
    in.read(reinterpret_cast<char*>(&v), sizeof(T));
    return static_cast<bool>(in);
}
}

Prefs::Prefs(std::filesystem::path filePath) : path_(std::move(filePath))
{
    load();
}

Prefs::~Prefs()
{
    flush();
}

Prefs& Prefs::instance()
{
    static Prefs global(std::filesystem::current_path() / "zio_prefs.dat");
    return global;
}

void Prefs::load()
{
    std::lock_guard lock(mutex_);
    values_.clear();

    std::ifstream in(path_, std::ios::binary);
    if (!in)
        return;

    char magic[4];
    in.read(magic, 4);
    if (!in || std::memcmp(magic, kMagic, 4) != 0)
        return;

    uint32_t version = 0;
    if (!readPod(in, version) || version != kVersion)
        return;

    uint32_t count = 0;
    if (!readPod(in, count))
        return;

    for (uint32_t i = 0; i < count; ++i)
    {
        uint32_t keyLen = 0;
        if (!readPod(in, keyLen) || keyLen > kMaxFieldLength)
            return;
        std::string key(keyLen, '\0');
        in.read(key.data(), keyLen);
        if (!in)
            return;

        uint8_t tag = 0;
        if (!readPod(in, tag))
            return;

        switch (static_cast<Tag>(tag))
        {
        case Tag::Bool:
        {
            uint8_t v = 0;
            if (!readPod(in, v))
                return;
            values_[key] = static_cast<bool>(v);
            break;
        }
        case Tag::Int32:
        {
            int32_t v = 0;
            if (!readPod(in, v))
                return;
            values_[key] = v;
            break;
        }
        case Tag::Int64:
        {
            int64_t v = 0;
            if (!readPod(in, v))
                return;
            values_[key] = v;
            break;
        }
        case Tag::Float:
        {
            float v = 0;
            if (!readPod(in, v))
                return;
            values_[key] = v;
            break;
        }
        case Tag::Double:
        {
            double v = 0;
            if (!readPod(in, v))
                return;
            values_[key] = v;
            break;
        }
        case Tag::String:
        {
            uint32_t len = 0;
            if (!readPod(in, len) || len > kMaxFieldLength)
                return;
            std::string v(len, '\0');
            in.read(v.data(), len);
            if (!in)
                return;
            values_[key] = std::move(v);
            break;
        }
        default:
            return;
        }
    }
}

bool Prefs::flush()
{
    std::lock_guard lock(mutex_);
    if (!dirty_)
        return true;

    std::ofstream out(path_, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;

    out.write(kMagic, 4);
    writePod(out, kVersion);
    writePod(out, static_cast<uint32_t>(values_.size()));

    for (auto& [key, value] : values_)
    {
        writePod(out, static_cast<uint32_t>(key.size()));
        out.write(key.data(), static_cast<std::streamsize>(key.size()));

        std::visit(
            [&out](auto&& v)
            {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, bool>)
                {
                    writePod(out, static_cast<uint8_t>(Tag::Bool));
                    writePod(out, static_cast<uint8_t>(v));
                }
                else if constexpr (std::is_same_v<T, int32_t>)
                {
                    writePod(out, static_cast<uint8_t>(Tag::Int32));
                    writePod(out, v);
                }
                else if constexpr (std::is_same_v<T, int64_t>)
                {
                    writePod(out, static_cast<uint8_t>(Tag::Int64));
                    writePod(out, v);
                }
                else if constexpr (std::is_same_v<T, float>)
                {
                    writePod(out, static_cast<uint8_t>(Tag::Float));
                    writePod(out, v);
                }
                else if constexpr (std::is_same_v<T, double>)
                {
                    writePod(out, static_cast<uint8_t>(Tag::Double));
                    writePod(out, v);
                }
                else if constexpr (std::is_same_v<T, std::string>)
                {
                    writePod(out, static_cast<uint8_t>(Tag::String));
                    writePod(out, static_cast<uint32_t>(v.size()));
                    out.write(v.data(), static_cast<std::streamsize>(v.size()));
                }
            },
            value);
    }

    if (!out)
        return false;

    dirty_ = false;
    return true;
}

bool Prefs::getBool(std::string_view key, bool defaultValue) const
{
    std::lock_guard lock(mutex_);
    auto it = values_.find(key);
    if (it == values_.end())
        return defaultValue;
    if (auto v = std::get_if<bool>(&it->second))
        return *v;
    return defaultValue;
}

void Prefs::setBool(std::string_view key, bool value)
{
    std::lock_guard lock(mutex_);
    values_[std::string(key)] = value;
    dirty_                    = true;
}

int32_t Prefs::getInt(std::string_view key, int32_t defaultValue) const
{
    std::lock_guard lock(mutex_);
    auto it = values_.find(key);
    if (it == values_.end())
        return defaultValue;
    if (auto v = std::get_if<int32_t>(&it->second))
        return *v;
    return defaultValue;
}

void Prefs::setInt(std::string_view key, int32_t value)
{
    std::lock_guard lock(mutex_);
    values_[std::string(key)] = value;
    dirty_                    = true;
}

int64_t Prefs::getLargeInt(std::string_view key, int64_t defaultValue) const
{
    std::lock_guard lock(mutex_);
    auto it = values_.find(key);
    if (it == values_.end())
        return defaultValue;
    if (auto v = std::get_if<int64_t>(&it->second))
        return *v;
    return defaultValue;
}

void Prefs::setLargeInt(std::string_view key, int64_t value)
{
    std::lock_guard lock(mutex_);
    values_[std::string(key)] = value;
    dirty_                    = true;
}

float Prefs::getFloat(std::string_view key, float defaultValue) const
{
    std::lock_guard lock(mutex_);
    auto it = values_.find(key);
    if (it == values_.end())
        return defaultValue;
    if (auto v = std::get_if<float>(&it->second))
        return *v;
    return defaultValue;
}

void Prefs::setFloat(std::string_view key, float value)
{
    std::lock_guard lock(mutex_);
    values_[std::string(key)] = value;
    dirty_                    = true;
}

double Prefs::getDouble(std::string_view key, double defaultValue) const
{
    std::lock_guard lock(mutex_);
    auto it = values_.find(key);
    if (it == values_.end())
        return defaultValue;
    if (auto v = std::get_if<double>(&it->second))
        return *v;
    return defaultValue;
}

void Prefs::setDouble(std::string_view key, double value)
{
    std::lock_guard lock(mutex_);
    values_[std::string(key)] = value;
    dirty_                    = true;
}

std::string Prefs::getString(std::string_view key, std::string_view defaultValue) const
{
    std::lock_guard lock(mutex_);
    auto it = values_.find(key);
    if (it == values_.end())
        return std::string(defaultValue);
    if (auto v = std::get_if<std::string>(&it->second))
        return *v;
    return std::string(defaultValue);
}

void Prefs::setString(std::string_view key, std::string_view value)
{
    std::lock_guard lock(mutex_);
    values_[std::string(key)] = std::string(value);
    dirty_                    = true;
}

bool Prefs::has(std::string_view key) const
{
    std::lock_guard lock(mutex_);
    return values_.contains(key);
}

void Prefs::remove(std::string_view key)
{
    std::lock_guard lock(mutex_);
    if (values_.erase(std::string(key)) > 0)
        dirty_ = true;
}

void Prefs::clear()
{
    std::lock_guard lock(mutex_);
    if (!values_.empty())
    {
        values_.clear();
        dirty_ = true;
    }
}

}
