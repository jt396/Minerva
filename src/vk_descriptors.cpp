#include <vk_descriptors.h>

void mnv::DescriptorLayoutBuilder::addBinding(std::uint32_t binding, VkDescriptorType type) {
    VkDescriptorSetLayoutBinding newBinding {};
        newBinding.binding          = binding;
        newBinding.descriptorCount  = 1;
        newBinding.descriptorType   = type;
    bindings.emplace_back(newBinding);
}

void mnv::DescriptorLayoutBuilder::clear() {
    bindings.clear();
}

// MAYDO: Per binding/shader stage flags...
VkDescriptorSetLayout mnv::DescriptorLayoutBuilder::build(VkDevice device, VkShaderStageFlags shaderStages, void* pNext, VkDescriptorSetLayoutCreateFlags flags) {
    for (auto& binding : bindings) {
        binding.stageFlags |= shaderStages;
    }

    VkDescriptorSetLayoutCreateInfo info { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    info.pNext          = pNext;
    info.pBindings      = bindings.data();
    info.bindingCount   = static_cast<std::uint32_t>(bindings.size());
    info.flags          = flags;

    VkDescriptorSetLayout layout {};
    VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &layout));

    return layout;
}



void mnv::DescriptorAllocator::initPool(VkDevice device, std::uint32_t maxSets, std::span<PoolSizeRatio> poolRatios) {
    std::vector<VkDescriptorPoolSize> poolSizes;

    for (PoolSizeRatio ratio : poolRatios) {
        poolSizes.emplace_back(VkDescriptorPoolSize {
            .type = ratio.type,
            .descriptorCount = uint32_t(ratio.ratio * maxSets)
        });
    }

    VkDescriptorPoolCreateInfo pool_info = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    pool_info.flags         = 0;
    pool_info.maxSets       = maxSets;
    pool_info.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
    pool_info.pPoolSizes    = poolSizes.data();

    vkCreateDescriptorPool(device, &pool_info, nullptr, &pool);
}

void mnv::DescriptorAllocator::clearDescriptors(VkDevice device) {
    vkResetDescriptorPool(device, pool, 0);
}

void mnv::DescriptorAllocator::destroyPool(VkDevice device) {
    vkDestroyDescriptorPool(device, pool, nullptr);
}

VkDescriptorSet mnv::DescriptorAllocator::allocate(VkDevice device, VkDescriptorSetLayout layout) {
    VkDescriptorSetAllocateInfo allocInfo = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    allocInfo.pNext                 = nullptr;
    allocInfo.descriptorPool        = pool;
    allocInfo.descriptorSetCount    = 1;
    allocInfo.pSetLayouts           = &layout;

    VkDescriptorSet descriptorSet {};
    VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));

    return descriptorSet;
}