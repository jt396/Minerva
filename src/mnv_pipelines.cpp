#include "mnv_Pipelines.hpp"

#include "vk_initializers.h"

#include <fmt/core.h>

void mnv::PipelineBuilder::SetShaders(VkShaderModule vertexShader, VkShaderModule fragmentShader) {
    _shaderStages.clear();

    _shaderStages.emplace_back(vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, vertexShader));
    _shaderStages.emplace_back(vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, fragmentShader));
}

void mnv::PipelineBuilder::SetInputTopology(VkPrimitiveTopology topology) {
    _inputAssembly.topology = topology;
    _inputAssembly.primitiveRestartEnable = VK_FALSE;
}

void mnv::PipelineBuilder::SetPolygonMode(VkPolygonMode mode) {
    _rasterizer.polygonMode = mode;
    _rasterizer.lineWidth = 1.f;
}

void mnv::PipelineBuilder::SetCullMode(VkCullModeFlags cullMode, VkFrontFace frontFace) {
    _rasterizer.cullMode = cullMode;
    _rasterizer.frontFace = frontFace;
}

void mnv::PipelineBuilder::SetColorAttachmentFormat(VkFormat format) {
    _colorAttachmentformat = format;
    // Connect the format to the RenderInfo struct
    _renderInfo.colorAttachmentCount = 1;
    _renderInfo.pColorAttachmentFormats = &_colorAttachmentformat;
}

void mnv::PipelineBuilder::SetDepthFormat(VkFormat format) {
    _renderInfo.depthAttachmentFormat = format;
}

void mnv::PipelineBuilder::EnableDepthTest(bool depthWriteEnable, VkCompareOp op) {
    _depthStencil.depthTestEnable       = VK_TRUE;
    _depthStencil.depthWriteEnable      = depthWriteEnable;
    _depthStencil.depthCompareOp        = op;
    _depthStencil.depthBoundsTestEnable = VK_FALSE;
    _depthStencil.stencilTestEnable     = VK_FALSE;
    _depthStencil.front                 = {};
    _depthStencil.back                  = {};
    _depthStencil.minDepthBounds        = 0.f;
    _depthStencil.maxDepthBounds        = 1.f;
}

void mnv::PipelineBuilder::DisableDepthTest() {
    _depthStencil.depthTestEnable = VK_FALSE;
    _depthStencil.depthWriteEnable = VK_FALSE;
    _depthStencil.depthCompareOp = VK_COMPARE_OP_NEVER;
    _depthStencil.depthBoundsTestEnable = VK_FALSE;
    _depthStencil.stencilTestEnable = VK_FALSE;
    _depthStencil.front = {};
    _depthStencil.back = {};
    _depthStencil.minDepthBounds = 0.f;
    _depthStencil.maxDepthBounds = 1.f;
}

void mnv::PipelineBuilder::DisableMultisampling() {
    _multisampling.sampleShadingEnable = VK_FALSE;
    // Default to no multisapling - 1 sample per pixel
    _multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    _multisampling.minSampleShading = 1.0f;
    _multisampling.pSampleMask = nullptr;
    // No alpha coverage either
    _multisampling.alphaToCoverageEnable = VK_FALSE;
    _multisampling.alphaToOneEnable = VK_FALSE;
}

void mnv::PipelineBuilder::DisableBlending() {
    _colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
    _colorBlendAttachment.blendEnable = VK_FALSE;
}

void mnv::PipelineBuilder::Clear() {
    _inputAssembly          = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    _rasterizer             = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    _colorBlendAttachment   = {};
    _multisampling          = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    _pipelineLayout         = {};
    _depthStencil           = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    _renderInfo             = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };

    _shaderStages.clear();
}

VkPipeline mnv::PipelineBuilder::BuildPipeline(VkDevice device) {
    // Not supporting multiple viewports or scissors right now;
    // we also don't have to fill anything else in here as we're using dynamic state
    VkPipelineViewportStateCreateInfo viewportState = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewportState.pNext         = nullptr;
    viewportState.viewportCount = 1;
    viewportState.scissorCount  = 1;

    // No transparency or blending, just write to the color attchment
    VkPipelineColorBlendStateCreateInfo colorBlending = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    colorBlending.pNext             = nullptr;
    colorBlending.logicOpEnable     = VK_FALSE;
    colorBlending.logicOp           = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount   = 1;
    colorBlending.pAttachments      = &_colorBlendAttachment;

    // No need for this either right now, so just clear it
    VkPipelineVertexInputStateCreateInfo _vertexInputInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

    // Create the actual pipeline, utilizes the above *Info structs
    VkGraphicsPipelineCreateInfo pipelineInfo = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };    
    pipelineInfo.pNext                  = &_renderInfo;
    pipelineInfo.stageCount             = static_cast<uint32_t>(_shaderStages.size());
    pipelineInfo.pStages                = _shaderStages.data();
    pipelineInfo.pVertexInputState      = &_vertexInputInfo;
    pipelineInfo.pInputAssemblyState    = &_inputAssembly;
    pipelineInfo.pViewportState         = &viewportState;
    pipelineInfo.pRasterizationState    = &_rasterizer;
    pipelineInfo.pMultisampleState      = &_multisampling;
    pipelineInfo.pColorBlendState       = &colorBlending;
    pipelineInfo.pDepthStencilState     = &_depthStencil;
    pipelineInfo.layout                 = _pipelineLayout;

    // Setup dynamic state
    const VkDynamicState dynamicState[] {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo { .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamicStateCreateInfo.pDynamicStates       = dynamicState;
    dynamicStateCreateInfo.dynamicStateCount    = sizeof(dynamicState) / sizeof(dynamicState[0]);

    pipelineInfo.pDynamicState = &dynamicStateCreateInfo;

    VkPipeline newPipeline;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline) != VK_SUCCESS) {
        fmt::println("PipelineBuilder::BuildPipeline() - Failed!");
        return VK_NULL_HANDLE;
    }

    return newPipeline;
}
