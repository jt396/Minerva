// Includes
#include "vk_engine.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include "vk_initializers.h"
#include "vk_types.h"
#include "vk_images.h"

#include <VkBootstrap.h>

#include <chrono>
#include <thread>

// Aliases
using mnv::VulkanEngine;

// Forward declarations
VulkanEngine* loadedEngine = nullptr;

namespace helpers {
    constexpr std::uint64_t FENCE_TIMEOUT_NS = 1000000000;
}

// Class impl
VulkanEngine& VulkanEngine::Get() {
    return *loadedEngine;
}

void VulkanEngine::init() {
    // only one engine initialization is allowed with the application.
    assert(loadedEngine == nullptr);
    loadedEngine = this;

    // We initialize SDL and create a window with it.
    SDL_Init(SDL_INIT_VIDEO);

    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);

    _window = SDL_CreateWindow("Minerva",
                               SDL_WINDOWPOS_UNDEFINED,
                               SDL_WINDOWPOS_UNDEFINED,
                               _windowExtent.width,
                               _windowExtent.height,
                               window_flags);

    initVulkan();
    initSwapchain();
    initCommands();
    initSynchronizationStructures();

    // everything went fine
    _isInitialized = true;
}

void VulkanEngine::cleanup() {
    if (_isInitialized) {
        vkDeviceWaitIdle(_logicalDevice);
        for (auto& frame : _frames) {
            vkDestroyCommandPool(_logicalDevice, frame.commandPool, nullptr);

            vkDestroyFence(_logicalDevice, frame.renderFence, nullptr);
            vkDestroySemaphore(_logicalDevice, frame.swapchainSemaphore, nullptr);
        }
        for (auto& imageData : _swapchainImageData) {
            vkDestroySemaphore(_logicalDevice, imageData.renderSemaphore, nullptr);
        }

        destroySwapchain();

        vkDestroySurfaceKHR(_instance, _surface, nullptr);
        vkDestroyDevice(_logicalDevice, nullptr);

        vkb::destroy_debug_utils_messenger(_instance, _debugMessenger);
        vkDestroyInstance(_instance, nullptr);

        SDL_DestroyWindow(_window);
    }
    loadedEngine = nullptr;
}

void VulkanEngine::draw() {
    // Wait for the GPU to finish the current frame before trying to submit the next.
    VK_CHECK(vkWaitForFences(_logicalDevice, 1, &getCurrentFrame().renderFence, VK_TRUE, UINT64_MAX));
    VK_CHECK(vkResetFences(_logicalDevice, 1, &getCurrentFrame().renderFence));

    // Request next available image from swapchain.
    std::uint32_t nextSwapchainImageIndex;
    VK_CHECK(vkAcquireNextImageKHR(_logicalDevice, _swapchain, helpers::FENCE_TIMEOUT_NS, getCurrentFrame().swapchainSemaphore, nullptr, &nextSwapchainImageIndex));

    VkCommandBuffer commandBuffer = getCurrentFrame().commandBuffer;

    // Now commands have finished executing, we can safely reset the buffer and being recording into it anew.
    VK_CHECK(vkResetCommandBuffer(commandBuffer, 0));

    // Begin command buffer recording.
    VkCommandBufferBeginInfo commandBufferBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo));

    // Transition the image into something writeable before rendering.
    vkutil::transitionImage(commandBuffer, _swapchainImages[nextSwapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    // Create a clear colour from the frame number, will flash at 120fps.
    const float flash = std::abs(std::sin(_frameNumber / 120.f));
    VkClearColorValue clearColor { 0.0f, 0.0f, flash, 0.0f };

    VkImageSubresourceRange clearRange = vkinit::image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);
    vkCmdClearColorImage(commandBuffer, _swapchainImages[nextSwapchainImageIndex], VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &clearRange);

    // Transition the image back to a presentable format.
    vkutil::transitionImage(commandBuffer, _swapchainImages[nextSwapchainImageIndex], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    VK_CHECK(vkEndCommandBuffer(commandBuffer));

    // Prepare to submit commands to the queue.
    // We wait on the present semaphore, this signals us when the swapchain is ready.
    // We then signal on the render semaphore, this signals that rendering is complete.
    VkCommandBufferSubmitInfo bufferSubmitInfo = vkinit::command_buffer_submit_info(commandBuffer);

    VkSemaphoreSubmitInfo waitInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, getCurrentFrame().swapchainSemaphore);
    VkSemaphoreSubmitInfo signalInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT_KHR, getSwapchainImageData(nextSwapchainImageIndex).renderSemaphore);

    const VkSubmitInfo2 submitInfo = vkinit::submit_info(&bufferSubmitInfo, &signalInfo, &waitInfo);

    // Submit command buffer to the queue and execute it.
    // _renderFrence will now block until the graphic commands finish executing.
    VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submitInfo, getCurrentFrame().renderFence));

    // Prepare to present.
    // Send the rendered image to the window, wait on the render semaphore since we need drawing to finish before we can present.
    VkPresentInfoKHR presentInfo {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pNext = nullptr;
    presentInfo.pSwapchains = &_swapchain;
    presentInfo.swapchainCount = 1;
    presentInfo.pWaitSemaphores = &getSwapchainImageData(nextSwapchainImageIndex).renderSemaphore;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pImageIndices = &nextSwapchainImageIndex;
    VK_CHECK(vkQueuePresentKHR(_graphicsQueue, &presentInfo));

    ++_frameNumber;
}

