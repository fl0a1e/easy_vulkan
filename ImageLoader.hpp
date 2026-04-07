#pragma once

#include "vkStart.h"
#include <filesystem>

namespace imageLoading {
    struct imageData {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> pixels;
    };

    imageData LoadRgba8(const std::filesystem::path& filepath);
}
