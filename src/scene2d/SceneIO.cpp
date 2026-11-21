#include "zio/scene2d/SceneIO.h"

#include <charconv>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <memory>
#include <sstream>
#include <utility>
#include <variant>
#include <vector>

#include "internal/Json.h"
#include "zio/renderer/Color.h"
#include "zio/scene2d/Label.h"
#include "zio/scene2d/Sprite.h"

namespace zio::scene2d
{

namespace
{

using namespace json;
using json::kMaxDepth;

void writeVec2(std::string& out, math::Vec2 v)
{
    out += '[';
    writeNumber(out, v.x);
    out += ", ";
    writeNumber(out, v.y);
    out += ']';
}

void writeColor(std::string& out, renderer::Color c)
{
    out += '[';
    writeNumber(out, c.r);
    out += ", ";
    writeNumber(out, c.g);
    out += ", ";
    writeNumber(out, c.b);
    out += ", ";
    writeNumber(out, c.a);
    out += ']';
}

void indent(std::string& out, int level)
{
    out.append(static_cast<std::size_t>(level) * 2, ' ');
}

void writeNode(std::string& out, const Node& node, int level)
{
    const char* type = "Node";
    if (dynamic_cast<const Sprite*>(&node))
        type = "Sprite";
    else if (dynamic_cast<const Label*>(&node))
        type = "Label";

    const int in = level + 1;
    out += "{\n";
    indent(out, in);
    out += "\"type\": ";
    writeString(out, type);
    out += ",\n";
    indent(out, in);
    out += "\"name\": ";
    writeString(out, node.name());
    out += ",\n";
    indent(out, in);
    out += "\"position\": ";
    writeVec2(out, node.position());
    out += ",\n";
    indent(out, in);
    out += "\"rotation\": ";
    writeNumber(out, node.rotation());
    out += ",\n";
    indent(out, in);
    out += "\"scale\": ";
    writeVec2(out, node.scale());
    out += ",\n";
    indent(out, in);
    out += "\"anchor\": ";
    writeVec2(out, node.anchorPoint());
    out += ",\n";
    indent(out, in);
    out += "\"size\": ";
    writeVec2(out, node.contentSize());
    out += ",\n";
    indent(out, in);
    out += "\"visible\": ";
    out += node.isVisible() ? "true" : "false";
    out += ",\n";
    indent(out, in);
    out += "\"z\": ";
    writeNumber(out, node.localZOrder());

    if (const auto* sprite = dynamic_cast<const Sprite*>(&node))
    {
        out += ",\n";
        indent(out, in);
        out += "\"texture\": ";
        writeString(out, sprite->texturePath());
        out += ",\n";
        indent(out, in);
        out += "\"color\": ";
        writeColor(out, sprite->color());
    }
    else if (const auto* label = dynamic_cast<const Label*>(&node))
    {
        out += ",\n";
        indent(out, in);
        out += "\"text\": ";
        writeString(out, label->text());
        out += ",\n";
        indent(out, in);
        out += "\"font\": ";
        writeString(out, label->fontFamily());
        out += ",\n";
        indent(out, in);
        out += "\"fontSize\": ";
        writeNumber(out, label->fontSizePx());
        out += ",\n";
        indent(out, in);
        out += "\"color\": ";
        writeColor(out, label->color());
    }

    if (!node.children().empty())
    {
        out += ",\n";
        indent(out, in);
        out += "\"children\": [\n";
        bool first = true;
        for (const auto& child : node.children())
        {
            if (!first)
                out += ",\n";
            first = false;
            indent(out, in + 1);
            writeNode(out, *child, in + 1);
        }
        out += '\n';
        indent(out, in);
        out += ']';
    }

    out += '\n';
    indent(out, level);
    out += '}';
}

struct Reader : json::Fields
{

    bool common(const Value& o, Node& node, const std::string& path)
    {
        std::string s;
        double n[4] = {};
        double d    = 0;
        bool b      = false;
        bool has    = false;

        if (!getString(o, "name", path, s, has))
            return false;
        if (has)
            node.setName(s);
        if (!getNumbers(o, "position", path, 2, n, has))
            return false;
        if (has)
            node.setPosition({static_cast<float>(n[0]), static_cast<float>(n[1])});
        if (!getNumber(o, "rotation", path, d, has))
            return false;
        if (has)
            node.setRotation(static_cast<float>(d));
        if (!getNumbers(o, "scale", path, 2, n, has))
            return false;
        if (has)
            node.setScale({static_cast<float>(n[0]), static_cast<float>(n[1])});
        if (!getNumbers(o, "anchor", path, 2, n, has))
            return false;
        if (has)
            node.setAnchorPoint({static_cast<float>(n[0]), static_cast<float>(n[1])});
        if (!getBool(o, "visible", path, b, has))
            return false;
        if (has)
            node.setVisible(b);
        if (!getNumber(o, "z", path, d, has))
            return false;
        if (has)
            node.setLocalZOrder(static_cast<int>(d));
        return true;
    }

    bool applySize(const Value& o, Node& node, const std::string& path)
    {
        double n[2] = {};
        bool has    = false;
        if (!getNumbers(o, "size", path, 2, n, has))
            return false;
        if (has)
            node.setContentSize({static_cast<float>(n[0]), static_cast<float>(n[1])});
        return true;
    }

