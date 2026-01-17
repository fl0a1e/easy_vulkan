#pragma once

#include "VKBase+.h"

// Render Pipeline and FrameBuffer

using namespace vulkan;
const VkExtent2D& windowSize = graphicsBase::Base().SwapchainCreateInfo().imageExtent;


namespace easyVulkan {
    using namespace vulkan;
    struct renderPassWithFramebuffers {
        renderPass renderPass;
        std::vector<framebuffer> framebuffers;
    };

    // 创建一个直接渲染到交换链图像，且不做深度测试等任何测试的render pass
    const auto& CreateRpwf_Screen() {
        static renderPassWithFramebuffers rpwf;

        // ===== 创建render pass =========
        // 描述图像附件
        VkAttachmentDescription attachmentDescription = {
            .format = graphicsBase::Base().SwapchainCreateInfo().imageFormat, // 这里描述的是交换链图像
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR // 将交换链图像用于呈现
        };

        // 只有一个子通道，该子通道只使用一个颜色附件
        VkAttachmentReference attachmentReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkSubpassDescription subpassDescription = {
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments = &attachmentReference
        };

        // 书写子通道依赖，覆盖渲染通道开始时的隐式依赖
        VkSubpassDependency subpassDependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, //不早于提交命令缓冲区时等待semaphore对应的waitDstStageMask
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT
        };

        VkRenderPassCreateInfo renderPassCreateInfo = {
        .attachmentCount = 1,
        .pAttachments = &attachmentDescription,
        .subpassCount = 1,
        .pSubpasses = &subpassDescription,
        .dependencyCount = 1,
        .pDependencies = &subpassDependency
        };
        rpwf.renderPass.Create(renderPassCreateInfo);

        // ===== 创建framebuffers =========
        //为每张交换链图像创建帧缓冲
        rpwf.framebuffers.resize(graphicsBase::Base().SwapchainImageCount()); //rpwf.framebuffers的元素数量对齐交换链图像

        VkFramebufferCreateInfo framebufferCreateInfo = {
        .renderPass = rpwf.renderPass,
        .attachmentCount = 1,
        .width = windowSize.width,
        .height = windowSize.height,
        .layers = 1
        };
        for (size_t i = 0; i < graphicsBase::Base().SwapchainImageCount(); i++) {
            VkImageView attachment = graphicsBase::Base().SwapchainImageView(i);
            framebufferCreateInfo.pAttachments = &attachment;
            rpwf.framebuffers[i].Create(framebufferCreateInfo);
        }

        //由于帧缓冲的大小与交换链图像相关，重建交换链时也会需要重建帧缓冲，于是将创建和销毁帧缓冲的代码扔进各自的lambda表达式，以用作回调函数
        auto CreateFramebuffers = [] {
            rpwf.framebuffers.resize(graphicsBase::Base().SwapchainImageCount());
            VkFramebufferCreateInfo framebufferCreateInfo = {
                .renderPass = rpwf.renderPass,
                .attachmentCount = 1,
                .width = windowSize.width,
                .height = windowSize.height,
                .layers = 1
            };
            for (size_t i = 0; i < graphicsBase::Base().SwapchainImageCount(); i++) {
                VkImageView attachment = graphicsBase::Base().SwapchainImageView(i);
                framebufferCreateInfo.pAttachments = &attachment;
                rpwf.framebuffers[i].Create(framebufferCreateInfo);
            }
        };
        auto DestroyFramebuffers = [] {
            rpwf.framebuffers.clear(); //清空vector中的元素时会逐一执行析构函数
            };
        CreateFramebuffers();

        ExecuteOnce(rpwf); //防止再次调用本函数时，重复添加回调函数
        graphicsBase::Base().AddCallback_CreateSwapchain(CreateFramebuffers);
        graphicsBase::Base().AddCallback_DestroySwapchain(DestroyFramebuffers);

        return rpwf;
    }

    
}

