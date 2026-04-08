#pragma once

#include "vkStart.h"
#include <filesystem>

namespace modelLoading {
    struct Vertex {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec2 uv;
    };

    struct MeshData {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        bool hasTexcoord = true;
    };

    MeshData LoadObj(const std::filesystem::path& filepath);
}
