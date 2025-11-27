
#include "vk_loader.hpp"

#include "stb_image.h"
#include <iostream>
#include <vk_loader.h>

#include "vk_engine.h"
#include "vk_initializers.h"
#include "vk_types.h"
#include <glm/gtx/quaternion.hpp>

#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/parser.hpp>
#include <fastgltf/tools.hpp>

std::optional<mnv::Meshes> mnv::loadGltfMeshes(mnv::VulkanEngine* engine, std::filesystem::path filePath) {
    std::cout << "Loading GLTF: " << filePath << std::endl;

    fastgltf::GltfDataBuffer data;
    data.loadFromFile(filePath);

    constexpr auto gltfOptions = fastgltf::Options::LoadGLBBuffers | fastgltf::Options::LoadExternalBuffers;

    fastgltf::Asset gltf;
    fastgltf::Parser parser{};

    if (auto loaded = parser.loadBinaryGLTF(&data, filePath.parent_path(), gltfOptions); loaded) {
        gltf = std::move(loaded.get());
    } else {
        fmt::print("Failed to load glTF: {} \n", fastgltf::to_underlying(loaded.error()));
        return {};
    }

    mnv::Meshes meshes;

    std::vector<std::uint32_t> indices;
    std::vector<mnv::Vertex> vertices;

    for (fastgltf::Mesh& mesh : gltf.meshes) {
        mnv::MeshAsset newmesh;

        newmesh.name = mesh.name;

        // clear the mesh arrays each mesh, we dont want to merge them by error
        indices.clear();
        vertices.clear();

        for (auto&& primitive : mesh.primitives) {
            mnv::GeoSurface newSurface;
            newSurface.startIndex = static_cast<std::uint32_t>(indices.size());
            newSurface.count = static_cast<std::uint32_t>(gltf.accessors[primitive.indicesAccessor.value()].count);

            size_t initial_vtx = vertices.size();

            // load indexes
            {
                const fastgltf::Accessor& indexaccessor = gltf.accessors[primitive.indicesAccessor.value()];
                indices.reserve(indices.size() + indexaccessor.count);

                fastgltf::iterateAccessor<std::uint32_t>(gltf, indexaccessor, [&] (std::uint32_t idx) {
                    indices.push_back(idx + initial_vtx);
                });
            }

            // load vertex positions
            {
                const fastgltf::Accessor& posAccessor = gltf.accessors[primitive.findAttribute("POSITION")->second];
                vertices.resize(vertices.size() + posAccessor.count);

                fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, posAccessor, [&] (glm::vec3 v, size_t index) {
                    mnv::Vertex newvtx;
                        newvtx.position = v;
                        newvtx.normal = { 1, 0, 0 };
                        newvtx.color = glm::vec4{ 1.f };
                        newvtx.uvX = 0;
                        newvtx.uvY = 0;
                    vertices[initial_vtx + index] = newvtx;
                });
            }

            // load vertex normals
            if (const auto normals = primitive.findAttribute("NORMAL"); normals != primitive.attributes.end()) {
                fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[(*normals).second], [&] (glm::vec3 v, size_t index) {
                    vertices[initial_vtx + index].normal = v;
                });
            }

            // load UVs
            if (const auto uv = primitive.findAttribute("TEXCOORD_0"); uv != primitive.attributes.end()) {
                fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[(*uv).second], [&] (glm::vec2 v, size_t index) {
                    vertices[initial_vtx + index].uvX = v.x;
                    vertices[initial_vtx + index].uvY = v.y;
                });
            }

            // load vertex colors
            if (const auto colors = primitive.findAttribute("COLOR_0"); colors != primitive.attributes.end()) {
                fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[(*colors).second], [&] (glm::vec4 v, size_t index) {
                    vertices[initial_vtx + index].color = v;
                });
            }
            newmesh.surfaces.push_back(newSurface);
        }

        // display the vertex normals
        constexpr bool OverrideColors = true;
        if (OverrideColors) {
            for (mnv::Vertex& vtx : vertices) {
                vtx.color = glm::vec4(vtx.normal, 1.f);
            }
        }

        newmesh.meshBuffers = engine->uploadMesh(vertices, indices);
        meshes.emplace_back(std::make_shared<mnv::MeshAsset>(std::move(newmesh)));
    }

    return meshes;
}
