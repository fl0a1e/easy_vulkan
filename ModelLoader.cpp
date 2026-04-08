#include "ModelLoader.hpp"

#define TINYOBJLOADER_DISABLE_FAST_FLOAT
#define TINYOBJLOADER_IMPLEMENTATION
#include <tinyobjloader/tiny_obj_loader.h>

#include <unordered_map>

namespace {
    struct VertexKey {
        int vertexIndex = -1;
        int normalIndex = -1;
        int texcoordIndex = -1;

        bool operator==(const VertexKey& other) const = default;
    };

    struct VertexKeyHash {
        size_t operator()(const VertexKey& key) const noexcept {
            size_t h = std::hash<int>{}(key.vertexIndex);
            h ^= std::hash<int>{}(key.normalIndex) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<int>{}(key.texcoordIndex) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };
}

namespace modelLoading {
    MeshData LoadObj(const std::filesystem::path& filepath) {
        tinyobj::ObjReaderConfig config;
        config.triangulate = true;
        config.mtl_search_path = filepath.parent_path().string();

        tinyobj::ObjReader reader;
        if (!reader.ParseFromFile(filepath.string(), config)) {
            const auto& error = reader.Error();
            std::cout << std::format(
                "[ ModelLoader ] ERROR\nFailed to load obj: {}\n{}\n",
                filepath.string(),
                error.empty() ? "Unknown error." : error);
            abort();
        }

        const auto& attrib = reader.GetAttrib();
        const auto& shapes = reader.GetShapes();

        if (shapes.empty()) {
            std::cout << std::format("[ ModelLoader ] ERROR\nObj has no shapes: {}\n", filepath.string());
            abort();
        }
        if (attrib.vertices.empty()) {
            std::cout << std::format("[ ModelLoader ] ERROR\nObj has no vertex positions: {}\n", filepath.string());
            abort();
        }

        MeshData mesh;
        std::unordered_map<VertexKey, uint32_t, VertexKeyHash> vertexToIndex;

        for (const auto& shape : shapes) {
            for (const auto& index : shape.mesh.indices) {
                if (index.vertex_index < 0 || index.normal_index < 0 || index.texcoord_index < 0) {
                    std::cout << std::format(
                        "[ ModelLoader ] ERROR\nObj must provide position, normal and uv for every vertex: {}\n",
                        filepath.string());
                    abort();
                }

                VertexKey key{
                    .vertexIndex = index.vertex_index,
                    .normalIndex = index.normal_index,
                    .texcoordIndex = index.texcoord_index
                };

                if (const auto found = vertexToIndex.find(key); found != vertexToIndex.end()) {
                    mesh.indices.push_back(found->second);
                    continue;
                }

                const size_t positionBase = static_cast<size_t>(index.vertex_index) * 3;
                const size_t normalBase = static_cast<size_t>(index.normal_index) * 3;
                const size_t uvBase = static_cast<size_t>(index.texcoord_index) * 2;

                if (positionBase + 2 >= attrib.vertices.size() ||
                    normalBase + 2 >= attrib.normals.size() ||
                    uvBase + 1 >= attrib.texcoords.size()) {
                    std::cout << std::format(
                        "[ ModelLoader ] ERROR\nObj index is out of range: {}\n",
                        filepath.string());
                    abort();
                }

                Vertex vertex{};
                vertex.position = {
                    attrib.vertices[positionBase + 0],
                    attrib.vertices[positionBase + 1],
                    attrib.vertices[positionBase + 2]
                };
                vertex.normal = {
                    attrib.normals[normalBase + 0],
                    attrib.normals[normalBase + 1],
                    attrib.normals[normalBase + 2]
                };
                vertex.uv = {
                    attrib.texcoords[uvBase + 0],
                    1.0f - attrib.texcoords[uvBase + 1]
                };

                const uint32_t newIndex = static_cast<uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back(vertex);
                mesh.indices.push_back(newIndex);
                vertexToIndex.emplace(key, newIndex);
            }
        }

        if (mesh.vertices.empty() || mesh.indices.empty()) {
            std::cout << std::format("[ ModelLoader ] ERROR\nObj produced empty mesh data: {}\n", filepath.string());
            abort();
        }

        const auto& warning = reader.Warning();
        if (!warning.empty()) {
            std::cout << std::format("[ ModelLoader ] WARNING\n{}\n", warning);
        }

        return mesh;
    }
}
