#pragma once 

#include <vulkan/vulkan.h>

namespace vkutil {
    void transitionImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout);
    void copyImageToImage(VkCommandBuffer commandBuffeer, VkImage source, VkImage destination, VkExtent2D srcSize, VkExtent2D dstSize);
};
