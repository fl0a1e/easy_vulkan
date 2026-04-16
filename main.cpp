#include <algorithm>
#include <filesystem>

#include "GlfwGeneral.hpp"
#include "easyVk.hpp"
#include "Camera.hpp"
#include "ImageLoader.hpp"
#include "ModelLoader.hpp"
#include "Terrain.hpp"

using namespace vulkan;

// MVP 是最基础的 3D 变换链：
// model 把模型从局部空间放到世界里，
// view 表示摄像机观察，
// proj 负责把 3D 投影到屏幕。
struct UniformBufferObject {
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 viewInv;
    glm::mat4 projInv;
    glm::vec3 cameraPosition;
    float _pad0;
};

struct PushConstantObject {
    glm::mat4 model;
    glm::vec4 meshInfo;
};

struct ShadowPushConstantObject {
    glm::mat4 model;
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

struct ShadowUniformObject {
    glm::mat4 lightViewProj;
};

struct MeshResource {
    buffer vertexBuffer;
    buffer indexBuffer;
    deviceMemory vertexMemory;
    deviceMemory indexMemory;
    uint32_t indexCount = 0;
    bool hasTexcoord = true;
};

struct MaterialResource {
    VkImage textureImage = VK_NULL_HANDLE;
    VkImageView textureImageView = VK_NULL_HANDLE;
    VkSampler textureSampler = VK_NULL_HANDLE;
    deviceMemory textureMemory;
    uint32_t textureMipLevels = 1;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
};

struct RenderObject {
    uint32_t meshIndex;
    uint32_t materialIndex;
    glm::vec3 position;
    glm::vec3 rotationAxis;
    float rotationSpeed;
    float rotationOffset;
    glm::vec3 scale;
};

pipelineLayout pipelineLayout_gbuffer;  // G-buffer pass 管线布局
pipelineLayout pipelineLayout_shadow;   // shadow pass 管线布局
pipelineLayout pipelineLayout_lighting; // lighting pass 管线布局
pipeline pipeline_gbuffer;              // G-buffer 管线
pipeline pipeline_shadow;               // shadow map 管线
pipeline pipeline_lighting;             // fullscreen lighting 管线
buffer uniformBuffer_camera;            // 相机 uniform 缓冲
buffer lightBuffer;                     // 光照 uniform 缓冲
buffer shadowUniformBuffer;             // 光源视角矩阵

deviceMemory uniformMemory_camera;      // 相机 uniform 显存
deviceMemory lightMemory;               // 光照 uniform 显存
deviceMemory shadowUniformMemory;       // shadow uniform 显存

VkDescriptorSetLayout descriptorSetLayout_gbuffer = VK_NULL_HANDLE;
VkDescriptorSetLayout descriptorSetLayout_shadow = VK_NULL_HANDLE;
VkDescriptorSetLayout descriptorSetLayout_lighting = VK_NULL_HANDLE;
VkDescriptorPool descriptorPool_gbuffer = VK_NULL_HANDLE;
VkDescriptorPool descriptorPool_shadow = VK_NULL_HANDLE;
VkDescriptorPool descriptorPool_lighting = VK_NULL_HANDLE;
VkDescriptorSet descriptorSet_shadow = VK_NULL_HANDLE;
VkDescriptorSet descriptorSet_lighting = VK_NULL_HANDLE;

camera camera_main; // 主相机，只负责生成 view/proj


// 很重要的解耦，渲染实体和资源分离
std::vector<MeshResource> meshResources;
std::vector<MaterialResource> materialResources;
std::vector<RenderObject> renderObjects;

const Light lightData{ {3.0f, 3.0f,3.0f},0, {-1.0f ,-1.0f, -1.0f}, 0,{1.f, 1.f, 1.f} };

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

const auto& ShadowRenderPassResources() {
    static const auto& rpwf = easyVulkan::CreateRpwf_Shadow();
    return rpwf;
}

const auto& GBufferPassResources() {
    static const auto& rpwf = easyVulkan::CreateRpwf_GBuffer();
    return rpwf;
}

std::vector<std::filesystem::path> DiscoverMeshPaths() {
    std::vector<std::filesystem::path> paths;
    const auto assetsDirectory = FindAssetPath("assets");
    for (const auto& entry : std::filesystem::directory_iterator(assetsDirectory)) {
        if (!entry.is_regular_file())
            continue;
        if (entry.path().extension() == ".obj")
            paths.push_back(entry.path());
    }

    std::ranges::sort(paths);
    if (paths.empty()) {
        std::cout << std::format("[ main ] ERROR\nNo obj files were found under: {}\n", assetsDirectory.string());
        abort();
    }
    return paths;
}

std::vector<std::filesystem::path> DiscoverMaterialPaths() {
    std::vector<std::filesystem::path> paths;
    const auto assetsDirectory = FindAssetPath("assets");
    for (const auto& entry : std::filesystem::directory_iterator(assetsDirectory)) {
        if (!entry.is_regular_file())
            continue;

        const auto extension = entry.path().extension().string();
        if (extension == ".jpg" || extension == ".jpeg" || extension == ".png" || extension == ".bmp" || extension == ".tga")
            paths.push_back(entry.path());
    }

    std::ranges::sort(paths);
    if (paths.empty()) {
        std::cout << std::format("[ main ] ERROR\nNo texture files were found under: {}\n", assetsDirectory.string());
        abort();
    }
    return paths;
}
std::vector<RenderObject> CreateDefaultRenderObjects(uint32_t modelMeshCount, uint32_t materialCount, uint32_t groundMeshIndex) {
    if (modelMeshCount == 0) {
        std::cout << "[ main ] ERROR\nCannot create render objects without mesh resources!\n";
        abort();
    }
    if (materialCount == 0) {
        std::cout << "[ main ] ERROR\nCannot create render objects without material resources!\n";
        abort();
    }

    return {
        { 0 % modelMeshCount, 0 % materialCount, {-3.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, glm::radians(22.0f), 0.0f, {0.0005f, 0.0005f, 0.0005f} },
        { 1 % modelMeshCount, 1 % materialCount, {-1.5f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, glm::radians(35.0f), 0.8f, {0.5f, 0.5f, 0.5f} },
        { 2 % modelMeshCount, 0 % materialCount, { 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, glm::radians(20.0f), 0.0f, {3.f, 3.f, 3.f} },
        { 3 % modelMeshCount, 1 % materialCount, { 1.5f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, glm::radians(28.0f), 1.6f, {0.125f, 0.125f, 0.125f} },
        { 4 % modelMeshCount, 0 % materialCount, { 3.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, glm::radians(18.0f), 2.4f, {0.05f, 0.05f, 0.05f} },
        { groundMeshIndex, 0 % materialCount, { 0.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f, 0.0f, {1.0f, 1.0f, 1.0f} }
    };
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
    barrier.subresourceRange.baseMipLevel = baseMipLevel;
    barrier.subresourceRange.levelCount = levelCount;
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

void CmdPrepareShadowMapForSampling(VkCommandBuffer commandBuffer, VkImage shadowImage) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = shadowImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier);
}

void CmdPrepareGBufferForSampling(VkCommandBuffer commandBuffer) {
    const auto& gbuffer = GBufferPassResources();

    auto Transition = [&](VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkImageAspectFlags aspectMask, VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask) {
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
        barrier.srcAccessMask = srcAccessMask;
        barrier.dstAccessMask = dstAccessMask;

        vkCmdPipelineBarrier(
            commandBuffer,
            aspectMask == VK_IMAGE_ASPECT_COLOR_BIT ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT : (VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT),
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier);
    };

    Transition(
        gbuffer.albedo.image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT);
    Transition(
        gbuffer.normal.image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT);
    Transition(
        gbuffer.depth.image,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        VK_IMAGE_ASPECT_DEPTH_BIT,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT);
}

void CreateGBufferDescriptorSetLayout() {
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

    const std::array bindings = { uboBinding, textureBinding, samplerBinding };

    VkDescriptorSetLayoutCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    createInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    createInfo.pBindings = bindings.data();

    if (VkResult result = vkCreateDescriptorSetLayout(graphicsBase::Base().Device(), &createInfo, nullptr, &descriptorSetLayout_gbuffer)) {
        std::cout << std::format("[ main ] ERROR\nFailed to create G-buffer descriptor set layout!\nError code: {}\n", int32_t(result));
        abort();
    }
}

void CreateShadowDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding shadowMatrixBinding{};
    shadowMatrixBinding.binding = 0;
    shadowMatrixBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    shadowMatrixBinding.descriptorCount = 1;
    shadowMatrixBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    createInfo.bindingCount = 1;
    createInfo.pBindings = &shadowMatrixBinding;

    if (VkResult result = vkCreateDescriptorSetLayout(graphicsBase::Base().Device(), &createInfo, nullptr, &descriptorSetLayout_shadow)) {
        std::cout << std::format("[ main ] ERROR\nFailed to create shadow descriptor set layout!\nError code: {}\n", int32_t(result));
        abort();
    }
}

void CreateLightingDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding cameraBinding{};
    cameraBinding.binding = 0;
    cameraBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    cameraBinding.descriptorCount = 1;
    cameraBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding lightBinding{};
    lightBinding.binding = 1;
    lightBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    lightBinding.descriptorCount = 1;
    lightBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding shadowMatrixBinding{};
    shadowMatrixBinding.binding = 2;
    shadowMatrixBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    shadowMatrixBinding.descriptorCount = 1;
    shadowMatrixBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding albedoBinding{};
    albedoBinding.binding = 3;
    albedoBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    albedoBinding.descriptorCount = 1;
    albedoBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding gbufferSamplerBinding{};
    gbufferSamplerBinding.binding = 4;
    gbufferSamplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    gbufferSamplerBinding.descriptorCount = 1;
    gbufferSamplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding normalBinding{};
    normalBinding.binding = 5;
    normalBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    normalBinding.descriptorCount = 1;
    normalBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding depthBinding{};
    depthBinding.binding = 6;
    depthBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    depthBinding.descriptorCount = 1;
    depthBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding depthSamplerBinding{};
    depthSamplerBinding.binding = 7;
    depthSamplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    depthSamplerBinding.descriptorCount = 1;
    depthSamplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding shadowImageBinding{};
    shadowImageBinding.binding = 8;
    shadowImageBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    shadowImageBinding.descriptorCount = 1;
    shadowImageBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    const std::array bindings = {
        cameraBinding,
        lightBinding,
        shadowMatrixBinding,
        albedoBinding,
        gbufferSamplerBinding,
        normalBinding,
        depthBinding,
        depthSamplerBinding,
        shadowImageBinding
    };

    VkDescriptorSetLayoutCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    createInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    createInfo.pBindings = bindings.data();

    if (VkResult result = vkCreateDescriptorSetLayout(graphicsBase::Base().Device(), &createInfo, nullptr, &descriptorSetLayout_lighting)) {
        std::cout << std::format("[ main ] ERROR\nFailed to create lighting descriptor set layout!\nError code: {}\n", int32_t(result));
        abort();
    }
}

void CreateGBufferLayout() {
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstantObject);

    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
    pipelineLayoutCreateInfo.setLayoutCount = 1;
    pipelineLayoutCreateInfo.pSetLayouts = &descriptorSetLayout_gbuffer;
    pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
    pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
    pipelineLayout_gbuffer.Create(pipelineLayoutCreateInfo);
}

void CreateShadowLayout() {
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(ShadowPushConstantObject);

    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
    pipelineLayoutCreateInfo.setLayoutCount = 1;
    pipelineLayoutCreateInfo.pSetLayouts = &descriptorSetLayout_shadow;
    pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
    pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
    pipelineLayout_shadow.Create(pipelineLayoutCreateInfo);
}

void CreateLightingLayout() {
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
    pipelineLayoutCreateInfo.setLayoutCount = 1;
    pipelineLayoutCreateInfo.pSetLayouts = &descriptorSetLayout_lighting;
    pipelineLayout_lighting.Create(pipelineLayoutCreateInfo);
}

// CreateGeometry()：把模型数据真正送进 GPU
MeshResource CreateMeshResource(const modelLoading::MeshData& meshData) {
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

    MeshResource meshResource{};

    // 顶点缓冲负责提供每个顶点的属性；
    // 索引缓冲负责复用顶点，避免同一个角点在每个三角形里重复存一份。
    CreateUploadBuffer(
        meshResource.vertexBuffer,
        meshResource.vertexMemory,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        meshData.vertices.data(),
        sizeof(modelLoading::Vertex) * meshData.vertices.size());
    CreateUploadBuffer(
        meshResource.indexBuffer,
        meshResource.indexMemory,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        meshData.indices.data(),
        sizeof(uint32_t) * meshData.indices.size());

    meshResource.indexCount = static_cast<uint32_t>(meshData.indices.size());
    meshResource.hasTexcoord = meshData.hasTexcoord;
    return meshResource;
}

void DestroyMeshResources() {
    meshResources.clear();
}
void CreateUniformResources() {
    uniformBuffer_camera.Create(sizeof(UniformBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    const auto requirements = uniformBuffer_camera.MemoryRequirements();
    VkMemoryAllocateInfo allocateInfo = {
        .allocationSize = requirements.size,
        .memoryTypeIndex = FindMemoryTypeIndex(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
    };
    uniformMemory_camera.Create(allocateInfo);
    uniformBuffer_camera.BindMemory(uniformMemory_camera);
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

void CreateShadowUniformResources() {
    shadowUniformBuffer.Create(sizeof(ShadowUniformObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    const auto requirements = shadowUniformBuffer.MemoryRequirements();
    VkMemoryAllocateInfo allocateInfo = {
        .allocationSize = requirements.size,
        .memoryTypeIndex = FindMemoryTypeIndex(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
    };
    shadowUniformMemory.Create(allocateInfo);
    shadowUniformBuffer.BindMemory(shadowUniformMemory);
}

MaterialResource CreateMaterialResource(const std::filesystem::path& texturePath) {
    const auto texture = imageLoading::LoadRgba8(texturePath);
    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(texture.pixels.size());

    MaterialResource material{};
    material.textureMipLevels = ComputeMipLevelCount(texture.width, texture.height);

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
    imageCreateInfo.mipLevels = material.textureMipLevels;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (VkResult result = vkCreateImage(graphicsBase::Base().Device(), &imageCreateInfo, nullptr, &material.textureImage)) {
        std::cout << std::format("[ main ] ERROR\nFailed to create texture image!\nError code: {}\n", int32_t(result));
        abort();
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(graphicsBase::Base().Device(), material.textureImage, &requirements);
    VkMemoryAllocateInfo allocateInfo = {
        .allocationSize = requirements.size,
        .memoryTypeIndex = FindMemoryTypeIndex(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    };
    material.textureMemory.Create(allocateInfo);
    if (VkResult result = vkBindImageMemory(graphicsBase::Base().Device(), material.textureImage, material.textureMemory, 0)) {
        std::cout << std::format("[ main ] ERROR\nFailed to bind texture image memory!\nError code: {}\n", int32_t(result));
        abort();
    }

    SubmitSingleTimeCommands([&](VkCommandBuffer commandBuffer) {
        CmdTransitionImageLayout(
            commandBuffer,
            material.textureImage,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT,
            0,
            material.textureMipLevels);
        CmdCopyBufferToImage(commandBuffer, stagingBuffer, material.textureImage, texture.width, texture.height);
        CmdGenerateMipmaps(commandBuffer, material.textureImage, VK_FORMAT_R8G8B8A8_SRGB, texture.width, texture.height, material.textureMipLevels);
    });

    VkImageViewCreateInfo imageViewCreateInfo{};
    imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewCreateInfo.image = material.textureImage;
    imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCreateInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
    imageViewCreateInfo.subresourceRange.levelCount = material.textureMipLevels;
    imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
    imageViewCreateInfo.subresourceRange.layerCount = 1;
    if (VkResult result = vkCreateImageView(graphicsBase::Base().Device(), &imageViewCreateInfo, nullptr, &material.textureImageView)) {
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
    samplerCreateInfo.maxLod = static_cast<float>(material.textureMipLevels - 1);
    samplerCreateInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerCreateInfo.unnormalizedCoordinates = VK_FALSE;
    if (VkResult result = vkCreateSampler(graphicsBase::Base().Device(), &samplerCreateInfo, nullptr, &material.textureSampler)) {
        std::cout << std::format("[ main ] ERROR\nFailed to create texture sampler!\nError code: {}\n", int32_t(result));
        abort();
    }

    return material;
}

void DestroyMaterialResources() {
    auto device = graphicsBase::Base().Device();
    for (auto& material : materialResources) {
        if (material.textureSampler)
            vkDestroySampler(device, material.textureSampler, nullptr);
        if (material.textureImageView)
            vkDestroyImageView(device, material.textureImageView, nullptr);
        if (material.textureImage)
            vkDestroyImage(device, material.textureImage, nullptr);
        material.textureSampler = VK_NULL_HANDLE;
        material.textureImageView = VK_NULL_HANDLE;
        material.textureImage = VK_NULL_HANDLE;
    }
    materialResources.clear();
}
void CreateGBufferDescriptorSet() {
    const std::array poolSizes = {
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, static_cast<uint32_t>(materialResources.size()) },
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, static_cast<uint32_t>(materialResources.size()) },
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLER, static_cast<uint32_t>(materialResources.size()) }
    };

    VkDescriptorPoolCreateInfo poolCreateInfo{};
    poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCreateInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolCreateInfo.pPoolSizes = poolSizes.data();
    poolCreateInfo.maxSets = static_cast<uint32_t>(materialResources.size());

    if (VkResult result = vkCreateDescriptorPool(graphicsBase::Base().Device(), &poolCreateInfo, nullptr, &descriptorPool_gbuffer)) {
        std::cout << std::format("[ main ] ERROR\nFailed to create G-buffer descriptor pool!\nError code: {}\n", int32_t(result));
        abort();
    }

    std::vector<VkDescriptorSetLayout> layouts(materialResources.size(), descriptorSetLayout_gbuffer);
    std::vector<VkDescriptorSet> descriptorSets(materialResources.size());

    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = descriptorPool_gbuffer;
    allocateInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
    allocateInfo.pSetLayouts = layouts.data();

    if (VkResult result = vkAllocateDescriptorSets(graphicsBase::Base().Device(), &allocateInfo, descriptorSets.data())) {
        std::cout << std::format("[ main ] ERROR\nFailed to allocate G-buffer descriptor sets!\nError code: {}\n", int32_t(result));
        abort();
    }

    for (size_t i = 0; i < materialResources.size(); ++i) {
        auto& material = materialResources[i];
        material.descriptorSet = descriptorSets[i];

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffer_camera;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        VkDescriptorImageInfo textureInfo{};
        textureInfo.imageView = material.textureImageView;
        textureInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo samplerInfo{};
        samplerInfo.sampler = material.textureSampler;

        std::array<VkWriteDescriptorSet, 3> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = material.descriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo = &bufferInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = material.descriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writes[1].pImageInfo = &textureInfo;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = material.descriptorSet;
        writes[2].dstBinding = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        writes[2].pImageInfo = &samplerInfo;

        vkUpdateDescriptorSets(graphicsBase::Base().Device(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

void CreateShadowDescriptorSet() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolCreateInfo{};
    poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCreateInfo.poolSizeCount = 1;
    poolCreateInfo.pPoolSizes = &poolSize;
    poolCreateInfo.maxSets = 1;

    if (VkResult result = vkCreateDescriptorPool(graphicsBase::Base().Device(), &poolCreateInfo, nullptr, &descriptorPool_shadow)) {
        std::cout << std::format("[ main ] ERROR\nFailed to create shadow descriptor pool!\nError code: {}\n", int32_t(result));
        abort();
    }

    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = descriptorPool_shadow;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &descriptorSetLayout_shadow;

    if (VkResult result = vkAllocateDescriptorSets(graphicsBase::Base().Device(), &allocateInfo, &descriptorSet_shadow)) {
        std::cout << std::format("[ main ] ERROR\nFailed to allocate shadow descriptor set!\nError code: {}\n", int32_t(result));
        abort();
    }

    VkDescriptorBufferInfo shadowMatrixInfo{};
    shadowMatrixInfo.buffer = shadowUniformBuffer;
    shadowMatrixInfo.offset = 0;
    shadowMatrixInfo.range = sizeof(ShadowUniformObject);

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet_shadow;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &shadowMatrixInfo;
    vkUpdateDescriptorSets(graphicsBase::Base().Device(), 1, &write, 0, nullptr);
}

void UpdateLightingDescriptorSet() {
    if (!descriptorSet_lighting)
        return;

    const auto& gbuffer = GBufferPassResources();
    const auto& shadowResources = ShadowRenderPassResources();

    VkDescriptorBufferInfo cameraInfo{};
    cameraInfo.buffer = uniformBuffer_camera;
    cameraInfo.offset = 0;
    cameraInfo.range = sizeof(UniformBufferObject);

    VkDescriptorBufferInfo lightInfo{};
    lightInfo.buffer = lightBuffer;
    lightInfo.offset = 0;
    lightInfo.range = sizeof(Light);

    VkDescriptorBufferInfo shadowMatrixInfo{};
    shadowMatrixInfo.buffer = shadowUniformBuffer;
    shadowMatrixInfo.offset = 0;
    shadowMatrixInfo.range = sizeof(ShadowUniformObject);

    VkDescriptorImageInfo albedoInfo{};
    albedoInfo.imageView = gbuffer.albedo.imageView;
    albedoInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo gbufferSamplerInfo{};
    gbufferSamplerInfo.sampler = gbuffer.sampler;

    VkDescriptorImageInfo normalInfo{};
    normalInfo.imageView = gbuffer.normal.imageView;
    normalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo depthInfo{};
    depthInfo.imageView = gbuffer.depth.imageView;
    depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo depthSamplerInfo{};
    depthSamplerInfo.sampler = shadowResources.shadowSampler;

    VkDescriptorImageInfo shadowInfo{};
    shadowInfo.imageView = shadowResources.depthImageView;
    shadowInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    std::array<VkWriteDescriptorSet, 9> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = descriptorSet_lighting;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &cameraInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = descriptorSet_lighting;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[1].pBufferInfo = &lightInfo;

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = descriptorSet_lighting;
    writes[2].dstBinding = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[2].pBufferInfo = &shadowMatrixInfo;

    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = descriptorSet_lighting;
    writes[3].dstBinding = 3;
    writes[3].descriptorCount = 1;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[3].pImageInfo = &albedoInfo;

    writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet = descriptorSet_lighting;
    writes[4].dstBinding = 4;
    writes[4].descriptorCount = 1;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    writes[4].pImageInfo = &gbufferSamplerInfo;

    writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[5].dstSet = descriptorSet_lighting;
    writes[5].dstBinding = 5;
    writes[5].descriptorCount = 1;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[5].pImageInfo = &normalInfo;

    writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[6].dstSet = descriptorSet_lighting;
    writes[6].dstBinding = 6;
    writes[6].descriptorCount = 1;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[6].pImageInfo = &depthInfo;

    writes[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[7].dstSet = descriptorSet_lighting;
    writes[7].dstBinding = 7;
    writes[7].descriptorCount = 1;
    writes[7].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    writes[7].pImageInfo = &depthSamplerInfo;

    writes[8].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[8].dstSet = descriptorSet_lighting;
    writes[8].dstBinding = 8;
    writes[8].descriptorCount = 1;
    writes[8].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[8].pImageInfo = &shadowInfo;

    vkUpdateDescriptorSets(graphicsBase::Base().Device(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void CreateLightingDescriptorSet() {
    const std::array poolSizes = {
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 },
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4 },
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLER, 2 }
    };

    VkDescriptorPoolCreateInfo poolCreateInfo{};
    poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCreateInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolCreateInfo.pPoolSizes = poolSizes.data();
    poolCreateInfo.maxSets = 1;

    if (VkResult result = vkCreateDescriptorPool(graphicsBase::Base().Device(), &poolCreateInfo, nullptr, &descriptorPool_lighting)) {
        std::cout << std::format("[ main ] ERROR\nFailed to create lighting descriptor pool!\nError code: {}\n", int32_t(result));
        abort();
    }

    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = descriptorPool_lighting;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &descriptorSetLayout_lighting;

    if (VkResult result = vkAllocateDescriptorSets(graphicsBase::Base().Device(), &allocateInfo, &descriptorSet_lighting)) {
        std::cout << std::format("[ main ] ERROR\nFailed to allocate lighting descriptor set!\nError code: {}\n", int32_t(result));
        abort();
    }

    UpdateLightingDescriptorSet();
    graphicsBase::Base().AddCallback_CreateSwapchain(UpdateLightingDescriptorSet);
}

glm::mat4 BuildModelMatrix(const RenderObject& object, float time) {
    glm::mat4 translation = glm::translate(glm::mat4(1.0f), object.position);
    glm::mat4 rotation = glm::rotate(
        glm::mat4(1.0f),
        object.rotationOffset + time * object.rotationSpeed,
        object.rotationAxis);
    glm::mat4 scale = glm::scale(glm::mat4(1.0f), object.scale);
    return translation * rotation * scale;
}

glm::mat4 BuildLightViewProjection() {
    constexpr glm::vec3 sceneCenter = { 0.0f, 0.0f, 0.0f };
    constexpr float sceneExtent = 18.0f;
    constexpr float lightDistance = 24.0f;

    const glm::vec3 lightDirection = glm::normalize(lightData.dir);
    const glm::vec3 worldUp = std::abs(glm::dot(lightDirection, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.99f
        ? glm::vec3(0.0f, 0.0f, 1.0f)
        : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 lightPosition = sceneCenter - lightDirection * lightDistance;

    glm::mat4 lightView = glm::lookAt(lightPosition, sceneCenter, worldUp);
    glm::mat4 lightProj = glm::ortho(-sceneExtent, sceneExtent, -sceneExtent, sceneExtent, 1.0f, lightDistance * 2.0f);
    lightProj[1][1] *= -1.0f;
    return lightProj * lightView;
}

void UpdateUniformBuffer() {
    UniformBufferObject ubo{};

    ubo.view = camera_main.View();
    ubo.proj = camera_main.Projection(windowSize);
    ubo.viewInv = glm::inverse(ubo.view);
    ubo.projInv = glm::inverse(ubo.proj);
    ubo.cameraPosition = camera_main.position;

    uniformMemory_camera.Write(&ubo, sizeof(ubo));

    ShadowUniformObject shadowUbo{};
    shadowUbo.lightViewProj = BuildLightViewProjection();
    shadowUniformMemory.Write(&shadowUbo, sizeof(shadowUbo));
}

void CreateGBufferPipeline() {
    static shaderModule vs("shaders/gbuffer.vs.spv");
    static shaderModule ps("shaders/gbuffer.ps.spv");
    static VkPipelineShaderStageCreateInfo shaderStageCreateInfos_gbuffer[2] = {
        vs.StageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT),
        ps.StageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT)
    };

    auto Create = [] {
        graphicsPipelineCreateInfoPack pipelineCiPack;
        pipelineCiPack.createInfo.layout = pipelineLayout_gbuffer;
        pipelineCiPack.createInfo.renderPass = GBufferPassResources().renderPass;
        pipelineCiPack.inputAssemblyStateCi.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        pipelineCiPack.rasterizationStateCi.polygonMode = VK_POLYGON_MODE_FILL;
        pipelineCiPack.rasterizationStateCi.cullMode = VK_CULL_MODE_BACK_BIT;
        pipelineCiPack.rasterizationStateCi.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        pipelineCiPack.rasterizationStateCi.lineWidth = 1.0f;

        // binding 0 是整条顶点流；
        // location 0/1/2 分别对应 position / normal / uv。
        pipelineCiPack.vertexInputBindings.push_back({
            .binding = 0,
            .stride = sizeof(modelLoading::Vertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
        });
        pipelineCiPack.vertexInputAttributes.push_back({
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(modelLoading::Vertex, position)
        });
        pipelineCiPack.vertexInputAttributes.push_back({
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(modelLoading::Vertex, normal)
        });
        pipelineCiPack.vertexInputAttributes.push_back({
            .location = 2,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(modelLoading::Vertex, uv)
        });

        const auto& gbuffer = GBufferPassResources();
        pipelineCiPack.viewports.emplace_back(0.f, 0.f, float(gbuffer.extent.width), float(gbuffer.extent.height), 0.f, 1.f);
        pipelineCiPack.scissors.emplace_back(VkOffset2D{}, gbuffer.extent);
        pipelineCiPack.multisampleStateCi.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        pipelineCiPack.depthStencilStateCi.depthTestEnable = VK_TRUE;
        pipelineCiPack.depthStencilStateCi.depthWriteEnable = VK_TRUE;
        pipelineCiPack.depthStencilStateCi.depthCompareOp = VK_COMPARE_OP_LESS;
        pipelineCiPack.colorBlendAttachmentStates.push_back({ .colorWriteMask = 0b1111 });
        pipelineCiPack.colorBlendAttachmentStates.push_back({ .colorWriteMask = 0b1111 });
        pipelineCiPack.UpdateAllArrays();
        pipelineCiPack.createInfo.stageCount = 2;
        pipelineCiPack.createInfo.pStages = shaderStageCreateInfos_gbuffer;
        pipeline_gbuffer.Create(pipelineCiPack);
    };

    auto Destroy = [] {
        pipeline_gbuffer.~pipeline();
    };

    graphicsBase::Base().AddCallback_CreateSwapchain(Create);
    graphicsBase::Base().AddCallback_DestroySwapchain(Destroy);
    Create();
}

void CreateShadowPipeline() {
    static shaderModule vs("shaders/shadow.vs.spv");
    static VkPipelineShaderStageCreateInfo shaderStageCreateInfos_shadow[] = {
        vs.StageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT)
    };

    graphicsPipelineCreateInfoPack pipelineCiPack;
    pipelineCiPack.createInfo.layout = pipelineLayout_shadow;
    pipelineCiPack.createInfo.renderPass = ShadowRenderPassResources().shadowPass;
    pipelineCiPack.inputAssemblyStateCi.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipelineCiPack.rasterizationStateCi.polygonMode = VK_POLYGON_MODE_FILL;
    pipelineCiPack.rasterizationStateCi.cullMode = VK_CULL_MODE_FRONT_BIT;
    pipelineCiPack.rasterizationStateCi.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    pipelineCiPack.rasterizationStateCi.lineWidth = 1.0f;

    pipelineCiPack.vertexInputBindings.push_back({
        .binding = 0,
        .stride = sizeof(modelLoading::Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    });
    pipelineCiPack.vertexInputAttributes.push_back({
        .location = 0,
        .binding = 0,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = offsetof(modelLoading::Vertex, position)
    });
    pipelineCiPack.vertexInputAttributes.push_back({
        .location = 1,
        .binding = 0,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = offsetof(modelLoading::Vertex, normal)
    });
    pipelineCiPack.vertexInputAttributes.push_back({
        .location = 2,
        .binding = 0,
        .format = VK_FORMAT_R32G32_SFLOAT,
        .offset = offsetof(modelLoading::Vertex, uv)
    });

    const auto& shadowResources = ShadowRenderPassResources();
    pipelineCiPack.viewports.emplace_back(0.f, 0.f, float(shadowResources.extent.width), float(shadowResources.extent.height), 0.f, 1.f);
    pipelineCiPack.scissors.emplace_back(VkOffset2D{}, shadowResources.extent);
    pipelineCiPack.multisampleStateCi.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    pipelineCiPack.depthStencilStateCi.depthTestEnable = VK_TRUE;
    pipelineCiPack.depthStencilStateCi.depthWriteEnable = VK_TRUE;
    pipelineCiPack.depthStencilStateCi.depthCompareOp = VK_COMPARE_OP_LESS;
    pipelineCiPack.UpdateAllArrays();
    pipelineCiPack.createInfo.stageCount = 1;
    pipelineCiPack.createInfo.pStages = shaderStageCreateInfos_shadow;
    pipeline_shadow.Create(pipelineCiPack);
}

void CreateLightingPipeline() {
    static shaderModule vs("shaders/lighting.vs.spv");
    static shaderModule ps("shaders/lighting.ps.spv");
    static VkPipelineShaderStageCreateInfo shaderStageCreateInfos_lighting[2] = {
        vs.StageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT),
        ps.StageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT)
    };

    auto Create = [] {
        graphicsPipelineCreateInfoPack pipelineCiPack;
        pipelineCiPack.createInfo.layout = pipelineLayout_lighting;
        pipelineCiPack.createInfo.renderPass = RenderPassAndFramebuffers().renderPass;
        pipelineCiPack.inputAssemblyStateCi.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        pipelineCiPack.rasterizationStateCi.polygonMode = VK_POLYGON_MODE_FILL;
        pipelineCiPack.rasterizationStateCi.cullMode = VK_CULL_MODE_NONE;
        pipelineCiPack.rasterizationStateCi.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        pipelineCiPack.rasterizationStateCi.lineWidth = 1.0f;
        pipelineCiPack.viewports.emplace_back(0.f, 0.f, float(windowSize.width), float(windowSize.height), 0.f, 1.f);
        pipelineCiPack.scissors.emplace_back(VkOffset2D{}, windowSize);
        pipelineCiPack.multisampleStateCi.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        pipelineCiPack.depthStencilStateCi.depthTestEnable = VK_FALSE;
        pipelineCiPack.depthStencilStateCi.depthWriteEnable = VK_FALSE;
        pipelineCiPack.colorBlendAttachmentStates.push_back({ .colorWriteMask = 0b1111 });
        pipelineCiPack.UpdateAllArrays();
        pipelineCiPack.createInfo.stageCount = 2;
        pipelineCiPack.createInfo.pStages = shaderStageCreateInfos_lighting;
        pipeline_lighting.Create(pipelineCiPack);
    };

    auto Destroy = [] {
        pipeline_lighting.~pipeline();
    };

    graphicsBase::Base().AddCallback_CreateSwapchain(Create);
    graphicsBase::Base().AddCallback_DestroySwapchain(Destroy);
    Create();
}

void DestroyDescriptors() {
    auto device = graphicsBase::Base().Device();
    if (descriptorPool_gbuffer)
        vkDestroyDescriptorPool(device, descriptorPool_gbuffer, nullptr);
    if (descriptorPool_shadow)
        vkDestroyDescriptorPool(device, descriptorPool_shadow, nullptr);
    if (descriptorPool_lighting)
        vkDestroyDescriptorPool(device, descriptorPool_lighting, nullptr);
    if (descriptorSetLayout_gbuffer)
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout_gbuffer, nullptr);
    if (descriptorSetLayout_shadow)
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout_shadow, nullptr);
    if (descriptorSetLayout_lighting)
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout_lighting, nullptr);
    descriptorPool_gbuffer = VK_NULL_HANDLE;
    descriptorPool_shadow = VK_NULL_HANDLE;
    descriptorPool_lighting = VK_NULL_HANDLE;
    descriptorSetLayout_gbuffer = VK_NULL_HANDLE;
    descriptorSetLayout_shadow = VK_NULL_HANDLE;
    descriptorSetLayout_lighting = VK_NULL_HANDLE;
    descriptorSet_shadow = VK_NULL_HANDLE;
    descriptorSet_lighting = VK_NULL_HANDLE;
}

int main() {
    if (!InitializeWindow({ 1280, 720 }))
        return -1;

    const auto& rpwf = RenderPassAndFramebuffers();
    const auto& shadowRpwf = ShadowRenderPassResources();
    const auto& gbufferRpwf = GBufferPassResources();
    const auto& renderPass = rpwf.renderPass;
    const auto& framebuffers = rpwf.framebuffers;
    const auto& shadowRenderPass = shadowRpwf.shadowPass;
    const auto& gbufferRenderPass = gbufferRpwf.renderPass;
    CreateGBufferDescriptorSetLayout();
    CreateShadowDescriptorSetLayout();
    CreateLightingDescriptorSetLayout();
    CreateGBufferLayout();
    CreateShadowLayout();
    CreateLightingLayout();
    const auto meshPaths = DiscoverMeshPaths();
    meshResources.clear();
    meshResources.reserve(meshPaths.size() + 1);
    for (const auto& meshPath : meshPaths) {
        const auto mesh = modelLoading::LoadObj(meshPath);
        meshResources.push_back(CreateMeshResource(mesh));
    }

    const uint32_t modelMeshCount = static_cast<uint32_t>(meshResources.size());
    const uint32_t groundMeshIndex = modelMeshCount;
    meshResources.push_back(CreateMeshResource(terrain::CreatePlaneMesh(30.0f)));

    const auto materialPaths = DiscoverMaterialPaths();
    materialResources.clear();
    materialResources.reserve(materialPaths.size());
    for (const auto& materialPath : materialPaths)
        materialResources.push_back(CreateMaterialResource(materialPath));
    renderObjects = CreateDefaultRenderObjects(modelMeshCount, static_cast<uint32_t>(materialResources.size()), groundMeshIndex);
    CreateUniformResources();
    CreateLightResources();
    CreateShadowUniformResources();
    CreateGBufferDescriptorSet();
    CreateShadowDescriptorSet();
    CreateLightingDescriptorSet();
    CreateGBufferPipeline();
    CreateShadowPipeline();
    CreateLightingPipeline();
    camera_main.AttachToWindow(pWindow);

    // 先创建成 signaled，这样第一帧开头的 WaitAndReset 不会阻塞。
    fence fence(VK_FENCE_CREATE_SIGNALED_BIT);
    semaphore semaphore_imageIsAvailable;
    semaphore semaphore_renderingIsOver;

    commandBuffer commandBuffer;
    commandPool commandPool(graphicsBase::Base().QueueFamilyIndex_Graphics(), VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandPool.AllocateBuffers(commandBuffer);

    VkClearValue screenClearValues[] = {
        { .color = { 0.f, 0.f, 0.f, 1.f } },
        { .depthStencil = { 1.0f, 0 } }
    };
    VkClearValue gbufferClearValues[] = {
        { .color = { 0.f, 0.f, 0.f, 1.f } },
        { .color = { 0.f, 0.f, 0.f, 1.f } },
        { .depthStencil = { 1.0f, 0 } }
    };
    VkClearValue shadowClearValue = { .depthStencil = { 1.0f, 0 } };

    static const auto sceneStartTime = std::chrono::high_resolution_clock::now();

    while (!glfwWindowShouldClose(pWindow)) {
        while (glfwGetWindowAttrib(pWindow, GLFW_ICONIFIED))
            glfwWaitEvents();

        static auto lastFrameTime = std::chrono::high_resolution_clock::now();
        const auto now = std::chrono::high_resolution_clock::now();
        const float deltaTime = std::chrono::duration<float>(now - lastFrameTime).count();
        const float sceneTime = std::chrono::duration<float>(now - sceneStartTime).count();
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
        shadowRenderPass.CmdBegin(commandBuffer, shadowRpwf.shadowFramebuffer, { {}, shadowRpwf.extent }, shadowClearValue);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_shadow);
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout_shadow,
            0,
            1,
            &descriptorSet_shadow,
            0,
            nullptr);

        for (const auto& renderObject : renderObjects) {
            if (renderObject.meshIndex >= meshResources.size()) {
                std::cout << std::format("[ main ] ERROR\nInvalid mesh index: {}\n", renderObject.meshIndex);
                abort();
            }

            const auto& mesh = meshResources[renderObject.meshIndex];
            VkBuffer vertexBuffers[] = { mesh.vertexBuffer };
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(commandBuffer, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

            ShadowPushConstantObject shadowPushConstant{
                .model = BuildModelMatrix(renderObject, sceneTime)
            };
            vkCmdPushConstants(
                commandBuffer,
                pipelineLayout_shadow,
                VK_SHADER_STAGE_VERTEX_BIT,
                0,
                sizeof(shadowPushConstant),
                &shadowPushConstant);
            vkCmdDrawIndexed(commandBuffer, mesh.indexCount, 1, 0, 0, 0);
        }

        shadowRenderPass.CmdEnd(commandBuffer);
        CmdPrepareShadowMapForSampling(commandBuffer, shadowRpwf.depthImage);

        gbufferRenderPass.CmdBegin(commandBuffer, gbufferRpwf.framebuffer, { {}, gbufferRpwf.extent }, gbufferClearValues);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_gbuffer);
        for (const auto& renderObject : renderObjects) {
            if (renderObject.meshIndex >= meshResources.size()) {
                std::cout << std::format("[ main ] ERROR\nInvalid mesh index: {}\n", renderObject.meshIndex);
                abort();
            }
            if (renderObject.materialIndex >= materialResources.size()) {
                std::cout << std::format("[ main ] ERROR\nInvalid material index: {}\n", renderObject.materialIndex);
                abort();
            }

            const auto& mesh = meshResources[renderObject.meshIndex];
            const auto& material = materialResources[renderObject.materialIndex];
            vkCmdBindDescriptorSets(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelineLayout_gbuffer,
                0,
                1,
                &material.descriptorSet,
                0,
                nullptr);

            VkBuffer vertexBuffers[] = { mesh.vertexBuffer };
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(commandBuffer, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

            PushConstantObject pushConstant{
                .model = BuildModelMatrix(renderObject, sceneTime),
                .meshInfo = glm::vec4(mesh.hasTexcoord ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f)
            };
            vkCmdPushConstants(
                commandBuffer,
                pipelineLayout_gbuffer,
                VK_SHADER_STAGE_VERTEX_BIT,
                0,
                sizeof(pushConstant),
                &pushConstant);
            vkCmdDrawIndexed(commandBuffer, mesh.indexCount, 1, 0, 0, 0);
        }

        gbufferRenderPass.CmdEnd(commandBuffer);
        CmdPrepareGBufferForSampling(commandBuffer);

        renderPass.CmdBegin(commandBuffer, framebuffers[i], { {}, windowSize }, screenClearValues);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_lighting);
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout_lighting,
            0,
            1,
            &descriptorSet_lighting,
            0,
            nullptr);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        renderPass.CmdEnd(commandBuffer);
        commandBuffer.End();

        graphicsBase::Base().SubmitCommandBuffer_Graphics(commandBuffer, semaphore_imageIsAvailable, semaphore_renderingIsOver, fence);
        graphicsBase::Base().PresentImage(semaphore_renderingIsOver);

        TitleFps();
    }

    vkDeviceWaitIdle(graphicsBase::Base().Device());
    pipeline_shadow.~pipeline();
    pipeline_gbuffer.~pipeline();
    pipeline_lighting.~pipeline();
    DestroyDescriptors();
    DestroyMaterialResources();
    DestroyMeshResources();
    TerminateWindow();
    return 0;
}






















