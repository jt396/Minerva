// Includes
#include "vk_engine.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include "vk_initializers.h"
#include "vk_types.h"
#include "vk_images.h"
#include "vk_pipelines.h"
#include "mnv_Pipelines.hpp"

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#include <VkBootstrap.h>

#include <chrono>
#include <thread>

// Aliases
using mnv::VulkanEngine;

// Forward declarations
VulkanEngine* loadedEngine = nullptr;

namespace helpers {
    constexpr std::uint32_t FRAME_OVERLAP = 2;
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
    initDescriptors();
    initPipelines();
    initImgui();

    // everything went fine
    _isInitialized = true;
}

void VulkanEngine::cleanup() {
    if (_isInitialized) {
        vkDeviceWaitIdle(_logicalDevice);

        for (auto i = 0; i < helpers::FRAME_OVERLAP; ++i) {
            vkDestroyCommandPool(_logicalDevice, _frames[i].commandPool, nullptr);

            vkDestroyFence(_logicalDevice, _frames[i].renderFence, nullptr);
            vkDestroySemaphore(_logicalDevice, _frames[i].swapchainSemaphore, nullptr);
            vkDestroySemaphore(_logicalDevice, _swapchainImageData[i].renderSemaphore, nullptr);

            _frames[i].deletionQueue.flush();
        }

        _mainDeletionQueue.flush();

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

    getCurrentFrame().deletionQueue.flush();

    // Request next available image from swapchain.
    std::uint32_t nextSwapchainImageIndex;
    VK_CHECK(vkAcquireNextImageKHR(_logicalDevice, _swapchain, helpers::FENCE_TIMEOUT_NS, getCurrentFrame().swapchainSemaphore, nullptr, &nextSwapchainImageIndex));

    VkCommandBuffer commandBuffer = getCurrentFrame().commandBuffer;

    // Now commands have finished executing, we can safely reset the buffer and being recording into it anew.
    VK_CHECK(vkResetCommandBuffer(commandBuffer, 0));

    _drawExtent.width = _drawImage.imageExtent.width;
    _drawExtent.height = _drawImage.imageExtent.height;

    // Begin command buffer recording.
    VkCommandBufferBeginInfo commandBufferBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo));

    // transition our main draw image into general layout so we can write into it
    // we will overwrite it all so we dont care about what was the older layout
    vkutil::transitionImage(commandBuffer, _drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    drawBackground(commandBuffer);

    vkutil::transitionImage(commandBuffer, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    drawGeometry(commandBuffer);

    // transition the draw image and the swapchain image into their correct transfer layouts
    vkutil::transitionImage(commandBuffer, _drawImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    vkutil::transitionImage(commandBuffer, _swapchainImages[nextSwapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // execute a copy from the draw image into the swapchain
    vkutil::copyImageToImage(commandBuffer, _drawImage.image, _swapchainImages[nextSwapchainImageIndex], _drawExtent, _swapchainExtent);

    vkutil::transitionImage(commandBuffer, _swapchainImages[nextSwapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    drawImgui(commandBuffer, _swapchainImageViews[nextSwapchainImageIndex]);

    vkutil::transitionImage(commandBuffer, _swapchainImages[nextSwapchainImageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    //finalize the command buffer (we can no longer add commands, but it can now be executed)
    VK_CHECK(vkEndCommandBuffer(commandBuffer));

    // Prepare to submit commands to the queue.
    // We wait on the present semaphore, this signals us when the swapchain is ready.
    // We then signal on the render semaphore, this signals that rendering is complete.
    VkCommandBufferSubmitInfo bufferSubmitInfo = vkinit::command_buffer_submit_info(commandBuffer);

    VkSemaphoreSubmitInfo signalInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT_KHR, getSwapchainImageData(nextSwapchainImageIndex).renderSemaphore);
    VkSemaphoreSubmitInfo waitInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, getCurrentFrame().swapchainSemaphore);

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

void VulkanEngine::drawBackground(VkCommandBuffer commandBuffer) {
    // Create a clear colour from the frame number, will flash at 120fps.
    const float flash = std::abs(std::sin(_frameNumber / 120.f));
    const VkClearColorValue clearColor { 0.0f, 0.0f, flash, 0.0f };

    ComputeEffect& effect = backgroundEffects[currentBackgroundEffect];

    // bind the gradient drawing compute pipeline
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);

    // bind the descriptor set containing the draw image for the compute pipeline
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _gradientPipelineLayout, 0, 1, &_drawImageDescriptor, 0, nullptr);

    ComputePushConstants pushConstants;
    pushConstants.data0 = glm::vec4(1.f, 0.f, 0.f, 1.f);
    pushConstants.data1 = glm::vec4(0.f, 0.f, 1.f, 1.f);
    vkCmdPushConstants(commandBuffer, _gradientPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &pushConstants);

    // execute the compute pipeline dispatch. We are using 16x16 workgroup size so we need to divide by it
    vkCmdDispatch(commandBuffer, std::ceil(_drawExtent.width / 16.0f), std::ceil(_drawExtent.height / 16.0f), 1);
}

void VulkanEngine::drawGeometry(VkCommandBuffer commandBuffer) {
    // Begin a render pass - connected to our draw image
    VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(_drawImage.view, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    VkRenderingInfo renderInfo = vkinit::rendering_info(_drawExtent, &colorAttachment, nullptr);
    vkCmdBeginRendering(commandBuffer, &renderInfo);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _trianglePipeline);

    //set dynamic viewport and scissor
    VkViewport viewport = {};
        viewport.x = 0;
        viewport.y = 0;
        viewport.width = _drawExtent.width;
        viewport.height = _drawExtent.height;
        viewport.minDepth = 0.f;
        viewport.maxDepth = 1.f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor = {};
        scissor.offset.x = 0;
        scissor.offset.y = 0;
        scissor.extent.width = _drawExtent.width;
        scissor.extent.height = _drawExtent.height;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    // Draw command 3 vertices
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);

    vkCmdEndRendering(commandBuffer);
}

void VulkanEngine::drawImgui(VkCommandBuffer commandBuffer, VkImageView targetImageView) {
    VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingInfo renderInfo = vkinit::rendering_info(_swapchainExtent, &colorAttachment, nullptr);

    vkCmdBeginRendering(commandBuffer, &renderInfo);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
    vkCmdEndRendering(commandBuffer);
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

        // imgui
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL2_NewFrame();

        ImGui::NewFrame();
        if (ImGui::Begin("background")) {
            ComputeEffect& selected = backgroundEffects[currentBackgroundEffect];

            ImGui::Text("Selected effect: ", selected.name);

            //ImGui::SliderInt("Effect Index", &currentBackgroundEffect, 0, backgroundEffects.size() - 1);

            ImGui::InputFloat4("data1", (float*)&selected.data.data0);
            ImGui::InputFloat4("data2", (float*)&selected.data.data1);
            ImGui::InputFloat4("data3", (float*)&selected.data.data2);
            ImGui::InputFloat4("data4", (float*)&selected.data.data3);

            ImGui::End();
        }
        ImGui::Render();
        // ---

        draw();
    }
}

void VulkanEngine::immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function) {
    VK_CHECK(vkResetFences(_logicalDevice, 1, &_immFence));
    VK_CHECK(vkResetCommandBuffer(_immCommandBuffer, 0));

    VkCommandBuffer commandBuffer = _immCommandBuffer;
    const VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &cmdBeginInfo));
        function(commandBuffer);
    VK_CHECK(vkEndCommandBuffer(commandBuffer));

    VkCommandBufferSubmitInfo cmdinfo = vkinit::command_buffer_submit_info(commandBuffer);
    const VkSubmitInfo2 submit = vkinit::submit_info(&cmdinfo, nullptr, nullptr);

    // Submit command buffer to the queue and execute it. _renderFence will now block until the graphic commands finish execution
    VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, _immFence));
    VK_CHECK(vkWaitForFences(_logicalDevice, 1, &_immFence, true, 9999999999));
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
    // allocator
    {
        VmaAllocatorCreateInfo allocatorDesc {};
            allocatorDesc.physicalDevice    = _physicalDevice;
            allocatorDesc.device            = _logicalDevice;
            allocatorDesc.instance          = _instance;
            allocatorDesc.flags             = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
        vmaCreateAllocator(&allocatorDesc, &_vmaAllocator);

        _mainDeletionQueue.pushFunction([&]() {
            vmaDestroyAllocator(_vmaAllocator);
        });
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

    // Draw image size to match the window
    const VkExtent3D drawImageExtent {
        .width = _windowExtent.width,
        .height = _windowExtent.height,
        .depth = 1
    };

    // Hard-code draw format to 64bit float for now..
    _drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    _drawImage.imageExtent = drawImageExtent;

    VkImageUsageFlags drawImageUsage {};
    drawImageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    drawImageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    drawImageUsage |= VK_IMAGE_USAGE_STORAGE_BIT;
    drawImageUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    const VkImageCreateInfo imageCreateInfo = vkinit::image_create_info(_drawImage.imageFormat, drawImageUsage, drawImageExtent);

    // Allocate from GPU memory
    VmaAllocationCreateInfo imageAllocationInfo = {};
    imageAllocationInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    imageAllocationInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vmaCreateImage(_vmaAllocator, &imageCreateInfo, &imageAllocationInfo, &_drawImage.image, &_drawImage.allocation, nullptr);

    const VkImageViewCreateInfo imageViewCreateInfo = vkinit::imageview_create_info(_drawImage.imageFormat, _drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);

    VK_CHECK(vkCreateImageView(_logicalDevice, &imageViewCreateInfo, nullptr, &_drawImage.view));

    _mainDeletionQueue.pushFunction([=] () {
        vkDestroyImageView(_logicalDevice, _drawImage.view, nullptr);
        vmaDestroyImage(_vmaAllocator, _drawImage.image, _drawImage.allocation);
    });
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

    // imgui
    VK_CHECK(vkCreateCommandPool(_logicalDevice, &commandPoolInfo, nullptr, &_immCommandPool));

    VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_immCommandPool, 1);
    VK_CHECK(vkAllocateCommandBuffers(_logicalDevice, &cmdAllocInfo, &_immCommandBuffer));

    _mainDeletionQueue.pushFunction([=]() {
        vkDestroyCommandPool(_logicalDevice, _immCommandPool, nullptr);
    });
    // ---
}

