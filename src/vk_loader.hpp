#pragma once

#include "vk_types.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>

namespace mnv {
    class VulkanEngine;

    struct GLTFMaterial {
        mnv::MaterialInstance data;
    };
    // MAYDO: Rename this to SubMesh since that's essentially what it is...
    struct GeoSurface {
        std::uint32_t                   startIndex;
        std::uint32_t                   count;
        std::shared_ptr<GLTFMaterial>   material;
    };

    struct MeshAsset {
        std::string             name;
        std::vector<mnv::GeoSurface> surfaces;
        mnv::GPUMeshBuffers     meshBuffers;
    };

    using Meshes = std::vector<std::shared_ptr<mnv::MeshAsset>>;
    std::optional<mnv::Meshes> loadGltfMeshes(mnv::VulkanEngine* engine, std::filesystem::path filePath);
}

