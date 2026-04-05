#include "GlfwGeneral.hpp"
#include "easyVk.hpp"

using namespace vulkan;

// 这个结构既影响 CPU 侧数据布局，也影响 pipeline 里的顶点输入描述
struct Vertex {
    glm::vec3 position;
    glm::vec3 color;
};

pipelineLayout pipelineLayout_cube; // 立方体管线布局
pipeline pipeline_cube;             // 立方体管线
buffer vertexBuffer_cube;           // 顶点缓冲
buffer indexBuffer_cube;            // 索引缓冲
deviceMemory vertexMemory_cube;     // 顶点缓冲绑定的显存
deviceMemory indexMemory_cube;      // 索引缓冲绑定的显存

const std::array<Vertex, 8> cubeVertices = {
    Vertex{ {-0.5f, -0.5f, -0.5f}, {1.f, 0.2f, 0.2f} },
    Vertex{ { 0.5f, -0.5f, -0.5f}, {0.2f, 1.f, 0.2f} },
    Vertex{ { 0.5f,  0.5f, -0.5f}, {0.2f, 0.6f, 1.f} },
    Vertex{ {-0.5f,  0.5f, -0.5f}, {1.f, 0.8f, 0.2f} },
    Vertex{ {-0.5f, -0.5f,  0.5f}, {1.f, 0.2f, 1.f} },
    Vertex{ { 0.5f, -0.5f,  0.5f}, {0.2f, 1.f, 1.f} },
    Vertex{ { 0.5f,  0.5f,  0.5f}, {1.f, 1.f, 1.f} },
    Vertex{ {-0.5f,  0.5f,  0.5f}, {0.3f, 0.3f, 0.3f} }
};

const std::array<uint16_t, 36> cubeIndices = {
    0, 1, 2, 0, 2, 3,
    4, 6, 5, 4, 7, 6,
    0, 3, 7, 0, 7, 4,
    1, 5, 6, 1, 6, 2,
    3, 2, 6, 3, 6, 7,
    0, 4, 5, 0, 5, 1
};

// 找合适的内存类型
// Vulkan 告诉你“这类 buffer 可以绑定哪些类型的内存”，再从物理设备支持的内存类型里，选一个满足要求的
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

// 这个函数把渲染到屏幕所需的 render pass + framebuffer 集合缓存起来。
// 做成静态局部变量，是因为这套对象全局只需要一份，后续直接复用即可。
const auto& RenderPassAndFramebuffers() {
    static const auto& rpwf = easyVulkan::CreateRpwf_Screen();
    return rpwf;
}

void CreateLayout() {
    // 当前示例没有 descriptor set 和 push constant，
    // 所以这里创建的是一个空的 pipeline layout。
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
    pipelineLayout_cube.Create(pipelineLayoutCreateInfo);
}

// CreateGeometry()：把立方体数据真正送进 GPU
void CreateGeometry() {
    auto CreateUploadBuffer = [](buffer& gpuBuffer, deviceMemory& gpuMemory, VkBufferUsageFlags usage, const void* pData, size_t size) {
        // 这个示例先走最容易理解的路径：
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
        gpuBuffer.BindMemory(gpuMemory); //绑定 buffer 和 memory
        gpuMemory.Write(pData, size); // gpu地址映射+memcpy拷贝数据
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

void CreatePipeline() {
    // shader 现在不再依赖 SV_VertexID，而是从顶点缓冲读取位置和颜色。
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
        pipelineCiPack.rasterizationStateCi.polygonMode = VK_POLYGON_MODE_LINE; // polygonMode

        // 把 CPU 侧的 Vertex 结构告诉 Vulkan：
        // binding 0，
        // 每次前进一个 Vertex，
        // location 0 是位置，location 1 是颜色。
        pipelineCiPack.vertexInputBindings.push_back({
            .binding = 0,
            .stride = sizeof(Vertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
        });
        // 这里的 location 要和 shader 对上
        pipelineCiPack.vertexInputAttributes.push_back({
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(Vertex, position)
        });
        // 这里的 location 要和 shader 对上
        pipelineCiPack.vertexInputAttributes.push_back({
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(Vertex, color)
        });

        pipelineCiPack.viewports.emplace_back(0.f, 0.f, float(windowSize.width), float(windowSize.height), 0.f, 1.f);
        pipelineCiPack.scissors.emplace_back(VkOffset2D{}, windowSize);
        pipelineCiPack.multisampleStateCi.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
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

int main() {
    if (!InitializeWindow({ 1280, 720 }))
        return -1;

    const auto& [renderPass, framebuffers] = RenderPassAndFramebuffers();
    CreateLayout();
    CreateGeometry();
    CreatePipeline();

    fence fence(VK_FENCE_CREATE_SIGNALED_BIT);
    semaphore semaphore_imageIsAvailable;
    semaphore semaphore_renderingIsOver;

    commandBuffer commandBuffer;
    commandPool commandPool(graphicsBase::Base().QueueFamilyIndex_Graphics(), VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandPool.AllocateBuffers(commandBuffer);

    VkClearValue clearColor = { .color = { 0.2f, 0.2f, 0.2f, 1.f } };

    while (!glfwWindowShouldClose(pWindow)) {
        while (glfwGetWindowAttrib(pWindow, GLFW_ICONIFIED))
            glfwWaitEvents();

        graphicsBase::Base().SwapImage(semaphore_imageIsAvailable);
        auto i = graphicsBase::Base().CurrentImageIndex();

        commandBuffer.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        renderPass.CmdBegin(commandBuffer, framebuffers[i], { {}, windowSize }, clearColor);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_cube);

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

        glfwPollEvents();
        TitleFps();
        fence.WaitAndReset();
    }

    TerminateWindow();
    return 0;
}