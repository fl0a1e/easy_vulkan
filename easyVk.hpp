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

    // 创建一个直接画到交换链图像的最简 render pass：
    // 只有颜色附件，没有深度模板，也没有多子通道。
    const auto& CreateRpwf_Screen() {
        static renderPassWithFramebuffers rpwf;

        // ===== 创建render pass =========
        // 这里描述的附件就是交换链图像本身。
        // loadOp = CLEAR 表示每帧开始先清屏，
        // finalLayout = PRESENT_SRC_KHR 表示渲染结束后要用于显示。
        VkAttachmentDescription attachmentDescription = {
            .format = graphicsBase::Base().SwapchainCreateInfo().imageFormat, // 这里描述的是交换链图像
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR // 将交换链图像用于呈现
        };

        // 这个 render pass 只有一个 subpass，而且只写一个颜色附件
        VkAttachmentReference attachmentReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkSubpassDescription subpassDescription = {
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments = &attachmentReference
        };

        // 显式指定子通道依赖，让颜色输出阶段的同步关系更清晰
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

        // framebuffer 依赖交换链尺寸和 image view。
        // 所以交换链一旦重建，framebuffer 也必须跟着销毁并重建。
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

        // 这个函数可能被多次访问，但回调只应该注册一次。
        ExecuteOnce(rpwf);
        graphicsBase::Base().AddCallback_CreateSwapchain(CreateFramebuffers);
        graphicsBase::Base().AddCallback_DestroySwapchain(DestroyFramebuffers);

        return rpwf;
    }

    
}

