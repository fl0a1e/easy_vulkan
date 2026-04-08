#include <algorithm>
#include <filesystem>

#include "GlfwGeneral.hpp"
#include "easyVk.hpp"
#include "Camera.hpp"
#include "ImageLoader.hpp"

using namespace vulkan;

// 一个顶点包含位置、调试用颜色和纹理坐标三部分属性。
// 颜色保留着，方便后面继续做“顶点色 * 纹理”的混合调试。
struct Vertex {
    glm::vec3 position;
    glm::vec3 color;
    glm::vec2 uv;
};

// MVP 是最基础的 3D 变换链：
// model 把模型从局部空间放到世界里，
// view 表示摄像机观察，
// proj 负责把 3D 投影到屏幕。
struct UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
};

// 光照信息
// 平行光
struct Light {
    glm::vec3 position;
    float _pad1;
    glm::vec3 dir;
    float _pad2;
    glm::vec3 color;
    float _pad3;
};


pipelineLayout pipelineLayout_cube; // 立方体管线布局
pipeline pipeline_cube;             // 立方体管线
buffer vertexBuffer_cube;           // 顶点缓冲
buffer indexBuffer_cube;            // 索引缓冲
buffer uniformBuffer_cube;          // uniform 缓冲

deviceMemory vertexMemory_cube;     // 顶点缓冲绑定的显存
deviceMemory indexMemory_cube;      // 索引缓冲绑定的显存
deviceMemory uniformMemory_cube;    // uniform 缓冲绑定的显存

VkImage textureImage_cube = VK_NULL_HANDLE;
VkImageView textureImageView_cube = VK_NULL_HANDLE;
VkSampler textureSampler_cube = VK_NULL_HANDLE;
deviceMemory textureMemory_cube;    // 纹理图像绑定的显存
uint32_t textureMipLevels_cube = 1; // 纹理实际创建的 mip 层数

deviceMemory lightMemory; // 光照信息绑定的显存
buffer lightBuffer; // 光照信息的uniform buffer

VkDescriptorSetLayout descriptorSetLayout_cube = VK_NULL_HANDLE;
VkDescriptorPool descriptorPool_cube = VK_NULL_HANDLE;
VkDescriptorSet descriptorSet_cube = VK_NULL_HANDLE;

camera camera_main; // 主相机，只负责生成 view/proj

// 立方体每个面都需要自己独立的一套 UV，
// 所以这里不再复用 8 个角点，而是拆成 24 个顶点。
const std::array<Vertex, 24> cubeVertices = {
    Vertex{ {-0.5f, -0.5f, -0.5f}, {1.f, 1.f, 1.f}, {0.f, 1.f} },
    Vertex{ { 0.5f, -0.5f, -0.5f}, {1.f, 1.f, 1.f}, {1.f, 1.f} },
    Vertex{ { 0.5f,  0.5f, -0.5f}, {1.f, 1.f, 1.f}, {1.f, 0.f} },
    Vertex{ {-0.5f,  0.5f, -0.5f}, {1.f, 1.f, 1.f}, {0.f, 0.f} },

    Vertex{ {-0.5f, -0.5f,  0.5f}, {1.f, 1.f, 1.f}, {0.f, 1.f} },
    Vertex{ { 0.5f, -0.5f,  0.5f}, {1.f, 1.f, 1.f}, {1.f, 1.f} },
    Vertex{ { 0.5f,  0.5f,  0.5f}, {1.f, 1.f, 1.f}, {1.f, 0.f} },
    Vertex{ {-0.5f,  0.5f,  0.5f}, {1.f, 1.f, 1.f}, {0.f, 0.f} },

    Vertex{ {-0.5f, -0.5f, -0.5f}, {1.f, 1.f, 1.f}, {0.f, 1.f} },
    Vertex{ {-0.5f,  0.5f, -0.5f}, {1.f, 1.f, 1.f}, {1.f, 1.f} },
    Vertex{ {-0.5f,  0.5f,  0.5f}, {1.f, 1.f, 1.f}, {1.f, 0.f} },
    Vertex{ {-0.5f, -0.5f,  0.5f}, {1.f, 1.f, 1.f}, {0.f, 0.f} },

    Vertex{ { 0.5f, -0.5f, -0.5f}, {1.f, 1.f, 1.f}, {0.f, 1.f} },
    Vertex{ { 0.5f, -0.5f,  0.5f}, {1.f, 1.f, 1.f}, {1.f, 1.f} },
    Vertex{ { 0.5f,  0.5f,  0.5f}, {1.f, 1.f, 1.f}, {1.f, 0.f} },
    Vertex{ { 0.5f,  0.5f, -0.5f}, {1.f, 1.f, 1.f}, {0.f, 0.f} },

    Vertex{ {-0.5f,  0.5f, -0.5f}, {1.f, 1.f, 1.f}, {0.f, 1.f} },
    Vertex{ { 0.5f,  0.5f, -0.5f}, {1.f, 1.f, 1.f}, {1.f, 1.f} },
    Vertex{ { 0.5f,  0.5f,  0.5f}, {1.f, 1.f, 1.f}, {1.f, 0.f} },
    Vertex{ {-0.5f,  0.5f,  0.5f}, {1.f, 1.f, 1.f}, {0.f, 0.f} },

    Vertex{ {-0.5f, -0.5f, -0.5f}, {1.f, 1.f, 1.f}, {0.f, 1.f} },
    Vertex{ {-0.5f, -0.5f,  0.5f}, {1.f, 1.f, 1.f}, {1.f, 1.f} },
    Vertex{ { 0.5f, -0.5f,  0.5f}, {1.f, 1.f, 1.f}, {1.f, 0.f} },
    Vertex{ { 0.5f, -0.5f, -0.5f}, {1.f, 1.f, 1.f}, {0.f, 0.f} }
};

