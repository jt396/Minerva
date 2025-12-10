// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <span>
#include <array>
#include <functional>
#include <deque>

#include <vulkan/vulkan.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vk_mem_alloc.h>

//#include "vk_loader.hpp"

#include <fmt/core.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>


#define VK_CHECK(x)                                                             \
    do {                                                                        \
        VkResult err = x;                                                       \
        if (err) {                                                              \
            fmt::println("Detected Vulkan error: {}", string_VkResult(err));    \
            abort();                                                            \
        }                                                                       \
    } while (0)

namespace mnv {
    struct AllocatedImage {
        VkImage         image;
        VkImageView     view;
        VmaAllocation   allocation;
        VkExtent3D      imageExtent;
        VkFormat        imageFormat;
    };

    struct AllocatedBuffer {
        VkBuffer            buffer;
        VmaAllocation       allocation;
        VmaAllocationInfo   info;
    };

    struct Vertex {
        glm::vec3 position;
        float     uvX;
        glm::vec3 normal;
        float     uvY;
        glm::vec4 color;
    };

    // Holds resources needed for a mesh
    struct GPUMeshBuffers {
        AllocatedBuffer vertex;
        AllocatedBuffer index;
        VkDeviceAddress vertexAddress;
    };
    // Push constants for mesh object draws
    struct GPUDrawPushConstants {
        glm::mat4       worldMatrix;
        VkDeviceAddress address;
    };

    // Abstractions/higher-level structures
    enum class MaterialPass : std::uint8_t {
        MainColor = 0,
        Transparent,
        Other
    };
    struct MaterialPipeline {
        VkPipeline          pipeline;
        VkPipelineLayout    layout;
    };
    struct MaterialInstance {
        MaterialPipeline*   pipeline;
        VkDescriptorSet     descriptorSet;
        MaterialPass        passType;
    };

    // MAYDO : Maybe this could be reworked to be compile-time (concepts etc.)
    struct DrawContext;

    class IRenderable {
    public:
        virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) = 0;
    };

    // Implementation of a drawable scene node.
    // The scene node can hold children and will also keep a transform to propagate to them.
    class Node : public IRenderable {
    public:
        // Parent pointer must be a weak pointer to avoid circular dependencies
        std::weak_ptr<Node> parent;
        std::vector<std::shared_ptr<Node>> children;

        glm::mat4 localTransform;
        glm::mat4 worldTransform;

        void refreshTransform(const glm::mat4& parentMatrix) {
            worldTransform = parentMatrix * localTransform;
            for (auto child : children) {
                child->refreshTransform(worldTransform);
            }
        }

        virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) override {
            for (auto& child : children) {
                child->Draw(topMatrix, ctx);
            }
        }
    };
}
