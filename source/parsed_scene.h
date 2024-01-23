#ifndef PARSED_SCENE
#define PARSED_SCENE

#include "camera.h"
#include "renderer_material.h"
#include "triangle.h"

#include <vector>

struct ParsedScene
{
    std::vector<Triangle> triangles;
    std::vector<RendererMaterial> materials;

    std::vector<int> emissive_triangle_indices;
    std::vector<int> material_indices;

    bool has_camera = false;
    Camera camera;
};

#endif