const std::array<uint16_t, 36> cubeIndices = {
    0, 1, 2, 0, 2, 3,
    4, 6, 5, 4, 7, 6,
    8, 9, 10, 8, 10, 11,
    12, 13, 14, 12, 14, 15,
    16, 17, 18, 16, 18, 19,
    20, 22, 21, 20, 23, 22
};

const Light lightData{ {3.0f, 3.0f,3.0f},0, {-1.0f ,-1.0f, -1.0f}, 0,{0.f, 0.f, 1.0f} };

// 找合适的内存类型
// Vulkan 告诉你“这类资源可以绑定哪些类型的内存”，再从物理设备支持的内存类型里选一个满足要求的。
uint32_t FindMemoryTypeIndex(uint32_t memoryTypeBits, VkMemoryPropertyFlags requiredProperties) {
    const auto& memoryProperties = graphicsBase::Base().PhysicalDeviceMemoryProperties();
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
        const bool typeMatches = memoryTypeBits & (1u << i);
        const bool propertyMatches =
            (memoryProperties.memoryTypes[i].propertyFlags & requiredProperties) == requiredProperties;
        if (typeMatches && propertyMatches)
            return i;
    }

    std::cout << std::format(
        "[ main ] ERROR\nFailed to find a memory type with flags: {}\n",
        static_cast<uint32_t>(requiredProperties));
    abort();
}

uint32_t ComputeMipLevelCount(uint32_t width, uint32_t height) {
    uint32_t mipLevels = 1;
    while (width > 1 || height > 1) {
        width = std::max(1u, width / 2);
        height = std::max(1u, height / 2);
        ++mipLevels;
    }
    return mipLevels;
}
std::filesystem::path FindAssetPath(const std::filesystem::path& relativePath) {
    const auto current = std::filesystem::current_path();
    const std::array<std::filesystem::path, 4> candidates = {
        current / relativePath,
        current / "assets" / relativePath.filename(),
        current.parent_path() / relativePath,
        current.parent_path().parent_path() / relativePath
    };

    for (const auto& candidate : candidates) {
        if (!candidate.empty() && std::filesystem::exists(candidate))
            return candidate;
    }

    std::cout << std::format("[ main ] ERROR\nFailed to locate asset: {}\n", relativePath.string());
    abort();
}

// 这个函数把渲染到屏幕所需的 render pass + framebuffer 集合缓存起来。
// 做成静态局部变量，是因为这套对象全局只需要一份，后续直接复用即可。
const auto& RenderPassAndFramebuffers() {
    static const auto& rpwf = easyVulkan::CreateRpwf_Screen();
    return rpwf;
}

