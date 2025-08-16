#pragma once 

#include <vulkan/vulkan.h>

namespace vkutil {
    void transitionImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout);
};