    bool color(const Value& o, const std::string& path, renderer::Color& out, bool& has)
    {
        double n[4] = {};
        if (!getNumbers(o, "color", path, 4, n, has))
            return false;
        if (has)
            out = {static_cast<float>(n[0]), static_cast<float>(n[1]), static_cast<float>(n[2]),
                   static_cast<float>(n[3])};
        return true;
    }

    bool children(const Value& o, Node& parent, const std::string& path, int depth)
    {
        const Value* f = o.find("children");
        if (!f)
            return true;
        const auto* a = std::get_if<Array>(&f->v);
        if (!a)
            return fail(path, "\"children\" must be an array");
        for (std::size_t i = 0; i < a->size(); ++i)
        {
            auto child = node((*a)[i], path + "/children[" + std::to_string(i) + "]", depth + 1);
            if (!child)
                return false;
            parent.addChild(std::move(child));
        }
        return true;
    }

    std::unique_ptr<Node> node(const Value& o, const std::string& path, int depth)
    {
        if (depth > kMaxDepth)
        {
            fail(path, "nesting too deep");
            return nullptr;
        }
        if (!std::holds_alternative<Object>(o.v))
        {
            fail(path, "a node must be an object");
            return nullptr;
        }
        std::string type = "Node";
        bool has         = false;
        if (!getString(o, "type", path, type, has))
            return nullptr;

        std::unique_ptr<Node> out;
        if (type == "Node")
        {
            out = std::make_unique<Node>();
        }
        else if (type == "Sprite")
        {
            auto sprite = std::make_unique<Sprite>();
            std::string tex;
            if (!getString(o, "texture", path, tex, has))
                return nullptr;
            if (has && !tex.empty())
                sprite->setTexturePath(std::move(tex));
            renderer::Color c = sprite->color();
            if (!color(o, path, c, has))
                return nullptr;
            sprite->setColor(c);
            out = std::move(sprite);
        }
        else if (type == "Label")
        {
            std::string text, font = "Segoe UI";
            double size = 24.0;
            if (!getString(o, "text", path, text, has) || !getString(o, "font", path, font, has) ||
                !getNumber(o, "fontSize", path, size, has))
                return nullptr;
            auto label = std::make_unique<Label>(std::move(text), std::move(font), static_cast<float>(size));
            renderer::Color c = label->color();
            if (!color(o, path, c, has))
                return nullptr;
            label->setColor(c);
            out = std::move(label);
        }
        else
        {
            fail(path, "unknown node type \"" + type + "\"");
            return nullptr;
        }

        if (!common(o, *out, path) || !applySize(o, *out, path) || !children(o, *out, path, depth))
            return nullptr;
        return out;
    }
};

void setError(std::string* error, const std::string& msg)
{
    if (error)
        *error = msg;
}

}

std::string serializeScene(const Node& root)
{
    std::string out = "{\n  \"format\": \"zio.scene2d\",\n  \"version\": 1,\n  \"root\": ";
    writeNode(out, root, 1);
    out += "\n}\n";
    return out;
}

bool saveSceneFile(const Node& root, const std::string& path, std::string* error)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f)
    {
        setError(error, "cannot open \"" + path + "\" for writing");
        return false;
    }
    const std::string text = serializeScene(root);
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!f)
    {
        setError(error, "failed writing \"" + path + "\"");
        return false;
    }
    return true;
}

bool deserializeScene(Node& root, std::string_view json, std::string* error)
{
    if (json.size() >= 3 && static_cast<unsigned char>(json[0]) == 0xEF &&
        static_cast<unsigned char>(json[1]) == 0xBB && static_cast<unsigned char>(json[2]) == 0xBF)
        json.remove_prefix(3);

    Value doc;
    std::string err;
    if (!Parser(json).parse(doc, err))
    {
        setError(error, err);
        return false;
    }

    if (!std::holds_alternative<Object>(doc.v))
    {
        setError(error, "scene file must be a JSON object");
        return false;
    }
    const Value* fmt = doc.find("format");
    const auto* fmtStr = fmt ? std::get_if<std::string>(&fmt->v) : nullptr;
    if (!fmtStr || *fmtStr != "zio.scene2d")
    {
        setError(error, "not a zio.scene2d file (missing or wrong \"format\")");
        return false;
    }
    const Value* ver   = doc.find("version");
    const auto* verNum = ver ? std::get_if<double>(&ver->v) : nullptr;
    if (!verNum || *verNum != 1.0)
    {
        setError(error, "unsupported scene \"version\" (this build reads version 1)");
        return false;
    }
    const Value* rootVal = doc.find("root");
    if (!rootVal)
    {
        setError(error, "missing \"root\"");
        return false;
    }

    Reader reader;
    auto built = reader.node(*rootVal, "root", 0);
    if (!built)
    {
        setError(error, reader.err);
        return false;
    }

    if (dynamic_cast<Sprite*>(built.get()) || dynamic_cast<Label*>(built.get()))
    {
        setError(error, "root: the root node must be of type \"Node\"");
        return false;
    }

    root.removeAllChildren();
    root.setName(built->name());
    root.setPosition(built->position());
    root.setRotation(built->rotation());
    root.setScale(built->scale());
    root.setAnchorPoint(built->anchorPoint());
    root.setContentSize(built->contentSize());
    root.setVisible(built->isVisible());
    root.setLocalZOrder(built->localZOrder());
    while (!built->children().empty())
        root.addChild(built->removeChild(built->children().front().get()));
    return true;
}

bool loadSceneFile(Node& root, const std::string& path, std::string* error)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
    {
        setError(error, "cannot open \"" + path + "\"");
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return deserializeScene(root, ss.str(), error);
}

}
