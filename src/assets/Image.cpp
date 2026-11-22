#include "zio/assets/Image.h"

#include "zio/jobs/JobSystem.h"

#if defined(ZIO_PLATFORM_WINDOWS)

#define INITGUID
#include <wincodec.h>
#include <wrl/client.h>

#include <filesystem>

using Microsoft::WRL::ComPtr;

namespace zio::assets
{

namespace
{

struct ComThreadGuard
{
    HRESULT hr;
    ComThreadGuard() : hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComThreadGuard()
    {
        if (SUCCEEDED(hr))
            CoUninitialize();
    }
};

}

namespace
{

std::optional<Image> decodeFrame(IWICImagingFactory* factory, IWICBitmapDecoder* decoder)
{
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame)))
        return std::nullopt;

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter)))
        return std::nullopt;
    if (FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                      WICBitmapPaletteTypeCustom)))
        return std::nullopt;

    UINT width = 0, height = 0;
    if (FAILED(converter->GetSize(&width, &height)) || width == 0 || height == 0)
        return std::nullopt;

    Image image;
    image.width  = width;
    image.height = height;
    image.pixelsRgba8.resize(static_cast<std::size_t>(width) * height * 4);

    const UINT stride = width * 4;
    if (FAILED(converter->CopyPixels(nullptr, stride, static_cast<UINT>(image.pixelsRgba8.size()),
                                      image.pixelsRgba8.data())))
        return std::nullopt;

    return image;
}

}

std::optional<Image> decodeImageFile(std::string_view path)
{
    thread_local ComThreadGuard comGuard;

    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
        return std::nullopt;

    std::wstring widePath = std::filesystem::path(std::string(path)).wstring();

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(widePath.c_str(), nullptr, GENERIC_READ,
                                                   WICDecodeMetadataCacheOnDemand, &decoder)))
        return std::nullopt;
    return decodeFrame(factory.Get(), decoder.Get());
}

std::optional<Image> decodeImageMemory(const std::uint8_t* data, std::size_t size)
{
    thread_local ComThreadGuard comGuard;
    if (!data || size == 0)
        return std::nullopt;

    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
        return std::nullopt;

    ComPtr<IWICStream> stream;
    if (FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(data), static_cast<DWORD>(size))))
        return std::nullopt;

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)))
        return std::nullopt;
    return decodeFrame(factory.Get(), decoder.Get());
}

}

#else

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO_WIDE
#include <stb_image.h>

namespace zio::assets
{

std::optional<Image> decodeImageFile(std::string_view path)
{
    const std::string file(path);
    int width = 0, height = 0, channels = 0;
    stbi_uc* pixels = stbi_load(file.c_str(), &width, &height, &channels, 4);
    if (!pixels || width <= 0 || height <= 0)
    {
        if (pixels)
            stbi_image_free(pixels);
        return std::nullopt;
    }
    Image image;
    image.width  = static_cast<std::uint32_t>(width);
    image.height = static_cast<std::uint32_t>(height);
    image.pixelsRgba8.assign(pixels, pixels + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    stbi_image_free(pixels);
    return image;
}

std::optional<Image> decodeImageMemory(const std::uint8_t* data, std::size_t size)
{
    int width = 0, height = 0, channels = 0;
    stbi_uc* pixels = data && size ? stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &channels, 4)
                                   : nullptr;
    if (!pixels || width <= 0 || height <= 0)
    {
        if (pixels)
            stbi_image_free(pixels);
        return std::nullopt;
    }
    Image image;
    image.width  = static_cast<std::uint32_t>(width);
    image.height = static_cast<std::uint32_t>(height);
    image.pixelsRgba8.assign(pixels, pixels + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    stbi_image_free(pixels);
    return image;
}

}

#endif

namespace zio::assets
{

void loadImageAsync(std::string path, ImageLoadCallback onLoaded)
{
    jobs::run([path = std::move(path)]() { return decodeImageFile(path); },
              [onLoaded = std::move(onLoaded)](std::optional<Image> image) { onLoaded(std::move(image)); });
}

}