void VulkanEngine::initDescriptors() {
    std::vector<mnv::DescriptorAllocator::PoolSizeRatio> sizes {
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 }
    };
    globalDescriptorAllocator.initPool(_logicalDevice, 10, sizes);

    // Create a DescriptorSetLayout for Compute draw
    DescriptorLayoutBuilder builder;
    builder.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    _drawImageDescriptorLayout = builder.build(_logicalDevice, VK_SHADER_STAGE_COMPUTE_BIT);

    // Allocate a DescriptorSet for our draw image
    _drawImageDescriptor = globalDescriptorAllocator.allocate(_logicalDevice, _drawImageDescriptorLayout);

    VkDescriptorImageInfo imageInfo {};
    imageInfo.imageLayout   = VK_IMAGE_LAYOUT_GENERAL;
    imageInfo.imageView     = _drawImage.view;

    VkWriteDescriptorSet drawImageWrite {};
    drawImageWrite.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    drawImageWrite.pNext            = nullptr;
    drawImageWrite.dstBinding       = 0;
    drawImageWrite.dstSet           = _drawImageDescriptor;
    drawImageWrite.descriptorCount  = 1;
    drawImageWrite.descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    drawImageWrite.pImageInfo       = &imageInfo;

    vkUpdateDescriptorSets(_logicalDevice, 1, &drawImageWrite, 0, nullptr);

    // Ensure both descriptor allocator and new layout are cleaned up
    _mainDeletionQueue.pushFunction([&]() {
        globalDescriptorAllocator.destroyPool(_logicalDevice);
        vkDestroyDescriptorSetLayout(_logicalDevice, _drawImageDescriptorLayout, nullptr);
    });
}

