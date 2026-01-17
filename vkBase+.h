#pragma once

#include "vkBase.h"

// 对graphicsPipelineCreateInfo的超级包装
// 所有创建信息结构体都是没有构造函数的聚合体，花括号初始化器列表中未提及的成员变量被零初始化
struct graphicsPipelineCreateInfoPack {
	VkGraphicsPipelineCreateInfo createInfo = 
		{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	
	//===== shader stage =======
	std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
	//Vertex Input
	VkPipelineVertexInputStateCreateInfo vertexInputStateCi =
	{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	std::vector<VkVertexInputBindingDescription> vertexInputBindings;
	std::vector<VkVertexInputAttributeDescription> vertexInputAttributes;
    //Input Assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCi =
    { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    //Tessellation
    VkPipelineTessellationStateCreateInfo tessellationStateCi =
    { VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO };
    //Viewport
    VkPipelineViewportStateCreateInfo viewportStateCi =
    { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    std::vector<VkViewport> viewports;
    std::vector<VkRect2D> scissors;
    uint32_t dynamicViewportCount = 1; //动态视口/剪裁不会用到上述的vector，因此动态视口和剪裁的个数向这俩变量手动指定
    uint32_t dynamicScissorCount = 1;
    //Rasterization
    VkPipelineRasterizationStateCreateInfo rasterizationStateCi =
    { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    //Multisample
    VkPipelineMultisampleStateCreateInfo multisampleStateCi =
    { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    //Depth & Stencil
    VkPipelineDepthStencilStateCreateInfo depthStencilStateCi =
    { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    //Color Blend
    VkPipelineColorBlendStateCreateInfo colorBlendStateCi =
    { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachmentStates;
    //Dynamic
    VkPipelineDynamicStateCreateInfo dynamicStateCi =
    { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    std::vector<VkDynamicState> dynamicStates;

    //--------------------

    graphicsPipelineCreateInfoPack() {
        SetCreateInfos();
        //若非派生管线，createInfo.basePipelineIndex不得为0，设置为-1
        createInfo.basePipelineIndex = -1;
    }

    graphicsPipelineCreateInfoPack(const graphicsPipelineCreateInfoPack& other) noexcept {
        createInfo = other.createInfo;
        SetCreateInfos();

        vertexInputStateCi = other.vertexInputStateCi;
        inputAssemblyStateCi = other.inputAssemblyStateCi;
        tessellationStateCi = other.tessellationStateCi;
        viewportStateCi = other.viewportStateCi;
        rasterizationStateCi = other.rasterizationStateCi;
        multisampleStateCi = other.multisampleStateCi;
        depthStencilStateCi = other.depthStencilStateCi;
        colorBlendStateCi = other.colorBlendStateCi;
        dynamicStateCi = other.dynamicStateCi;

        shaderStages = other.shaderStages;
        vertexInputBindings = other.vertexInputBindings;
        vertexInputAttributes = other.vertexInputAttributes;
        viewports = other.viewports;
        scissors = other.scissors;
        colorBlendAttachmentStates = other.colorBlendAttachmentStates;
        dynamicStates = other.dynamicStates;
        UpdateAllArrayAddresses();
    }
    //Getter，这里我没用const修饰符
    operator VkGraphicsPipelineCreateInfo& () { return createInfo; }
    //Non-const Function
    //该函数用于将各个vector中数据的地址赋值给各个创建信息中相应成员，并相应改变各个count
    void UpdateAllArrays() {
        createInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
        vertexInputStateCi.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInputBindings.size());
        vertexInputStateCi.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
        viewportStateCi.viewportCount = viewports.size() ? static_cast<uint32_t>(viewports.size()) : dynamicViewportCount;
        viewportStateCi.scissorCount = scissors.size() ? static_cast<uint32_t>(scissors.size()) : dynamicScissorCount;
        colorBlendStateCi.attachmentCount = static_cast<uint32_t>(colorBlendAttachmentStates.size());
        dynamicStateCi.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        UpdateAllArrayAddresses();
    }
private:
    //该函数用于将创建信息的地址赋值给basePipelineIndex中相应成员
    void SetCreateInfos() {
        createInfo.pVertexInputState = &vertexInputStateCi;
        createInfo.pInputAssemblyState = &inputAssemblyStateCi;
        createInfo.pTessellationState = &tessellationStateCi;
        createInfo.pViewportState = &viewportStateCi;
        createInfo.pRasterizationState = &rasterizationStateCi;
        createInfo.pMultisampleState = &multisampleStateCi;
        createInfo.pDepthStencilState = &depthStencilStateCi;
        createInfo.pColorBlendState = &colorBlendStateCi;
        createInfo.pDynamicState = &dynamicStateCi;
    }

    //该函数用于将各个vector中数据的地址赋值给各个创建信息中相应成员，但不改变各个count
    void UpdateAllArrayAddresses() {
        createInfo.pStages = shaderStages.data();
        vertexInputStateCi.pVertexBindingDescriptions = vertexInputBindings.data();
        vertexInputStateCi.pVertexAttributeDescriptions = vertexInputAttributes.data();
        viewportStateCi.pViewports = viewports.data();
        viewportStateCi.pScissors = scissors.data();
        colorBlendStateCi.pAttachments = colorBlendAttachmentStates.data();
        dynamicStateCi.pDynamicStates = dynamicStates.data();
    }
};