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
        bool shouldGenerateNormals = false;

        for (const auto& shape : shapes) {
            for (const auto& index : shape.mesh.indices) {
                if (index.vertex_index < 0) {
                    std::cout << std::format(
                        "[ ModelLoader ] ERROR\nObj must provide vertex positions for every vertex: {}\n",
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
                const bool hasNormal = index.normal_index >= 0;
                const size_t normalBase = hasNormal ? static_cast<size_t>(index.normal_index) * 3 : 0;
                const bool hasTexcoord = index.texcoord_index >= 0;
                const size_t uvBase = hasTexcoord ? static_cast<size_t>(index.texcoord_index) * 2 : 0;

                if (positionBase + 2 >= attrib.vertices.size() ||
                    (hasNormal && normalBase + 2 >= attrib.normals.size()) ||
                    (hasTexcoord && uvBase + 1 >= attrib.texcoords.size())) {
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
                if (hasNormal) {
                    vertex.normal = {
                        attrib.normals[normalBase + 0],
                        attrib.normals[normalBase + 1],
                        attrib.normals[normalBase + 2]
                    };
                }
                else {
                    vertex.normal = { 0.0f, 0.0f, 0.0f };
                    shouldGenerateNormals = true;
                }
                if (hasTexcoord) {
                    vertex.uv = {
                        attrib.texcoords[uvBase + 0],
                        1.0f - attrib.texcoords[uvBase + 1]
                    };
                }
                else {
                    vertex.uv = { 0.0f, 0.0f };
                    mesh.hasTexcoord = false;
                }

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

        if (shouldGenerateNormals) {
            for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                const uint32_t i0 = mesh.indices[i + 0];
                const uint32_t i1 = mesh.indices[i + 1];
                const uint32_t i2 = mesh.indices[i + 2];

                const glm::vec3& p0 = mesh.vertices[i0].position;
                const glm::vec3& p1 = mesh.vertices[i1].position;
                const glm::vec3& p2 = mesh.vertices[i2].position;

                const glm::vec3 edge1 = p1 - p0;
                const glm::vec3 edge2 = p2 - p0;
                const glm::vec3 faceNormal = glm::cross(edge1, edge2);

                if (glm::length(faceNormal) == 0.0f)
                    continue;

                mesh.vertices[i0].normal += faceNormal;
                mesh.vertices[i1].normal += faceNormal;
                mesh.vertices[i2].normal += faceNormal;
            }

            for (auto& vertex : mesh.vertices) {
                if (glm::length(vertex.normal) == 0.0f)
                    vertex.normal = { 0.0f, 1.0f, 0.0f };
                else
                    vertex.normal = glm::normalize(vertex.normal);
            }

            std::cout << std::format(
                "[ ModelLoader ] WARNING\nObj has no normals. Generated smooth normals: {}\n",
                filepath.string());
        }

        const auto& warning = reader.Warning();
        if (!warning.empty()) {
            std::cout << std::format("[ ModelLoader ] WARNING\n{}\n", warning);
        }

        return mesh;
    }
}
