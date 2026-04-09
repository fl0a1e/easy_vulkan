#include "Terrain.hpp"

namespace terrain {
    modelLoading::MeshData CreatePlaneMesh(float size) {
        return CreatePlaneMesh(size, size);
    }

    modelLoading::MeshData CreatePlaneMesh(float width, float depth) {
        const float halfWidth = width * 0.5f;
        const float halfDepth = depth * 0.5f;

        modelLoading::MeshData mesh;
        mesh.hasTexcoord = true;
        mesh.vertices = {
            { {-halfWidth, 0.0f, -halfDepth}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f} },
            { { halfWidth, 0.0f, -halfDepth}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f} },
            { { halfWidth, 0.0f,  halfDepth}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f} },
            { {-halfWidth, 0.0f,  halfDepth}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f} }
        };
        mesh.indices = { 0, 2, 1, 2, 0, 3 };
        return mesh;
    }
}
