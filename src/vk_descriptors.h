#pragma once

#include <vk_types.h>

#include <vector>

namespace mnv {
    struct DescriptorLayoutBuilder {
        std::vector<VkDescriptorSetLayoutBinding>   bindings;

        void                                        addBinding(std::uint32_t binding, VkDescriptorType type);
        void                                        clear();
        VkDescriptorSetLayout                       build(VkDevice device, VkShaderStageFlags shaderStages, void* pNext = nullptr, VkDescriptorSetLayoutCreateFlags flags = 0);

        // TODO: Add a destroy function? Bit stupid to use unwrapped vkDestory* call?
    };

    struct DescriptorAllocator {
        struct PoolSizeRatio {
            VkDescriptorType    type;
            float               ratio;
        };

        VkDescriptorPool pool;

        void            initPool(VkDevice device, std::uint32_t maxSets, std::span<PoolSizeRatio> poolRatios);
        void            clearDescriptors(VkDevice device);
        void            destroyPool(VkDevice device);

        VkDescriptorSet allocate(VkDevice device, VkDescriptorSetLayout layout);
    };

    class DescriptorAllocatorGrowable {
    public:
        struct PoolSizeRatio {
            VkDescriptorType    type;
            float               ratio;
        };

        void                            init(VkDevice device, std::uint32_t maxSets, std::span<PoolSizeRatio> poolRatios);
        void                            clearPools(VkDevice device);
        void                            destroyPools(VkDevice device);

        VkDescriptorSet                 allocate(VkDevice device, VkDescriptorSetLayout layout, void* next = nullptr);

    private:
        VkDescriptorPool                getPool(VkDevice device);
        VkDescriptorPool                createPool(VkDevice device, std::uint32_t setCount, std::span<PoolSizeRatio> poolRatios);

        std::vector<PoolSizeRatio>      _ratios;
        std::vector<VkDescriptorPool>   _fullPools;
        std::vector<VkDescriptorPool>   _readyPools;
        std::uint32_t                   _setsPerPool;
    };

    struct DescriptorWriter {
        // std::deque will preserve the pointers
        std::deque<VkDescriptorImageInfo>   imageInfos;
        std::deque<VkDescriptorBufferInfo>  bufferInfos;
        std::vector<VkWriteDescriptorSet>   writes;

        void                                writeImage(int binding, VkImageView image, VkSampler sampler, VkImageLayout layout, VkDescriptorType type);
        void                                writeBuffer(int binding, VkBuffer buffer, size_t size, size_t offset, VkDescriptorType type);

        void                                clear();
        void                                updateSet(VkDevice device, VkDescriptorSet set);
    };
}
