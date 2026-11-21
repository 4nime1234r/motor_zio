#pragma once

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace zio::json
{

constexpr int kMaxDepth = 128;

inline void writeString(std::string& out, std::string_view s)
{
    out += '"';
    for (const unsigned char c : s)
    {
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20)
            {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            }
            else
                out += static_cast<char>(c);
        }
    }
    out += '"';
}

inline void writeNumber(std::string& out, double v)
{
    if (!std::isfinite(v))
        v = 0.0;
    char buf[40];
    const auto r = std::to_chars(buf, buf + sizeof(buf), v);
    out.append(buf, r.ptr);
}

struct Value;
using Array  = std::vector<Value>;
using Object = std::vector<std::pair<std::string, Value>>;

struct Value
{
    std::variant<std::monostate, bool, double, std::string, Array, Object> v;

    const Value* find(std::string_view key) const
    {
        if (const auto* o = std::get_if<Object>(&v))
            for (const auto& [k, val] : *o)
                if (k == key)
                    return &val;
        return nullptr;
    }
};

class Parser
{
public:
    explicit Parser(std::string_view text) : s_(text) {}

    bool parse(Value& out, std::string& err)
    {
        if (!value(out, 0))
        {
            err = err_;
            return false;
        }
        skipWs();
        if (pos_ != s_.size())
        {
            err = fail("unexpected data after the JSON value");
            return false;
        }
        return true;
    }

private:
    std::string fail(const std::string& msg)
    {
        std::size_t line = 1;
        for (std::size_t i = 0; i < pos_ && i < s_.size(); ++i)
            if (s_[i] == '\n')
                ++line;
        err_ = "JSON error at line " + std::to_string(line) + ": " + msg;
        return err_;
    }
    bool bad(const std::string& msg)
    {
        fail(msg);
        return false;
    }

    void skipWs()
    {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r'))
            ++pos_;
    }

    bool literal(std::string_view word)
    {
        if (s_.substr(pos_, word.size()) == word)
        {
            pos_ += word.size();
            return true;
        }
        return false;
    }

    bool value(Value& out, int depth)
    {
        if (depth > kMaxDepth)
            return bad("nesting too deep");
        skipWs();
        if (pos_ >= s_.size())
            return bad("unexpected end of file");
        const char c = s_[pos_];
        if (c == '{')
            return object(out, depth);
        if (c == '[')
            return array(out, depth);
        if (c == '"')
        {
            std::string str;
            if (!string(str))
                return false;
            out.v = std::move(str);
            return true;
        }
        if (literal("true"))
        {
            out.v = true;
            return true;
        }
        if (literal("false"))
        {
            out.v = false;
            return true;
        }
        if (literal("null"))
        {
            out.v = std::monostate{};
            return true;
        }
        return number(out);
    }

    bool number(Value& out)
    {
        const char* first = s_.data() + pos_;
        const char* last  = s_.data() + s_.size();
        double d          = 0.0;
        const auto r      = std::from_chars(first, last, d);
        if (r.ec != std::errc{} || r.ptr == first)
            return bad("invalid value");
        pos_ += static_cast<std::size_t>(r.ptr - first);
        out.v = d;
        return true;
    }

