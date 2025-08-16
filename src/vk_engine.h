// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_types.h>

struct SDL_Window;

namespace mnv {
    class VulkanEngine {
    private:
        struct FrameData {
            VkCommandPool                   commandPool;
            VkCommandBuffer                 commandBuffer;

            VkSemaphore                     swapchainSemaphore;
            VkFence                         renderFence;
        };
        struct SwapchainImageData {
            // The semaphore to signal rendering completion must be stored at a swapchain image granularity,
            // not inline with the number of frames the application wants to have inflight.
            VkSemaphore                     renderSemaphore;
        };

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

        std::vector<FrameData>          _frames;
        std::vector<SwapchainImageData> _swapchainImageData;
        VkQueue                         _graphicsQueue;
        std::uint32_t                   _graphicsQueueFamilyIndex;

        SDL_Window*                 _window{ nullptr };

        static VulkanEngine&        Get();

                                    //initializes everything in the engine
        void                        init();

                                    //shuts down the engine
        void                        cleanup();

                                    //draw loop
        void                        draw();

                                    //run main loop
        void                        run();

        inline FrameData&           getCurrentFrame() { return _frames[_frameNumber % _frames.size()]; }
        inline SwapchainImageData&  getSwapchainImageData(std::size_t index) { return _swapchainImageData[index % _swapchainImageData.size()]; }

    private:
        void                        initVulkan();
        void                        initSwapchain();
        void                        createSwapchain(std::uint32_t width, std::uint32_t height);
        void                        destroySwapchain();
        void                        initCommands();
        void                        initSynchronizationStructures();
    };
}