void SubmitSingleTimeCommands(const std::function<void(VkCommandBuffer)>& record) {
    commandPool pool(graphicsBase::Base().QueueFamilyIndex_Graphics(), VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
    commandBuffer cmd;
    pool.AllocateBuffers(cmd);
    cmd.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    record(cmd);
    cmd.End();

    fence submitFence;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = cmd.Address();

    if (VkResult result = vkQueueSubmit(graphicsBase::Base().Queue_Graphics(), 1, &submitInfo, submitFence)) {
        std::cout << std::format("[ main ] ERROR\nFailed to submit one-time command buffer!\nError code: {}\n", int32_t(result));
        abort();
    }
    submitFence.Wait();
}

void CmdTransitionImageLayout(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkImageAspectFlags aspectMask,
    uint32_t baseMipLevel = 0,
    uint32_t levelCount = 1)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspectMask;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else {
        std::cout << std::format(
            "[ main ] ERROR\nUnsupported image layout transition: {} -> {}\n",
            int32_t(oldLayout),
            int32_t(newLayout));
        abort();
    }

    vkCmdPipelineBarrier(
        commandBuffer,
        srcStage,
        dstStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier);
}

void CmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer sourceBuffer, VkImage destinationImage, uint32_t width, uint32_t height) {
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { width, height, 1 };

    vkCmdCopyBufferToImage(
        commandBuffer,
        sourceBuffer,
        destinationImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region);
}

void CmdGenerateMipmaps(VkCommandBuffer commandBuffer, VkImage image, VkFormat imageFormat, int32_t width, int32_t height, uint32_t mipLevels) {
    VkFormatProperties formatProperties{};
    vkGetPhysicalDeviceFormatProperties(graphicsBase::Base().PhysicalDevice(), imageFormat, &formatProperties);
    if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
        std::cout << std::format("[ main ] ERROR\nTexture format does not support linear blit for mipmap generation!\n");
        abort();
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    int32_t mipWidth = width;
    int32_t mipHeight = height;
    for (uint32_t i = 1; i < mipLevels; ++i) {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier);

        VkImageBlit blit{};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;
        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = {
            std::max(1, mipWidth / 2),
            std::max(1, mipHeight / 2),
            1
        };

        vkCmdBlitImage(
            commandBuffer,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &blit,
            VK_FILTER_LINEAR);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier);

        mipWidth = std::max(1, mipWidth / 2);
        mipHeight = std::max(1, mipHeight / 2);
    }

    barrier.subresourceRange.baseMipLevel = mipLevels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier);
}
void CreateDescriptorSetLayout() {
    // set 0 / binding 0：给 VS 的 uniform buffer
    // set 0 / binding 1：给 PS 的 sampled image
    // set 0 / binding 2：给 PS 的 sampler
    // set 0 / binding 3: 给 PS 的 光照数据
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding textureBinding{};
    textureBinding.binding = 1;
    textureBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    textureBinding.descriptorCount = 1;
    textureBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 2;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding lightBinding{};
    lightBinding.binding = 3;
    lightBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    lightBinding.descriptorCount = 1;
    lightBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    const std::array bindings = { uboBinding, textureBinding, samplerBinding, lightBinding };

    VkDescriptorSetLayoutCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    createInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    createInfo.pBindings = bindings.data();

    if (VkResult result = vkCreateDescriptorSetLayout(graphicsBase::Base().Device(), &createInfo, nullptr, &descriptorSetLayout_cube)) {
        std::cout << std::format("[ main ] ERROR\nFailed to create descriptor set layout!\nError code: {}\n", int32_t(result));
        abort();
    }
}

void CreateLayout() {
    // pipeline layout 描述的是“这个 pipeline 期望看到哪些 descriptor set layout”。
    // 真正这次 draw 使用哪一个 descriptor set，要在录命令时再 bind。
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
    pipelineLayoutCreateInfo.setLayoutCount = 1;
    pipelineLayoutCreateInfo.pSetLayouts = &descriptorSetLayout_cube;
    pipelineLayout_cube.Create(pipelineLayoutCreateInfo);
}