void VulkanEngine::initPipelines() {
    initBackgroundPipelines();
    initTrianglePipeline();
}

void VulkanEngine::initBackgroundPipelines() {
    VkShaderModule gradientShader;
    if (!mnv::loadShaderModule("../../shaders/gradient_color.comp.spv", _logicalDevice, &gradientShader)) {
        fmt::print("Error when building the compute shader \n");
    }

    VkShaderModule nightSkyShader;
    if (!mnv::loadShaderModule("../../shaders/sky.comp.spv", _logicalDevice, &nightSkyShader)) {
        fmt::print("Error when building the compute shader \n");
    }

    VkPushConstantRange pushConstants {};
    pushConstants.offset = 0;
    pushConstants.size = sizeof(ComputePushConstants);
    pushConstants.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkPipelineLayoutCreateInfo computeLayout {};
        computeLayout.sType                     = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        computeLayout.pNext                     = nullptr;
        computeLayout.pSetLayouts               = &_drawImageDescriptorLayout;
        computeLayout.setLayoutCount            = 1;
        computeLayout.pPushConstantRanges       = &pushConstants;
        computeLayout.pushConstantRangeCount    = 1;
    VK_CHECK(vkCreatePipelineLayout(_logicalDevice, &computeLayout, nullptr, &_gradientPipelineLayout));

    VkPipelineShaderStageCreateInfo stageInfo {};
    stageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.pNext  = nullptr;
    stageInfo.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = gradientShader;
    stageInfo.pName  = "main";

    VkComputePipelineCreateInfo computePipelineCreateInfo {};
        computePipelineCreateInfo.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        computePipelineCreateInfo.pNext  = nullptr;
        computePipelineCreateInfo.layout = _gradientPipelineLayout;
        computePipelineCreateInfo.stage  = stageInfo;

        ComputeEffect gradient {};
        gradient.name = "gradient";
        gradient.layout = _gradientPipelineLayout;
        gradient.data = {};
        // Default colours
        gradient.data.data0 = glm::vec4(1.f, 0.f, 0.f, 1.f);
        gradient.data.data1 = glm::vec4(0.f, 0.f, 1.f, 1.f);
    VK_CHECK(vkCreateComputePipelines(_logicalDevice, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &gradient.pipeline));

    // Re-use majority of the information to create the sky shader
    ComputeEffect sky {};
    sky.name = "sky";
    sky.layout = _gradientPipelineLayout;
    sky.data = {};
    sky.data.data0 = glm::vec4(0.1f, 0.2f, 0.4f, 0.97f);

    VK_CHECK(vkCreateComputePipelines(_logicalDevice, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &sky.pipeline));

    backgroundEffects.emplace_back(gradient);
    backgroundEffects.emplace_back(sky);

    vkDestroyShaderModule(_logicalDevice, gradientShader, nullptr);
    vkDestroyShaderModule(_logicalDevice, nightSkyShader, nullptr);

    _mainDeletionQueue.pushFunction([&]() {
        vkDestroyPipelineLayout(_logicalDevice, _gradientPipelineLayout, nullptr);
        vkDestroyPipeline(_logicalDevice, gradient.pipeline, nullptr);
        vkDestroyPipeline(_logicalDevice, sky.pipeline, nullptr);
    });
}

