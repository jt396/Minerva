#pragma once

#include <vulkan/vulkan.h>

#include <vector>

namespace mnv {
    class PipelineBuilder {
    public:
                                                PipelineBuilder() { Clear(); }

        void                                    SetShaders(VkShaderModule vertexShader, VkShaderModule fragmentShader);
        void                                    SetInputTopology(VkPrimitiveTopology topology);
        void                                    SetPolygonMode(VkPolygonMode mode);
        void                                    SetCullMode(VkCullModeFlags cullMode, VkFrontFace frontFace);
        void                                    DisableMultisampling();
        void                                    EnableBlendingAdditive();
        void                                    EnableBlendingAlphaBlend();
        void                                    DisableBlending();
        void                                    SetColorAttachmentFormat(VkFormat format);
        void                                    SetDepthFormat(VkFormat format);
        void                                    EnableDepthTest(bool depthWriteEnable, VkCompareOp op);
        void                                    DisableDepthTest();

        void                                    Clear();
        VkPipeline                              BuildPipeline(VkDevice device);

        VkPipelineInputAssemblyStateCreateInfo  _inputAssembly;
        VkPipelineRasterizationStateCreateInfo  _rasterizer;
        VkPipelineColorBlendAttachmentState     _colorBlendAttachment;
        VkPipelineMultisampleStateCreateInfo    _multisampling;
        VkPipelineLayout                        _pipelineLayout;
        VkPipelineDepthStencilStateCreateInfo   _depthStencil;
        VkPipelineRenderingCreateInfo           _renderInfo;
        VkFormat                                _colorAttachmentformat;

        std::vector<VkPipelineShaderStageCreateInfo>    _shaderStages;
    };
}