    static void appendUtf8(std::string& out, std::uint32_t cp)
    {
        if (cp < 0x80)
            out += static_cast<char>(cp);
        else if (cp < 0x800)
        {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else
        {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool hex4(std::uint32_t& cp)
    {
        if (pos_ + 4 > s_.size())
            return bad("bad \\u escape");
        cp = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char h = s_[pos_++];
            cp <<= 4;
            if (h >= '0' && h <= '9')
                cp |= static_cast<std::uint32_t>(h - '0');
            else if (h >= 'a' && h <= 'f')
                cp |= static_cast<std::uint32_t>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F')
                cp |= static_cast<std::uint32_t>(h - 'A' + 10);
            else
                return bad("bad \\u escape");
        }
        return true;
    }

    bool string(std::string& out)
    {
        ++pos_;
        while (pos_ < s_.size())
        {
            const char c = s_[pos_++];
            if (c == '"')
                return true;
            if (static_cast<unsigned char>(c) < 0x20)
                return bad("control character in string");
            if (c != '\\')
            {
                out += c;
                continue;
            }
            if (pos_ >= s_.size())
                break;
            const char e = s_[pos_++];
            switch (e)
            {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u':
            {
                std::uint32_t cp = 0;
                if (!hex4(cp))
                    return false;
                if (cp >= 0xD800 && cp <= 0xDBFF)
                {
                    std::uint32_t lo = 0;
                    if (!literal("\\u") || !hex4(lo) || lo < 0xDC00 || lo > 0xDFFF)
                        return bad("bad surrogate pair");
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                else if (cp >= 0xDC00 && cp <= 0xDFFF)
                    return bad("bad surrogate pair");
                appendUtf8(out, cp);
                break;
            }
            default: return bad("bad escape in string");
            }
        }
        return bad("unterminated string");
    }

    bool array(Value& out, int depth)
    {
        ++pos_;
        Array arr;
        skipWs();
        if (pos_ < s_.size() && s_[pos_] == ']')
        {
            ++pos_;
            out.v = std::move(arr);
            return true;
        }
        for (;;)
        {
            Value item;
            if (!value(item, depth + 1))
                return false;
            arr.push_back(std::move(item));
            skipWs();
            if (pos_ >= s_.size())
                return bad("unterminated array");
            const char c = s_[pos_++];
            if (c == ']')
                break;
            if (c != ',')
                return bad("expected ',' or ']'");
        }
        out.v = std::move(arr);
        return true;
    }

    bool object(Value& out, int depth)
    {
        ++pos_;
        Object obj;
        skipWs();
        if (pos_ < s_.size() && s_[pos_] == '}')
        {
            ++pos_;
            out.v = std::move(obj);
            return true;
        }
        for (;;)
        {
            skipWs();
            if (pos_ >= s_.size() || s_[pos_] != '"')
                return bad("expected a string key");
            std::string key;
            if (!string(key))
                return false;
            skipWs();
            if (pos_ >= s_.size() || s_[pos_] != ':')
                return bad("expected ':'");
            ++pos_;
            Value item;
            if (!value(item, depth + 1))
                return false;
            obj.emplace_back(std::move(key), std::move(item));
            skipWs();
            if (pos_ >= s_.size())
                return bad("unterminated object");
            const char c = s_[pos_++];
            if (c == '}')
                break;
            if (c != ',')
                return bad("expected ',' or '}'");
        }
        out.v = std::move(obj);
        return true;
    }

    std::string_view s_;
    std::size_t pos_ = 0;
    std::string err_;
};

struct Fields
{
    std::string err;

    bool fail(const std::string& path, const std::string& msg)
    {
        if (err.empty())
            err = path + ": " + msg;
        return false;
    }

    bool getString(const Value& o, const char* key, const std::string& path, std::string& out, bool& present)
    {
        present = false;
        const Value* f = o.find(key);
        if (!f)
            return true;
        const auto* s = std::get_if<std::string>(&f->v);
        if (!s)
            return fail(path, std::string("\"") + key + "\" must be a string");
        out     = *s;
        present = true;
        return true;
    }

    bool getNumber(const Value& o, const char* key, const std::string& path, double& out, bool& present)
    {
        present = false;
        const Value* f = o.find(key);
        if (!f)
            return true;
        const auto* d = std::get_if<double>(&f->v);
        if (!d)
            return fail(path, std::string("\"") + key + "\" must be a number");
        out     = *d;
        present = true;
        return true;
    }

    bool getBool(const Value& o, const char* key, const std::string& path, bool& out, bool& present)
    {
        present = false;
        const Value* f = o.find(key);
        if (!f)
            return true;
        const auto* b = std::get_if<bool>(&f->v);
        if (!b)
            return fail(path, std::string("\"") + key + "\" must be true or false");
        out     = *b;
        present = true;
        return true;
    }

    bool getNumbers(const Value& o, const char* key, const std::string& path, std::size_t count, double* out,
                    bool& present)
    {
        present = false;
        const Value* f = o.find(key);
        if (!f)
            return true;
        const auto* a = std::get_if<Array>(&f->v);
        if (!a || a->size() != count)
            return fail(path, std::string("\"") + key + "\" must be an array of " + std::to_string(count) + " numbers");
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto* d = std::get_if<double>(&(*a)[i].v);
            if (!d)
                return fail(path, std::string("\"") + key + "\" must contain only numbers");
            out[i] = *d;
        }
        present = true;
        return true;
    }

};

}
