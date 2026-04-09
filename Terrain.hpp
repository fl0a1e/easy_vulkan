#pragma once

#include "ModelLoader.hpp"

namespace terrain {
    modelLoading::MeshData CreatePlaneMesh(float size);
    modelLoading::MeshData CreatePlaneMesh(float width, float depth);
}
