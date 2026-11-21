#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

namespace zio_editor
{

class EditorWindow
{
public:
    EditorWindow();
    ~EditorWindow();

    EditorWindow(const EditorWindow&)            = delete;
    EditorWindow& operator=(const EditorWindow&) = delete;

    bool create(std::string_view title, std::uint32_t width, std::uint32_t height);
    void destroy();

    void pumpMessages();

    bool closeRequested() const;
    void cancelClose();
    void* nativeHandle() const;
    std::uint32_t width() const;
    std::uint32_t height() const;

    bool resized() const;
    void consumeResized();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