void VulkanEngine::run() {
    SDL_Event event;
    bool bQuit = false;

    // main loop
    while (!bQuit) {
        // Handle events on queue
        while (SDL_PollEvent(&event) != 0) {
            switch (event.type) {
                case SDL_QUIT: {
                    bQuit = true;
                    break;
                }
                case SDL_WINDOWEVENT_MINIMIZED: {
                    _stopRendering = true;
                    break;
                }
                case SDL_WINDOWEVENT_RESTORED: {
                    _stopRendering = false;
                    break;
                }
            }
        }

        // do not draw if we are minimized
        if (_stopRendering) {
            // throttle the speed to avoid the endless spinning
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        draw();
    }
}

void VulkanEngine::initVulkan() {
    // instance
    vkb::Instance vkbInstance;
    {
        vkb::InstanceBuilder builder;

        const vkb::Result<vkb::Instance> result = builder.set_app_name("Minerva")
                                                         .request_validation_layers(true)
                                                         .use_default_debug_messenger()
                                                         .require_api_version(1, 3, 0)
                                                         .build();

        vkbInstance = result.value();
        _instance = vkbInstance.instance;
        _debugMessenger = vkbInstance.debug_messenger;
    }
    // device
    vkb::Device vkbDevice;
    {
        assert(SDL_Vulkan_CreateSurface(_window, _instance, &_surface));

        VkPhysicalDeviceVulkan13Features features13 {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        features13.dynamicRendering = true;
        features13.synchronization2 = true;

        VkPhysicalDeviceVulkan12Features features12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
        features12.bufferDeviceAddress = true;
        features12.descriptorIndexing = true;

        vkb::PhysicalDeviceSelector selector {vkbInstance};
        vkb::PhysicalDevice physicalDevice = selector.set_minimum_version(1, 3)
                                                     .set_required_features_13(features13)
                                                     .set_required_features_12(features12)
                                                     .set_surface(_surface)
                                                     .select()
                                                     .value();

        vkbDevice = vkb::DeviceBuilder{physicalDevice}.build().value();
        _physicalDevice = physicalDevice.physical_device;
        _logicalDevice = vkbDevice.device;
    }
    // swapchain
    {
        // ..?
    }
    // queues
    {
        _graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
        _graphicsQueueFamilyIndex = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();
    }
}

void VulkanEngine::initSwapchain() {
    createSwapchain(_windowExtent.width, _windowExtent.height);
}

void VulkanEngine::createSwapchain(std::uint32_t width, std::uint32_t height) {
    vkb::SwapchainBuilder builder {_physicalDevice, _logicalDevice, _surface};

    _swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

    vkb::Swapchain swapchain = builder.set_desired_format(VkSurfaceFormatKHR {.format = _swapchainImageFormat, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
                                      // use vsync present mode
                                      .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
                                      .set_desired_extent(width, height)
                                      .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
                                      .build()
                                      .value();

    _swapchainExtent = swapchain.extent;
    _swapchain = swapchain.swapchain;
    _swapchainImages = swapchain.get_images().value();
    _swapchainImageViews = swapchain.get_image_views().value();

    _frames.resize(2);
    _swapchainImageData.resize(_swapchainImages.size());
}

void VulkanEngine::destroySwapchain() {
    vkDestroySwapchainKHR(_logicalDevice, _swapchain, nullptr);
    for (auto i = 0; i < _swapchainImageViews.size(); ++i) {
        vkDestroyImageView(_logicalDevice, _swapchainImageViews[i], nullptr);
    }
}

void VulkanEngine::initCommands() {
    // Create a command pool for the graphics queue with ability to reset command buffers.
    const VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(_graphicsQueueFamilyIndex, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

    for (auto& frame : _frames) {
        VK_CHECK(vkCreateCommandPool(_logicalDevice, &commandPoolInfo, nullptr, &frame.commandPool));

        // Allocate the default command-buffer used for rendering.
        const VkCommandBufferAllocateInfo commandBufferAllocInfo = vkinit::command_buffer_allocate_info(frame.commandPool, 1);
        VK_CHECK(vkAllocateCommandBuffers(_logicalDevice, &commandBufferAllocInfo, &frame.commandBuffer));
    }
}

void VulkanEngine::initSynchronizationStructures() {
    // Fence to ensure we are 'blocked'/forced to wait for rendering of frame N to finish.
    // Semaphores help us synchronize with the swapchain.
    // We want the fence to start in the signalled state so we can wait for the first frame to finish.
    const VkFenceCreateInfo fenceCreateInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
    const VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();

    for (auto& frame : _frames) {
        VK_CHECK(vkCreateFence(_logicalDevice, &fenceCreateInfo, nullptr, &frame.renderFence));
        VK_CHECK(vkCreateSemaphore(_logicalDevice, &semaphoreCreateInfo, nullptr, &frame.swapchainSemaphore));
    }
    for (auto& imageData : _swapchainImageData) {
        VK_CHECK(vkCreateSemaphore(_logicalDevice, &semaphoreCreateInfo, nullptr, &imageData.renderSemaphore));
    }
}
