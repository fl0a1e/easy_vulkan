#include "ImageLoader.hpp"

#include <wincodec.h>
#include <gdiplus.h>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "gdiplus.lib")

namespace imageLoading {
    namespace {
        imageData LoadRgba8WithGdiPlus(const std::filesystem::path& filepath) {
            Gdiplus::GdiplusStartupInput startupInput;
            ULONG_PTR token = 0;
            const Gdiplus::Status startupStatus = Gdiplus::GdiplusStartup(&token, &startupInput, nullptr);
            if (startupStatus != Gdiplus::Ok) {
                std::cout << std::format(
                    "[ ImageLoader ] ERROR\nGDI+ startup failed for image: {}\nStatus: {}\n",
                    filepath.string(),
                    static_cast<int>(startupStatus));
                abort();
            }

            auto shutdownGdi = [&]() {
                if (token)
                    Gdiplus::GdiplusShutdown(token);
            };

            imageData image;

            {
                Gdiplus::Bitmap bitmap(filepath.c_str());
                if (bitmap.GetLastStatus() != Gdiplus::Ok) {
                    std::cout << std::format(
                        "[ ImageLoader ] ERROR\nGDI+ failed to open image: {}\nStatus: {}\n",
                        filepath.string(),
                        static_cast<int>(bitmap.GetLastStatus()));
                    shutdownGdi();
                    abort();
                }

                const UINT width = bitmap.GetWidth();
                const UINT height = bitmap.GetHeight();
                if (width == 0 || height == 0) {
                    std::cout << std::format("[ ImageLoader ] ERROR\nGDI+ reported invalid image size: {}\n", filepath.string());
                    shutdownGdi();
                    abort();
                }

                Gdiplus::Rect rect(0, 0, static_cast<INT>(width), static_cast<INT>(height));
                Gdiplus::BitmapData bitmapData{};
                const Gdiplus::Status lockStatus = bitmap.LockBits(
                    &rect,
                    Gdiplus::ImageLockModeRead,
                    PixelFormat32bppARGB,
                    &bitmapData);
                if (lockStatus != Gdiplus::Ok) {
                    std::cout << std::format(
                        "[ ImageLoader ] ERROR\nGDI+ failed to lock image bits: {}\nStatus: {}\n",
                        filepath.string(),
                        static_cast<int>(lockStatus));
                    shutdownGdi();
                    abort();
                }

                image.width = width;
                image.height = height;
                image.pixels.resize(static_cast<size_t>(width) * height * 4);

                for (UINT y = 0; y < height; ++y) {
                    const auto* srcRow = static_cast<const uint8_t*>(bitmapData.Scan0) + static_cast<ptrdiff_t>(y) * bitmapData.Stride;
                    auto* dstRow = image.pixels.data() + static_cast<size_t>(y) * width * 4;
                    for (UINT x = 0; x < width; ++x) {
                        const uint8_t b = srcRow[x * 4 + 0];
                        const uint8_t g = srcRow[x * 4 + 1];
                        const uint8_t r = srcRow[x * 4 + 2];
                        const uint8_t a = srcRow[x * 4 + 3];
                        dstRow[x * 4 + 0] = r;
                        dstRow[x * 4 + 1] = g;
                        dstRow[x * 4 + 2] = b;
                        dstRow[x * 4 + 3] = a;
                    }
                }

                bitmap.UnlockBits(&bitmapData);
            }
            shutdownGdi();
            return image;
        }
    }

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

        auto fallbackToGdiPlus = [&]() {
            releaseAll();
            return LoadRgba8WithGdiPlus(filepath);
        };

        hr = CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory));
        if (FAILED(hr)) {
            std::cout << std::format("[ ImageLoader ] WARNING\nWIC factory creation failed, falling back to GDI+: HRESULT 0x{:08X}\n", static_cast<uint32_t>(hr));
            return fallbackToGdiPlus();
        }

        hr = factory->CreateDecoderFromFilename(
            filepath.c_str(),
            nullptr,
            GENERIC_READ,
            WICDecodeMetadataCacheOnLoad,
            &decoder);
        if (FAILED(hr)) {
            std::cout << std::format("[ ImageLoader ] WARNING\nWIC failed to open image, falling back to GDI+: {}\nHRESULT: 0x{:08X}\n", filepath.string(), static_cast<uint32_t>(hr));
            return fallbackToGdiPlus();
        }

        hr = decoder->GetFrame(0, &frame);
        if (FAILED(hr)) {
            std::cout << std::format("[ ImageLoader ] WARNING\nWIC failed to get image frame, falling back to GDI+: {}\nHRESULT: 0x{:08X}\n", filepath.string(), static_cast<uint32_t>(hr));
            return fallbackToGdiPlus();
        }

        hr = factory->CreateFormatConverter(&converter);
        if (FAILED(hr)) {
            std::cout << std::format("[ ImageLoader ] WARNING\nWIC failed to create format converter, falling back to GDI+: HRESULT 0x{:08X}\n", static_cast<uint32_t>(hr));
            return fallbackToGdiPlus();
        }

        hr = converter->Initialize(
            frame,
            GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom);
        if (FAILED(hr)) {
            std::cout << std::format("[ ImageLoader ] WARNING\nWIC failed to convert image to RGBA8, falling back to GDI+: {}\nHRESULT: 0x{:08X}\n", filepath.string(), static_cast<uint32_t>(hr));
            return fallbackToGdiPlus();
        }

        imageData image;
        hr = converter->GetSize(&image.width, &image.height);
        if (FAILED(hr) || image.width == 0 || image.height == 0) {
            std::cout << std::format("[ ImageLoader ] WARNING\nWIC reported invalid image size, falling back to GDI+: {}\nHRESULT: 0x{:08X}\n", filepath.string(), static_cast<uint32_t>(hr));
            return fallbackToGdiPlus();
        }

        const uint32_t rowPitch = image.width * 4;
        image.pixels.resize(static_cast<size_t>(rowPitch) * image.height);
        hr = converter->CopyPixels(
            nullptr,
            rowPitch,
            static_cast<uint32_t>(image.pixels.size()),
            image.pixels.data());
        if (FAILED(hr)) {
            std::cout << std::format("[ ImageLoader ] WARNING\nWIC failed to copy image pixels, falling back to GDI+: {}\nHRESULT: 0x{:08X}\n", filepath.string(), static_cast<uint32_t>(hr));
            return fallbackToGdiPlus();
        }

        releaseAll();
        return image;
    }
}
