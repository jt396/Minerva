// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_types.h>
#include <vk_descriptors.h>

#include <vk_deletionQueue.hpp>

#include <string>

struct SDL_Window;

namespace mnv {
    class VulkanEngine {
    private:
        struct FrameData {
            VkCommandPool       commandPool;
            VkCommandBuffer     commandBuffer;

            VkSemaphore         swapchainSemaphore;
            VkFence             renderFence;

            mnv::DeletionQueue  deletionQueue;
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

    public:

        // handle our internal state
        bool                        _isInitialized{ false };
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
        VkExtent2D                  _drawExtent;

        DescriptorAllocator         globalDescriptorAllocator;
        VkDescriptorSet             _drawImageDescriptor;
        VkDescriptorSetLayout       _drawImageDescriptorLayout;
        VkPipeline                  _gradientPipeline;
        VkPipelineLayout            _gradientPipelineLayout;

        // For use with imgui
        VkFence                     _immFence;
        VkCommandBuffer             _immCommandBuffer;
        VkCommandPool               _immCommandPool;
        // ---

        static VulkanEngine&        Get();

                                    //initializes everything in the engine
        void                        init();

                                    //shuts down the engine
        void                        cleanup();

                                    //draw loop
        void                        draw();
        void                        drawBackground(VkCommandBuffer commandBuffer);
        void                        drawImgui(VkCommandBuffer commandBuffer, VkImageView targetImageView);

                                    //run main loop
        void                        run();

        std::size_t currentswapimage = 0;

        inline std::size_t          getCurrentFrameIndex() const { return _frameNumber % _frames.size(); }
        inline FrameData&           getCurrentFrame() { return _frames[_frameNumber % _frames.size()]; }
        inline FrameData&           getNextFrame() { return _frames[(_frameNumber+1) % _frames.size()]; }
        inline SwapchainImageData&  getSwapchainImageData(std::size_t index) { return _swapchainImageData[index % _swapchainImageData.size()]; }

        void                        immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function);

    private:
        void                        initVulkan();
        void                        initSwapchain();
        void                        createSwapchain(std::uint32_t width, std::uint32_t height);
        void                        destroySwapchain();
        void                        initCommands();
        void                        initDescriptors();
        void                        initPipelines();
        void                        initBackgroundPipelines();
        void                        initSynchronizationStructures();

        void                        initImgui();
    };
}
