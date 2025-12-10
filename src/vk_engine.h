// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_types.h>
#include <vk_descriptors.h>
#include "vk_loader.hpp"

#include <vk_deletionQueue.hpp>

#include <string>

struct SDL_Window;

namespace mnv {
    struct GLTFMetallicRoughness {
        MaterialPipeline        opaquePipeline;
        MaterialPipeline        transparentPipeline;
        VkDescriptorSetLayout   materialLayout;

        // Use a 256byte alignment for now (massively wasteful though?)
        struct MaterialConstants {
            glm::vec4           colorFactor;
            glm::vec4           metalRoughFactor;
            glm::vec4           PADDING[14];
        };

        struct MaterialResources {
            AllocatedImage      colorImage;
            VkSampler           colorSampler;
            AllocatedImage      metalRoughImage;
            VkSampler           metalRoughSampler;
            VkBuffer            dataBuffer;
            std::uint32_t       dataBufferOffset;
        };

        DescriptorWriter        writer;

        void                    buildPipelines(VulkanEngine* engine);
        void                    clearResources(VkDevice device);
        MaterialInstance        writeMaterial(VkDevice device, MaterialPass pass, const MaterialResources& resources, DescriptorAllocatorGrowable& descriptorAllocator);
    };

    // 'Architecture'...
    struct RenderObject {
        std::uint32_t       indexCount;
        std::uint32_t       offset;
        VkBuffer            indexBuffer;

        MaterialInstance* material;

        glm::mat4           transform;
        VkDeviceAddress     vertexBufferAddress;
    };

    struct DrawContext {
        std::vector<RenderObject> opaqueObjects;
    };

    // NOTE: Looks like what vkguide calls a "surface" is infact a SubMesh?
    class MeshNode final : public Node {
    public:
        std::shared_ptr<MeshAsset> mesh;

        virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) final {
            const glm::mat4 nodeMatrix = topMatrix * worldTransform;

            for (auto& surface : mesh->surfaces) {
                RenderObject object;
                object.indexCount = surface.count;
                object.offset = surface.startIndex;
                object.indexBuffer = mesh->meshBuffers.index.buffer;
                object.material = &surface.material->data;
                object.transform = nodeMatrix;
                object.vertexBufferAddress = mesh->meshBuffers.vertexAddress;
                ctx.opaqueObjects.push_back(object);
            }

            // Recurse down
            Node::Draw(topMatrix, ctx);
        }
    };
    // ---

    class VulkanEngine {
    private:
        struct GPUSceneData {
            glm::mat4 view;
            glm::mat4 proj;
            glm::mat4 viewProj;
            glm::vec4 ambientColor;
            glm::vec4 sunlightDirection; // w for sun power
            glm::vec4 sunlightColor;
        };

        struct FrameData {
            VkCommandPool                       commandPool;
            VkCommandBuffer                     commandBuffer;

            VkSemaphore                         swapchainSemaphore;
            VkFence                             renderFence;

            mnv::DeletionQueue                  deletionQueue;
            mnv::DescriptorAllocatorGrowable    frameDescriptors;
        };
        struct SwapchainImageData {
            // The semaphore to signal rendering completion must be stored at a swapchain image granularity,
            // not inline with the number of frames the application wants to have inflight.
            VkSemaphore renderSemaphore;
        };

        struct ComputePushConstants {
            glm::vec4 data0;
            glm::vec4 data1;
            glm::vec4 data2;
            glm::vec4 data3;
        };
        struct ComputeEffect {
            std::string             name;

            VkPipeline              pipeline;
            VkPipelineLayout        layout;

            ComputePushConstants    data;
        };
        std::vector<ComputeEffect>  backgroundEffects;
        std::size_t                 currentBackgroundEffect {0};

        mnv::Meshes                 testMeshes;

    public:

        // handle our internal state
        bool                        _isInitialized{ false };
        bool                        _resizeRequested{ false };
        int                         _frameNumber{ 0 };
        bool                        _stopRendering{ false };

        // Vulkan-specifics
        VkInstance                  _instance;
        VkDebugUtilsMessengerEXT    _debugMessenger;
        VkPhysicalDevice            _physicalDevice;
        VkDevice                    _logicalDevice;
        VkSurfaceKHR                _surface;
        VkExtent2D                  _windowExtent{ 1700 , 900 };
        VkSwapchainKHR              _swapchain;
        VkFormat                    _swapchainImageFormat;
        std::vector<VkImage>        _swapchainImages;
        std::vector<VkImageView>    _swapchainImageViews;
        VkExtent2D                  _swapchainExtent;

        VmaAllocator                _vmaAllocator;

        std::vector<FrameData>          _frames;
        std::vector<SwapchainImageData> _swapchainImageData;
        VkQueue                         _graphicsQueue;
        std::uint32_t                   _graphicsQueueFamilyIndex;

        SDL_Window*                 _window{ nullptr };

        mnv::DeletionQueue          _mainDeletionQueue;

        // Draw resources
        mnv::AllocatedImage         _drawImage;
        mnv::AllocatedImage         _depthImage;
        VkExtent2D                  _drawExtent;
        float                       _renderScale{ 1.0f };

        DescriptorAllocatorGrowable _globalDescriptorAllocator;
        VkDescriptorSet             _drawImageDescriptor;
        VkDescriptorSetLayout       _drawImageDescriptorLayout;
        VkPipeline                  _gradientPipeline;
        VkPipelineLayout            _gradientPipelineLayout;
        VkPipeline                  _meshPipeline;
        VkPipelineLayout            _meshPipelineLayout;

        GPUSceneData                _sceneData;
        VkDescriptorSetLayout       _gpuSceneDataDescriptorSetLayout;

        // For use with imgui
        VkFence                     _immFence;
        VkCommandBuffer             _immCommandBuffer;
        VkCommandPool               _immCommandPool;
        // ---

        // Textures
        VkDescriptorSetLayout       _singleImageDescriptorLayout;

        mnv::AllocatedImage         _whiteImage;
        mnv::AllocatedImage         _blackImage;
        mnv::AllocatedImage         _greyImage;
        mnv::AllocatedImage         _errorCheckerboardImage;

        VkSampler                   _defaultSamplerLinear;
        VkSampler                   _defaultSamplerNearest;
        // ---

        // Default resources
        MaterialInstance            _defaultMaterial;
        GLTFMetallicRoughness       _metallicRoughnessMaterial;
        // ---

        // 'Architecture' rework
        DrawContext                 _mainDrawContext;
        std::unordered_map<std::string, std::shared_ptr<mnv::Node>> _loadedNodes;

        void                        UpdateScene();
        // ---

        static VulkanEngine&        Get();

                                    //initializes everything in the engine
        void                        init();

                                    //shuts down the engine
        void                        cleanup();

                                    //draw loop
        void                        draw();
        void                        drawBackground(VkCommandBuffer commandBuffer);
        void                        drawGeometry(VkCommandBuffer commandBuffer);
        void                        drawImgui(VkCommandBuffer commandBuffer, VkImageView targetImageView);

                                    //run main loop
        void                        run();

        std::size_t currentswapimage = 0;

        inline std::size_t          getCurrentFrameIndex() const { return _frameNumber % _frames.size(); }
        inline FrameData&           getCurrentFrame() { return _frames[_frameNumber % _frames.size()]; }
        inline FrameData&           getNextFrame() { return _frames[(_frameNumber+1) % _frames.size()]; }
        inline SwapchainImageData&  getSwapchainImageData(std::size_t index) { return _swapchainImageData[index % _swapchainImageData.size()]; }

        void                        immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function);

        mnv::GPUMeshBuffers         uploadMesh(std::span<mnv::Vertex> vertices, std::span<std::uint32_t> indices);

    private:
        void                        initVulkan();
        void                        initSwapchain();
        void                        createSwapchain(std::uint32_t width, std::uint32_t height);
        void                        destroySwapchain();
        void                        initCommands();
        void                        initDescriptors();
        void                        initPipelines();
        void                        initBackgroundPipelines();
        void                        initMeshPipeline();
        void                        initSynchronizationStructures();
        void                        initDefaultData();

        void                        initImgui();

        mnv::AllocatedBuffer        createBuffer(std::size_t size, VkBufferUsageFlags flags, VmaMemoryUsage usage);
        void                        destroyBuffer(const AllocatedBuffer& buffer);

        AllocatedImage              createImage(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
        AllocatedImage              createImage(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
        void                        destroyImage(const AllocatedImage& img);

        void                        resizeSwapchain();
    };
}