// CreateGeometry()：把立方体数据真正送进 GPU
void CreateGeometry() {
    auto CreateUploadBuffer = [](buffer& gpuBuffer, deviceMemory& gpuMemory, VkBufferUsageFlags usage, const void* pData, size_t size) {
        // 几何缓冲这边先继续走最容易理解的路径：
        // 创建一个 HOST_VISIBLE | HOST_COHERENT 的缓冲，让 CPU 可以直接写入数据。
        gpuBuffer.Create(size, usage);

        const auto requirements = gpuBuffer.MemoryRequirements();
        VkMemoryAllocateInfo allocateInfo = {
            .allocationSize = requirements.size,
            .memoryTypeIndex = FindMemoryTypeIndex(
                requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        };
        gpuMemory.Create(allocateInfo);
        gpuBuffer.BindMemory(gpuMemory); // 绑定 buffer 和 memory
        gpuMemory.Write(pData, size);    // map + memcpy + unmap
    };

    // 顶点缓冲负责提供每个顶点的属性；
    // 索引缓冲负责复用顶点，避免同一个角点在每个三角形里重复存一份。
    CreateUploadBuffer(
        vertexBuffer_cube,
        vertexMemory_cube,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        cubeVertices.data(),
        sizeof(cubeVertices));
    CreateUploadBuffer(
        indexBuffer_cube,
        indexMemory_cube,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        cubeIndices.data(),
        sizeof(cubeIndices));
}

void CreateUniformResources() {
    // uniform buffer 存的是“每帧会变化，但当前 draw 共用”的小块数据，
    // 这里就是 model/view/proj 三个矩阵。
    uniformBuffer_cube.Create(sizeof(UniformBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    const auto requirements = uniformBuffer_cube.MemoryRequirements();
    VkMemoryAllocateInfo allocateInfo = {
        .allocationSize = requirements.size,
        .memoryTypeIndex = FindMemoryTypeIndex(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
    };
    uniformMemory_cube.Create(allocateInfo);
    uniformBuffer_cube.BindMemory(uniformMemory_cube);
}

void CreateLightResources() {
    lightBuffer.Create(sizeof(Light), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    const auto requirements = lightBuffer.MemoryRequirements();
    VkMemoryAllocateInfo allocateInfo = {
        .allocationSize = requirements.size,
        .memoryTypeIndex = FindMemoryTypeIndex(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
    };
    lightMemory.Create(allocateInfo);
    lightBuffer.BindMemory(lightMemory);
    lightMemory.Write(&lightData, sizeof(lightData));
}

void CreateTextureResources() {
    const auto texturePath = FindAssetPath("assets/texture.jpg");
    const auto texture = imageLoading::LoadRgba8(texturePath);
    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(texture.pixels.size());
    textureMipLevels_cube = ComputeMipLevelCount(texture.width, texture.height);

    buffer stagingBuffer;
    deviceMemory stagingMemory;
    stagingBuffer.Create(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    {
        const auto requirements = stagingBuffer.MemoryRequirements();
        VkMemoryAllocateInfo allocateInfo = {
            .allocationSize = requirements.size,
            .memoryTypeIndex = FindMemoryTypeIndex(
                requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        };
        stagingMemory.Create(allocateInfo);
        stagingBuffer.BindMemory(stagingMemory);
        stagingMemory.Write(texture.pixels.data(), texture.pixels.size());
    }

    VkImageCreateInfo imageCreateInfo{};
    imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageCreateInfo.extent = { texture.width, texture.height, 1 };
    imageCreateInfo.mipLevels = textureMipLevels_cube;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (VkResult result = vkCreateImage(graphicsBase::Base().Device(), &imageCreateInfo, nullptr, &textureImage_cube)) {
        std::cout << std::format("[ main ] ERROR\nFailed to create texture image!\nError code: {}\n", int32_t(result));
        abort();
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(graphicsBase::Base().Device(), textureImage_cube, &requirements);
    VkMemoryAllocateInfo allocateInfo = {
        .allocationSize = requirements.size,
        .memoryTypeIndex = FindMemoryTypeIndex(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    };
    textureMemory_cube.Create(allocateInfo);
    if (VkResult result = vkBindImageMemory(graphicsBase::Base().Device(), textureImage_cube, textureMemory_cube, 0)) {
        std::cout << std::format("[ main ] ERROR\nFailed to bind texture image memory!\nError code: {}\n", int32_t(result));
        abort();
    }

    SubmitSingleTimeCommands([&](VkCommandBuffer commandBuffer) {
        // 先把整张 image 的所有 mip 层都切到 transfer 目标布局，
        // 再把原始像素写到第 0 层，最后在 GPU 上逐层生成更小的 mip。
        CmdTransitionImageLayout(
            commandBuffer,
            textureImage_cube,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT,
            0,
            textureMipLevels_cube);
        CmdCopyBufferToImage(commandBuffer, stagingBuffer, textureImage_cube, texture.width, texture.height);
        CmdGenerateMipmaps(commandBuffer, textureImage_cube, VK_FORMAT_R8G8B8A8_SRGB, texture.width, texture.height, textureMipLevels_cube);
    });

    VkImageViewCreateInfo imageViewCreateInfo{};
    imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewCreateInfo.image = textureImage_cube;
    imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCreateInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
    imageViewCreateInfo.subresourceRange.levelCount = textureMipLevels_cube;
    imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
    imageViewCreateInfo.subresourceRange.layerCount = 1;
    if (VkResult result = vkCreateImageView(graphicsBase::Base().Device(), &imageViewCreateInfo, nullptr, &textureImageView_cube)) {
        std::cout << std::format("[ main ] ERROR\nFailed to create texture image view!\nError code: {}\n", int32_t(result));
        abort();
    }

    VkPhysicalDeviceFeatures features{};
    vkGetPhysicalDeviceFeatures(graphicsBase::Base().PhysicalDevice(), &features);
    const auto maxAnisotropy = graphicsBase::Base().PhysicalDeviceProperties().limits.maxSamplerAnisotropy;

    VkSamplerCreateInfo samplerCreateInfo{};
    samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
    samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
    samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.mipLodBias = 0.0f;
    samplerCreateInfo.anisotropyEnable = features.samplerAnisotropy;
    samplerCreateInfo.maxAnisotropy = features.samplerAnisotropy ? maxAnisotropy : 1.0f;
    samplerCreateInfo.compareEnable = VK_FALSE;
    samplerCreateInfo.minLod = 0.0f;
    samplerCreateInfo.maxLod = static_cast<float>(textureMipLevels_cube - 1);
    samplerCreateInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerCreateInfo.unnormalizedCoordinates = VK_FALSE;
    if (VkResult result = vkCreateSampler(graphicsBase::Base().Device(), &samplerCreateInfo, nullptr, &textureSampler_cube)) {
        std::cout << std::format("[ main ] ERROR\nFailed to create texture sampler!\nError code: {}\n", int32_t(result));
        abort();
    }
}
void CreateDescriptorSet() {
    const std::array poolSizes = {
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 },
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1 },
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLER, 1 }
    };

    VkDescriptorPoolCreateInfo poolCreateInfo{};
    poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCreateInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolCreateInfo.pPoolSizes = poolSizes.data();
    poolCreateInfo.maxSets = 1;

    if (VkResult result = vkCreateDescriptorPool(graphicsBase::Base().Device(), &poolCreateInfo, nullptr, &descriptorPool_cube)) {
        std::cout << std::format("[ main ] ERROR\nFailed to create descriptor pool!\nError code: {}\n", int32_t(result));
        abort();
    }

    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = descriptorPool_cube;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &descriptorSetLayout_cube;

    if (VkResult result = vkAllocateDescriptorSets(graphicsBase::Base().Device(), &allocateInfo, &descriptorSet_cube)) {
        std::cout << std::format("[ main ] ERROR\nFailed to allocate descriptor set!\nError code: {}\n", int32_t(result));
        abort();
    }

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = uniformBuffer_cube;
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(UniformBufferObject);

    VkDescriptorImageInfo textureInfo{};
    textureInfo.imageView = textureImageView_cube;
    textureInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo samplerInfo{};
    samplerInfo.sampler = textureSampler_cube;

    VkDescriptorBufferInfo lightInfo{};
    lightInfo.buffer = lightBuffer;
    lightInfo.offset = 0;
    lightInfo.range = sizeof(Light);

    // descriptor set 里记录的不是数据本体，而是“shader 应该去哪里读这些资源”的描述信息。
    std::array<VkWriteDescriptorSet, 4> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = descriptorSet_cube;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &bufferInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = descriptorSet_cube;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[1].pImageInfo = &textureInfo;

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = descriptorSet_cube;
    writes[2].dstBinding = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    writes[2].pImageInfo = &samplerInfo;

    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = descriptorSet_cube;
    writes[3].dstBinding = 3;
    writes[3].descriptorCount = 1;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[3].pBufferInfo = &lightInfo;

    vkUpdateDescriptorSets(graphicsBase::Base().Device(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void UpdateUniformBuffer() {
    static const auto startTime = std::chrono::high_resolution_clock::now();
    const auto now = std::chrono::high_resolution_clock::now();
    const float time = std::chrono::duration<float>(now - startTime).count();

    UniformBufferObject ubo{};
    ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(60.0f), glm::vec3(0.4f, 1.0f, 0.2f));

    // 相机模块只负责观察和投影：
    // view 表示“相机从哪里看”，proj 表示“怎么把 3D 压到屏幕上”。
    ubo.view = camera_main.View();
    ubo.proj = camera_main.Projection(windowSize);

    uniformMemory_cube.Write(&ubo, sizeof(ubo));

    // update light struct
    //Light light_ubo{};
    //lightMemory.Write(&light_ubo, sizeof(light_ubo));
}

void CreatePipeline() {
    // shader 现在从顶点缓冲读取位置、颜色、UV，
    // PS 再根据 descriptor set 里的纹理资源做采样。
    static shaderModule vs("shaders/triangle.vs.spv");
    static shaderModule ps("shaders/triangle.ps.spv");
    static VkPipelineShaderStageCreateInfo shaderStageCreateInfos_cube[2] = {
        vs.StageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT),
        ps.StageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT)
    };

    auto Create = [] {
        graphicsPipelineCreateInfoPack pipelineCiPack;
        pipelineCiPack.createInfo.layout = pipelineLayout_cube;
        pipelineCiPack.createInfo.renderPass = RenderPassAndFramebuffers().renderPass;
        pipelineCiPack.inputAssemblyStateCi.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        pipelineCiPack.rasterizationStateCi.polygonMode = VK_POLYGON_MODE_FILL;
        pipelineCiPack.rasterizationStateCi.cullMode = VK_CULL_MODE_BACK_BIT;
        pipelineCiPack.rasterizationStateCi.frontFace = VK_FRONT_FACE_CLOCKWISE;
        pipelineCiPack.rasterizationStateCi.lineWidth = 1.0f;

        // binding 0 是整条顶点流；
        // location 0/1/2 分别对应 position / color / uv。
        pipelineCiPack.vertexInputBindings.push_back({
            .binding = 0,
            .stride = sizeof(Vertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
        });
        pipelineCiPack.vertexInputAttributes.push_back({
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(Vertex, position)
        });
        pipelineCiPack.vertexInputAttributes.push_back({
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(Vertex, color)
        });
        pipelineCiPack.vertexInputAttributes.push_back({
            .location = 2,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(Vertex, uv)
        });

        pipelineCiPack.viewports.emplace_back(0.f, 0.f, float(windowSize.width), float(windowSize.height), 0.f, 1.f);
        pipelineCiPack.scissors.emplace_back(VkOffset2D{}, windowSize);
        pipelineCiPack.multisampleStateCi.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        pipelineCiPack.depthStencilStateCi.depthTestEnable = VK_TRUE;
        pipelineCiPack.depthStencilStateCi.depthWriteEnable = VK_TRUE;
        pipelineCiPack.depthStencilStateCi.depthCompareOp = VK_COMPARE_OP_LESS;
        pipelineCiPack.colorBlendAttachmentStates.push_back({ .colorWriteMask = 0b1111 });
        pipelineCiPack.UpdateAllArrays();
        pipelineCiPack.createInfo.stageCount = 2;
        pipelineCiPack.createInfo.pStages = shaderStageCreateInfos_cube;
        pipeline_cube.Create(pipelineCiPack);
    };

    auto Destroy = [] {
        pipeline_cube.~pipeline();
    };

    graphicsBase::Base().AddCallback_CreateSwapchain(Create);
    graphicsBase::Base().AddCallback_DestroySwapchain(Destroy);
    Create();
}

void DestroyTextureResources() {
    auto device = graphicsBase::Base().Device();
    if (textureSampler_cube)
        vkDestroySampler(device, textureSampler_cube, nullptr);
    if (textureImageView_cube)
        vkDestroyImageView(device, textureImageView_cube, nullptr);
    if (textureImage_cube)
        vkDestroyImage(device, textureImage_cube, nullptr);
    textureSampler_cube = VK_NULL_HANDLE;
    textureImageView_cube = VK_NULL_HANDLE;
    textureImage_cube = VK_NULL_HANDLE;
}

void DestroyDescriptors() {
    auto device = graphicsBase::Base().Device();
    if (descriptorPool_cube)
        vkDestroyDescriptorPool(device, descriptorPool_cube, nullptr);
    if (descriptorSetLayout_cube)
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout_cube, nullptr);
    descriptorPool_cube = VK_NULL_HANDLE;
    descriptorSetLayout_cube = VK_NULL_HANDLE;
    descriptorSet_cube = VK_NULL_HANDLE;
}

int main() {
    if (!InitializeWindow({ 1280, 720 }))
        return -1;

    const auto& rpwf = RenderPassAndFramebuffers();
    const auto& renderPass = rpwf.renderPass;
    const auto& framebuffers = rpwf.framebuffers;
    CreateDescriptorSetLayout();
    CreateLayout();
    CreateGeometry();
    CreateUniformResources();
    CreateLightResources();
    CreateTextureResources();
    CreateDescriptorSet();
    CreatePipeline();
    camera_main.AttachToWindow(pWindow);

    // 先创建成 signaled，这样第一帧开头的 WaitAndReset 不会阻塞。
    fence fence(VK_FENCE_CREATE_SIGNALED_BIT);
    semaphore semaphore_imageIsAvailable;
    semaphore semaphore_renderingIsOver;

    commandBuffer commandBuffer;
    commandPool commandPool(graphicsBase::Base().QueueFamilyIndex_Graphics(), VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandPool.AllocateBuffers(commandBuffer);

    VkClearValue clearValues[] = {
        { .color = { 0.2f, 0.2f, 0.2f, 1.f } },
        { .depthStencil = { 1.0f, 0 } }
    };

    while (!glfwWindowShouldClose(pWindow)) {
        while (glfwGetWindowAttrib(pWindow, GLFW_ICONIFIED))
            glfwWaitEvents();

        static auto lastFrameTime = std::chrono::high_resolution_clock::now();
        const auto now = std::chrono::high_resolution_clock::now();
        const float deltaTime = std::chrono::duration<float>(now - lastFrameTime).count();
        lastFrameTime = now;

        glfwPollEvents();
        camera_main.UpdateFromInput(pWindow, deltaTime);

        // 单帧 in-flight 写法：
        // 每一帧开始先等上一帧 GPU 完成，再把 fence 重置回 unsignaled，
        // 这样这次 vkQueueSubmit 才能合法地再次使用它。
        fence.WaitAndReset();

        UpdateUniformBuffer();
        graphicsBase::Base().SwapImage(semaphore_imageIsAvailable);
        auto i = graphicsBase::Base().CurrentImageIndex();

        commandBuffer.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        renderPass.CmdBegin(commandBuffer, framebuffers[i], { {}, windowSize }, clearValues);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_cube);

        // pipeline 只知道“shader 需要一个 set 0”，
        // 真正这次 draw 用哪一个 descriptor set，还是在命令里绑定。
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout_cube,
            0,
            1,
            &descriptorSet_cube,
            0,
            nullptr);

        // 顶点缓冲提供顶点属性，索引缓冲决定三角形怎么拼接这些顶点。
        VkBuffer vertexBuffers[] = { vertexBuffer_cube };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(commandBuffer, indexBuffer_cube, 0, VK_INDEX_TYPE_UINT16);
        vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(cubeIndices.size()), 1, 0, 0, 0);

        renderPass.CmdEnd(commandBuffer);
        commandBuffer.End();

        graphicsBase::Base().SubmitCommandBuffer_Graphics(commandBuffer, semaphore_imageIsAvailable, semaphore_renderingIsOver, fence);
        graphicsBase::Base().PresentImage(semaphore_renderingIsOver);

        TitleFps();
    }

    vkDeviceWaitIdle(graphicsBase::Base().Device());
    DestroyDescriptors();
    DestroyTextureResources();
    TerminateWindow();
    return 0;
}