void VulkanEngine::initTrianglePipeline() {
    VkShaderModule triangleVertexShader;
    if (!mnv::loadShaderModule("../../shaders/colored_triangle.vert.spv", _logicalDevice, &triangleVertexShader)) {
        fmt::print("Error when building the triangle vertex shader module");
    } else {
        fmt::print("Triangle vertex shader succesfully loaded");
    }

    VkShaderModule triangleFragmentShader;
    if (!mnv::loadShaderModule("../../shaders/colored_triangle.frag.spv", _logicalDevice, &triangleFragmentShader)) {
        fmt::print("Error when building the triangle fragment shader module");
    } else {
        fmt::print("Triangle fragment shader succesfully loaded");
    }

    // Build the pipeline layout that controls the inputs/outputs of the shader
    // We are not using descriptor sets or other systems yet, so no need to use anything other than empty default
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vkinit::pipeline_layout_create_info();
    VK_CHECK(vkCreatePipelineLayout(_logicalDevice, &pipelineLayoutCreateInfo, nullptr, &_trianglePipelineLayout));

    mnv::PipelineBuilder builder;
        builder._pipelineLayout = _trianglePipelineLayout;
        builder.SetShaders(triangleVertexShader, triangleFragmentShader);
        builder.SetInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        builder.SetPolygonMode(VK_POLYGON_MODE_FILL);
        // No backface culling
        builder.SetCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
        builder.DisableMultisampling();
        builder.DisableBlending();
        builder.DisableDepthTest();
        // Connec the image format we'll draw into, taken from the draw image
        builder.SetColorAttachmentFormat(_drawImage.imageFormat);
        builder.SetDepthFormat(VK_FORMAT_UNDEFINED);
    _trianglePipeline = builder.BuildPipeline(_logicalDevice);

    // Clean up
    vkDestroyShaderModule(_logicalDevice, triangleVertexShader, nullptr);
    vkDestroyShaderModule(_logicalDevice, triangleFragmentShader, nullptr);

    _mainDeletionQueue.pushFunction([&](){
        vkDestroyPipelineLayout(_logicalDevice, _trianglePipelineLayout, nullptr);
        vkDestroyPipeline(_logicalDevice, _trianglePipeline, nullptr);
    });
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

    // imgui
    VK_CHECK(vkCreateFence(_logicalDevice, &fenceCreateInfo, nullptr, &_immFence));
    _mainDeletionQueue.pushFunction([=]() { vkDestroyFence(_logicalDevice, _immFence, nullptr); });
    // ---
}

