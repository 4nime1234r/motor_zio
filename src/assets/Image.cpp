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

#else

namespace zio::assets
{

std::optional<Image> decodeImageFile(std::string_view)
{
    return std::nullopt;
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
