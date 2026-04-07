#include "ImageLoader.hpp"

#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")

namespace imageLoading {
    imageData LoadRgba8(const std::filesystem::path& filepath) {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool shouldUninitialize = SUCCEEDED(hr);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
            std::cout << std::format("[ ImageLoader ] ERROR\nFailed to initialize COM. HRESULT: 0x{:08X}\n", static_cast<uint32_t>(hr));
            abort();
        }

        IWICImagingFactory* factory = nullptr;
        IWICBitmapDecoder* decoder = nullptr;
        IWICBitmapFrameDecode* frame = nullptr;
        IWICFormatConverter* converter = nullptr;

        auto releaseAll = [&]() {
            if (converter) converter->Release();
            if (frame) frame->Release();
            if (decoder) decoder->Release();
            if (factory) factory->Release();
            if (shouldUninitialize) CoUninitialize();
        };

        hr = CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory));
        if (FAILED(hr)) {
            std::cout << std::format("[ ImageLoader ] ERROR\nFailed to create WIC factory. HRESULT: 0x{:08X}\n", static_cast<uint32_t>(hr));
            releaseAll();
            abort();
        }

        hr = factory->CreateDecoderFromFilename(
            filepath.c_str(),
            nullptr,
            GENERIC_READ,
            WICDecodeMetadataCacheOnLoad,
            &decoder);
        if (FAILED(hr)) {
            std::cout << std::format("[ ImageLoader ] ERROR\nFailed to open image: {}\nHRESULT: 0x{:08X}\n", filepath.string(), static_cast<uint32_t>(hr));
            releaseAll();
            abort();
        }

        hr = decoder->GetFrame(0, &frame);
        if (FAILED(hr)) {
            std::cout << std::format("[ ImageLoader ] ERROR\nFailed to get image frame: {}\nHRESULT: 0x{:08X}\n", filepath.string(), static_cast<uint32_t>(hr));
            releaseAll();
            abort();
        }

        hr = factory->CreateFormatConverter(&converter);
        if (FAILED(hr)) {
            std::cout << std::format("[ ImageLoader ] ERROR\nFailed to create WIC format converter. HRESULT: 0x{:08X}\n", static_cast<uint32_t>(hr));
            releaseAll();
            abort();
        }

        hr = converter->Initialize(
            frame,
            GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom);
        if (FAILED(hr)) {
            std::cout << std::format("[ ImageLoader ] ERROR\nFailed to convert image to RGBA8: {}\nHRESULT: 0x{:08X}\n", filepath.string(), static_cast<uint32_t>(hr));
            releaseAll();
            abort();
        }

        imageData image;
        hr = converter->GetSize(&image.width, &image.height);
        if (FAILED(hr) || image.width == 0 || image.height == 0) {
            std::cout << std::format("[ ImageLoader ] ERROR\nInvalid image size: {}\nHRESULT: 0x{:08X}\n", filepath.string(), static_cast<uint32_t>(hr));
            releaseAll();
            abort();
        }

        const uint32_t rowPitch = image.width * 4;
        image.pixels.resize(static_cast<size_t>(rowPitch) * image.height);
        hr = converter->CopyPixels(
            nullptr,
            rowPitch,
            static_cast<uint32_t>(image.pixels.size()),
            image.pixels.data());
        if (FAILED(hr)) {
            std::cout << std::format("[ ImageLoader ] ERROR\nFailed to copy image pixels: {}\nHRESULT: 0x{:08X}\n", filepath.string(), static_cast<uint32_t>(hr));
            releaseAll();
            abort();
        }

        releaseAll();
        return image;
    }
}