void VulkanEngine::initImgui() {
    // 1: Create descriptor pool for imgui; the size of the pool is very large but it's copied from imgui demo.
    const VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000;
    pool_info.poolSizeCount = static_cast<uint32_t>(std::size(pool_sizes));
    pool_info.pPoolSizes = pool_sizes;

    VkDescriptorPool imguiPool;
    VK_CHECK(vkCreateDescriptorPool(_logicalDevice, &pool_info, nullptr, &imguiPool));

    // 2: Initialize imgui library
    // Initialize the core structures of imgui
    ImGui::CreateContext();

    // Initialize imgui for SDL
    ImGui_ImplSDL2_InitForVulkan(_window);

    // Initializes imgui for Vulkan
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = _instance;
    init_info.PhysicalDevice = _physicalDevice;
    init_info.Device = _logicalDevice;
    init_info.Queue = _graphicsQueue; // MAYDO: This could be a separte queue? Would also need updating in immediateSubmit()?
    init_info.DescriptorPool = imguiPool;
    init_info.MinImageCount = 3;
    init_info.ImageCount = 3;
    init_info.UseDynamicRendering = true;

    // Dynamic rendering parameters for imgui
    init_info.PipelineRenderingCreateInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &_swapchainImageFormat;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&init_info);
    ImGui_ImplVulkan_CreateFontsTexture();

    // Destroy the imgui-related structures
    _mainDeletionQueue.pushFunction([=]() {
        ImGui_ImplVulkan_Shutdown();
        vkDestroyDescriptorPool(_logicalDevice, imguiPool, nullptr);
    });
}