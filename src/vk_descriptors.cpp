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


// ===========================================================
// DescriptorAllocator
// ===========================================================
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

// ===========================================================
// DescriptorAllocatorGrowable
// ===========================================================
void mnv::DescriptorAllocatorGrowable::init(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios) {
    _ratios.clear();

    for (auto ratio : poolRatios) {
        _ratios.push_back(ratio);
    }

    VkDescriptorPool newPool = createPool(device, maxSets, poolRatios);
    _setsPerPool = maxSets * 1.5;
    _readyPools.push_back(newPool);
}

void mnv::DescriptorAllocatorGrowable::clearPools(VkDevice device) {
    for (auto pool : _readyPools) {
        vkResetDescriptorPool(device, pool, 0);
    }
    for (auto pool : _fullPools) {
        vkResetDescriptorPool(device, pool, 0);
        _readyPools.push_back(pool);
    }
    _fullPools.clear();
}

void mnv::DescriptorAllocatorGrowable::destroyPools(VkDevice device) {
    for (auto pool : _readyPools) {
        vkDestroyDescriptorPool(device, pool, nullptr);
    }
    _readyPools.clear();

    for (auto pool : _fullPools) {
        vkDestroyDescriptorPool(device, pool, nullptr);
    }
    _fullPools.clear();
}

VkDescriptorSet mnv::DescriptorAllocatorGrowable::allocate(VkDevice device, VkDescriptorSetLayout layout, void* next) {
    VkDescriptorPool poolToUse = getPool(device);

    VkDescriptorSetAllocateInfo allocInfo {};
    allocInfo.sType                 = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.pNext                 = next;
    allocInfo.descriptorPool        = poolToUse;
    allocInfo.descriptorSetCount    = 1;
    allocInfo.pSetLayouts           = &layout;

    VkDescriptorSet descriptorSet;

    if (const VkResult result = vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet);
        result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
        _fullPools.push_back(poolToUse);

        poolToUse = getPool(device);
        allocInfo.descriptorPool = poolToUse;

        VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));
    }

    _readyPools.push_back(poolToUse);
    return descriptorSet;
}

VkDescriptorPool mnv::DescriptorAllocatorGrowable::getPool(VkDevice device) {
    VkDescriptorPool newPool;

    if (!_readyPools.empty()) {
        newPool = _readyPools.back();
        _readyPools.pop_back();
    } else {
        newPool = createPool(device, _setsPerPool, _ratios);
        // MAYDO: Make this configurable?
        _setsPerPool = std::min<std::uint32_t>((_setsPerPool *= 1.5), 4092);
    }

    return newPool;
}

VkDescriptorPool mnv::DescriptorAllocatorGrowable::createPool(VkDevice device, uint32_t setCount, std::span<PoolSizeRatio> poolRatios) {
    std::vector<VkDescriptorPoolSize> poolSizes;

    for (PoolSizeRatio ratio : poolRatios) {
        poolSizes.push_back(VkDescriptorPoolSize {
            .type = ratio.type,
            .descriptorCount = uint32_t(ratio.ratio * setCount)
        });
    }

    VkDescriptorPoolCreateInfo poolInfo {};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags         = 0;
    poolInfo.maxSets       = setCount;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes    = poolSizes.data();

    VkDescriptorPool newPool;
    vkCreateDescriptorPool(device, &poolInfo, nullptr, &newPool);

    return newPool;
}

// ===========================================================
// DescriptorWriter
// ===========================================================
void mnv::DescriptorWriter::writeImage(int binding, VkImageView image, VkSampler sampler, VkImageLayout layout, VkDescriptorType type) {
    VkDescriptorImageInfo& info = imageInfos.emplace_back(VkDescriptorImageInfo {
        .sampler = sampler,
        .imageView = image,
        .imageLayout = layout
    });

    VkWriteDescriptorSet write { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstBinding        = binding;
    write.dstSet            = VK_NULL_HANDLE; // Empty for now until we need to write it
    write.descriptorCount   = 1;
    write.descriptorType    = type;
    write.pImageInfo        = &info;

    writes.push_back(write);
}

void mnv::DescriptorWriter::writeBuffer(int binding, VkBuffer buffer, size_t size, size_t offset, VkDescriptorType type) {
    VkDescriptorBufferInfo& info = bufferInfos.emplace_back(VkDescriptorBufferInfo {
        .buffer = buffer,
        .offset = offset,
        .range = size
    });

    VkWriteDescriptorSet write { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstBinding        = binding;
    write.dstSet            = VK_NULL_HANDLE; // Empty for now until we need to write it
    write.descriptorCount   = 1;
    write.descriptorType    = type;
    write.pBufferInfo       = &info;

    writes.push_back(write);
}

void mnv::DescriptorWriter::clear() {
    imageInfos.clear();
    bufferInfos.clear();
    writes.clear();
}

void mnv::DescriptorWriter::updateSet(VkDevice device, VkDescriptorSet set) {
    for (VkWriteDescriptorSet& write : writes) {
        write.dstSet = set;
    }
    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}
