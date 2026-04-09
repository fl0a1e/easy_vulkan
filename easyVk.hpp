#pragma once

#include "VKBase+.h"

// Render Pipeline and FrameBuffer

using namespace vulkan;
const VkExtent2D& windowSize = graphicsBase::Base().SwapchainCreateInfo().imageExtent;


namespace easyVulkan {
    using namespace vulkan;

    inline uint32_t FindMemoryTypeIndex(uint32_t memoryTypeBits, VkMemoryPropertyFlags requiredProperties) {
        const auto& memoryProperties = graphicsBase::Base().PhysicalDeviceMemoryProperties();
        for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
            const bool typeMatches = memoryTypeBits & (1u << i);
            const bool propertyMatches =
                (memoryProperties.memoryTypes[i].propertyFlags & requiredProperties) == requiredProperties;
            if (typeMatches && propertyMatches)
                return i;
        }

        outStream << std::format(
            "[ easyVulkan ] ERROR\nFailed to find a memory type with flags: {}\n",
            static_cast<uint32_t>(requiredProperties));
        abort();
    }

    inline VkFormat FindSupportedFormat(
        arrayRef<const VkFormat> candidates,
        VkImageTiling tiling,
        VkFormatFeatureFlags features) {
        for (auto format : candidates) {
            VkFormatProperties properties{};
            vkGetPhysicalDeviceFormatProperties(graphicsBase::Base().PhysicalDevice(), format, &properties);

            if (tiling == VK_IMAGE_TILING_LINEAR && (properties.linearTilingFeatures & features) == features)
                return format;
            if (tiling == VK_IMAGE_TILING_OPTIMAL && (properties.optimalTilingFeatures & features) == features)
                return format;
        }

        outStream << "[ easyVulkan ] ERROR\nFailed to find a supported image format!\n";
        abort();
    }

    inline VkFormat FindDepthFormat() {
        static constexpr VkFormat candidates[] = {
            VK_FORMAT_D32_SFLOAT,
            VK_FORMAT_D32_SFLOAT_S8_UINT,
            VK_FORMAT_D24_UNORM_S8_UINT
        };
        return FindSupportedFormat(candidates, VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    }

    inline VkFormat FindShadowDepthFormat() {
        static constexpr VkFormat candidates[] = {
            VK_FORMAT_D32_SFLOAT,
            VK_FORMAT_D32_SFLOAT_S8_UINT,
            VK_FORMAT_D24_UNORM_S8_UINT
        };
        return FindSupportedFormat(
            candidates,
            VK_IMAGE_TILING_OPTIMAL,
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
    }

    struct renderPassWithFramebuffers {
        renderPass renderPass;
        std::vector<framebuffer> framebuffers;
        VkFormat depthFormat = VK_FORMAT_UNDEFINED;
        VkImage depthImage = VK_NULL_HANDLE;
        VkDeviceMemory depthMemory = VK_NULL_HANDLE;
        VkImageView depthImageView = VK_NULL_HANDLE;
    };

    struct shadowRenderPassResources {
        renderPass shadowPass;
        framebuffer shadowFramebuffer;
        VkFormat depthFormat = VK_FORMAT_UNDEFINED;
        VkExtent2D extent = {};
        VkImage depthImage = VK_NULL_HANDLE;
        VkDeviceMemory depthMemory = VK_NULL_HANDLE;
        VkImageView depthImageView = VK_NULL_HANDLE;
        VkSampler shadowSampler = VK_NULL_HANDLE;
    };

    // 创建一个直接画到交换链图像的最简 render pass：
    // 颜色附件写到 swapchain，深度附件单独存到一张 depth image。
    const auto& CreateRpwf_Screen() {
        static renderPassWithFramebuffers rpwf;

        rpwf.depthFormat = FindDepthFormat();

        // ===== 创建 render pass =========
        // 附件 0：交换链颜色图像。
        // loadOp = CLEAR 表示每帧开始先清屏
        // finalLayout = PRESENT_SRC_KHR 表示渲染结束后要用于显示
        VkAttachmentDescription colorAttachmentDescription = {
            .format = graphicsBase::Base().SwapchainCreateInfo().imageFormat,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
        };

        // 附件 1：深度图像。
        // 每帧开始清成 1.0，表示“最远”；渲染结束后保持深度附件布局，供下次继续作为 depth attachment 使用。
        VkAttachmentDescription depthAttachmentDescription = {
            .format = rpwf.depthFormat,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        };

        // 两个attachment，两个subpass
        VkAttachmentReference colorAttachmentReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkAttachmentReference depthAttachmentReference = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
        VkSubpassDescription subpassDescription = {
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachmentReference,
            .pDepthStencilAttachment = &depthAttachmentReference
        };

        // 这里同时把颜色输出和深度测试阶段的依赖都描述清楚。
        VkSubpassDependency subpassDependency = {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT
        };

        VkAttachmentDescription attachmentDescriptions[] = { colorAttachmentDescription, depthAttachmentDescription };
        VkRenderPassCreateInfo renderPassCreateInfo = {
            .attachmentCount = 2,
            .pAttachments = attachmentDescriptions,
            .subpassCount = 1,
            .pSubpasses = &subpassDescription,
            .dependencyCount = 1,
            .pDependencies = &subpassDependency
        };
        rpwf.renderPass.Create(renderPassCreateInfo);

        auto CreateDepthResources = [] {
            auto device = graphicsBase::Base().Device();

            // depth image 是我们自己创建的离屏图像，大小与交换链一致。
            VkImageCreateInfo imageCreateInfo = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                .imageType = VK_IMAGE_TYPE_2D,
                .format = rpwf.depthFormat,
                .extent = { windowSize.width, windowSize.height, 1 },
                .mipLevels = 1,
                .arrayLayers = 1,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .tiling = VK_IMAGE_TILING_OPTIMAL,
                .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
            };
            if (VkResult result = vkCreateImage(device, &imageCreateInfo, nullptr, &rpwf.depthImage)) {
                outStream << std::format("[ easyVulkan ] ERROR\nFailed to create depth image!\nError code: {}\n", int32_t(result));
                abort();
            }

            VkMemoryRequirements requirements{};
            vkGetImageMemoryRequirements(device, rpwf.depthImage, &requirements);
            VkMemoryAllocateInfo allocateInfo = {
                .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                .allocationSize = requirements.size,
                .memoryTypeIndex = FindMemoryTypeIndex(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
            };
            if (VkResult result = vkAllocateMemory(device, &allocateInfo, nullptr, &rpwf.depthMemory)) {
                outStream << std::format("[ easyVulkan ] ERROR\nFailed to allocate depth memory!\nError code: {}\n", int32_t(result));
                abort();
            }
            vkBindImageMemory(device, rpwf.depthImage, rpwf.depthMemory, 0);

            VkImageViewCreateInfo imageViewCreateInfo = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = rpwf.depthImage,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = rpwf.depthFormat,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1
                }
            };
            if (VkResult result = vkCreateImageView(device, &imageViewCreateInfo, nullptr, &rpwf.depthImageView)) {
                outStream << std::format("[ easyVulkan ] ERROR\nFailed to create depth image view!\nError code: {}\n", int32_t(result));
                abort();
            }
        };

        auto DestroyDepthResources = [] {
            auto device = graphicsBase::Base().Device();
            if (rpwf.depthImageView)
                vkDestroyImageView(device, rpwf.depthImageView, nullptr);
            if (rpwf.depthImage)
                vkDestroyImage(device, rpwf.depthImage, nullptr);
            if (rpwf.depthMemory)
                vkFreeMemory(device, rpwf.depthMemory, nullptr);
            rpwf.depthImageView = VK_NULL_HANDLE;
            rpwf.depthImage = VK_NULL_HANDLE;
            rpwf.depthMemory = VK_NULL_HANDLE;
        };

        auto CreateFramebuffers = [] {
            rpwf.framebuffers.resize(graphicsBase::Base().SwapchainImageCount());
            VkFramebufferCreateInfo framebufferCreateInfo = {
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .renderPass = rpwf.renderPass,
                .attachmentCount = 2,
                .width = windowSize.width,
                .height = windowSize.height,
                .layers = 1
            };
            for (size_t i = 0; i < graphicsBase::Base().SwapchainImageCount(); i++) {
                VkImageView attachments[] = {
                    graphicsBase::Base().SwapchainImageView(static_cast<uint32_t>(i)),
                    rpwf.depthImageView
                };
                framebufferCreateInfo.pAttachments = attachments;
                rpwf.framebuffers[i].Create(framebufferCreateInfo);
            }
        };

        auto DestroyFramebuffers = [] {
            rpwf.framebuffers.clear(); //清空vector中的元素时会逐一执行析构函数
        };

        CreateDepthResources();
        CreateFramebuffers();

        // 这个函数可能被多次访问，但回调只应该注册一次。
        ExecuteOnce(rpwf);
        graphicsBase::Base().AddCallback_DestroySwapchain(DestroyFramebuffers);
        graphicsBase::Base().AddCallback_DestroySwapchain(DestroyDepthResources);
        graphicsBase::Base().AddCallback_CreateSwapchain(CreateDepthResources);
        graphicsBase::Base().AddCallback_CreateSwapchain(CreateFramebuffers);

        return rpwf;
    }

    const auto& CreateRpwf_Shadow() {
        static shadowRenderPassResources rpwf;

        rpwf.depthFormat = FindShadowDepthFormat();
        rpwf.extent = { 2048, 2048 };

        VkAttachmentDescription depthAttachmentDescription = {
            .format = rpwf.depthFormat,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        };

        VkAttachmentReference depthAttachmentReference = { 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
        VkSubpassDescription subpassDescription = {
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .pDepthStencilAttachment = &depthAttachmentReference
        };

        VkSubpassDependency subpassDependency = {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT
        };

        VkRenderPassCreateInfo renderPassCreateInfo = {
            .attachmentCount = 1,
            .pAttachments = &depthAttachmentDescription,
            .subpassCount = 1,
            .pSubpasses = &subpassDescription,
            .dependencyCount = 1,
            .pDependencies = &subpassDependency
        };
        rpwf.shadowPass.Create(renderPassCreateInfo);

        auto CreateShadowResources = [] {
            auto device = graphicsBase::Base().Device();

            VkImageCreateInfo imageCreateInfo = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                .imageType = VK_IMAGE_TYPE_2D,
                .format = rpwf.depthFormat,
                .extent = { rpwf.extent.width, rpwf.extent.height, 1 },
                .mipLevels = 1,
                .arrayLayers = 1,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .tiling = VK_IMAGE_TILING_OPTIMAL,
                .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
            };
            if (VkResult result = vkCreateImage(device, &imageCreateInfo, nullptr, &rpwf.depthImage)) {
                outStream << std::format("[ easyVulkan ] ERROR\nFailed to create shadow image!\nError code: {}\n", int32_t(result));
                abort();
            }

            VkMemoryRequirements requirements{};
            vkGetImageMemoryRequirements(device, rpwf.depthImage, &requirements);
            VkMemoryAllocateInfo allocateInfo = {
                .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                .allocationSize = requirements.size,
                .memoryTypeIndex = FindMemoryTypeIndex(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
            };
            if (VkResult result = vkAllocateMemory(device, &allocateInfo, nullptr, &rpwf.depthMemory)) {
                outStream << std::format("[ easyVulkan ] ERROR\nFailed to allocate shadow memory!\nError code: {}\n", int32_t(result));
                abort();
            }
            vkBindImageMemory(device, rpwf.depthImage, rpwf.depthMemory, 0);

            VkImageViewCreateInfo imageViewCreateInfo = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = rpwf.depthImage,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = rpwf.depthFormat,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1
                }
            };
            if (VkResult result = vkCreateImageView(device, &imageViewCreateInfo, nullptr, &rpwf.depthImageView)) {
                outStream << std::format("[ easyVulkan ] ERROR\nFailed to create shadow image view!\nError code: {}\n", int32_t(result));
                abort();
            }

            VkFramebufferCreateInfo framebufferCreateInfo = {
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .renderPass = rpwf.shadowPass,
                .attachmentCount = 1,
                .pAttachments = &rpwf.depthImageView,
                .width = rpwf.extent.width,
                .height = rpwf.extent.height,
                .layers = 1
            };
            rpwf.shadowFramebuffer.Create(framebufferCreateInfo);

            VkSamplerCreateInfo samplerCreateInfo{};
            samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerCreateInfo.magFilter = VK_FILTER_NEAREST;
            samplerCreateInfo.minFilter = VK_FILTER_NEAREST;
            samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            samplerCreateInfo.mipLodBias = 0.0f;
            samplerCreateInfo.anisotropyEnable = VK_FALSE;
            samplerCreateInfo.compareEnable = VK_FALSE;
            samplerCreateInfo.minLod = 0.0f;
            samplerCreateInfo.maxLod = 0.0f;
            samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
            samplerCreateInfo.unnormalizedCoordinates = VK_FALSE;
            if (VkResult result = vkCreateSampler(device, &samplerCreateInfo, nullptr, &rpwf.shadowSampler)) {
                outStream << std::format("[ easyVulkan ] ERROR\nFailed to create shadow sampler!\nError code: {}\n", int32_t(result));
                abort();
            }
        };

        auto DestroyShadowResources = [] {
            auto device = graphicsBase::Base().Device();
            rpwf.shadowFramebuffer.~framebuffer();
            if (rpwf.shadowSampler)
                vkDestroySampler(device, rpwf.shadowSampler, nullptr);
            if (rpwf.depthImageView)
                vkDestroyImageView(device, rpwf.depthImageView, nullptr);
            if (rpwf.depthImage)
                vkDestroyImage(device, rpwf.depthImage, nullptr);
            if (rpwf.depthMemory)
                vkFreeMemory(device, rpwf.depthMemory, nullptr);
            rpwf.shadowSampler = VK_NULL_HANDLE;
            rpwf.depthImageView = VK_NULL_HANDLE;
            rpwf.depthImage = VK_NULL_HANDLE;
            rpwf.depthMemory = VK_NULL_HANDLE;
            rpwf.shadowPass.~renderPass();
        };

        CreateShadowResources();

        ExecuteOnce(rpwf);
        graphicsBase::Base().AddCallback_DestroyDevice(DestroyShadowResources);

        return rpwf;
    }
}
