#include "GlfwGeneral.hpp"
#include "easyVk.hpp"
#include "Camera.hpp"

using namespace vulkan;

// 一个顶点包含位置和颜色两部分属性。
// pipeline 会根据这个结构体的内存布局，告诉 shader 去哪里读 Position/Color。
struct Vertex {
    glm::vec3 position;
    glm::vec3 color;
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

pipelineLayout pipelineLayout_cube; // 立方体管线布局
pipeline pipeline_cube;             // 立方体管线
buffer vertexBuffer_cube;           // 顶点缓冲
buffer indexBuffer_cube;            // 索引缓冲
buffer uniformBuffer_cube;          // uniform 缓冲

deviceMemory vertexMemory_cube;     // 顶点缓冲绑定的显存
deviceMemory indexMemory_cube;      // 索引缓冲绑定的显存
deviceMemory uniformMemory_cube;    // uniform 缓冲绑定的显存

VkDescriptorSetLayout descriptorSetLayout_cube = VK_NULL_HANDLE;
VkDescriptorPool descriptorPool_cube = VK_NULL_HANDLE;
VkDescriptorSet descriptorSet_cube = VK_NULL_HANDLE;

camera camera_main;               // 主相机，只负责生成 view/proj

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

void CreateDescriptorSetLayout() {
    // 这里声明一个最小的 descriptor set layout：
    // set 0 / binding 0 放一个给 VS 使用的 uniform buffer。
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    createInfo.bindingCount = 1;
    createInfo.pBindings = &uboBinding;

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

void CreateDescriptorSet() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolCreateInfo{};
    poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCreateInfo.poolSizeCount = 1;
    poolCreateInfo.pPoolSizes = &poolSize;
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

    // 这一步把 uniform buffer 填进 descriptor set：
    // shader 里 set 0 / binding 0 读到的，就是这块 buffer。
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet_cube;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &bufferInfo;
    vkUpdateDescriptorSets(graphicsBase::Base().Device(), 1, &write, 0, nullptr);
}

void UpdateUniformBuffer() {
    static const auto startTime = std::chrono::high_resolution_clock::now();
    const auto now = std::chrono::high_resolution_clock::now();
    const float time = std::chrono::duration<float>(now - startTime).count();

    UniformBufferObject ubo{};
    ubo.model = glm::rotate(glm::mat4(1.0f), glm::radians(60.0f), glm::vec3(0.4f, 1.0f, 0.2f));

    // 相机模块只负责观察和投影：
    // view 表示“相机从哪里看”，proj 表示“怎么把 3D 压到屏幕上”。
    ubo.view = camera_main.View();
    ubo.proj = camera_main.Projection(windowSize);

    uniformMemory_cube.Write(&ubo, sizeof(ubo));
}

void CreatePipeline() {
    // shader 现在不再依赖 SV_VertexID，而是从顶点缓冲读取位置和颜色。
    // 顶点位置会先经过 model/view/proj，再输出到裁剪空间。
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

        // 把 CPU 侧的 Vertex 结构告诉 Vulkan：
        // binding 0，
        // 每次前进一个 Vertex，
        // location 0 是位置，location 1 是颜色。
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

        pipelineCiPack.viewports.emplace_back(0.f, 0.f, float(windowSize.width), float(windowSize.height), 0.f, 1.f);
        pipelineCiPack.scissors.emplace_back(VkOffset2D{}, windowSize);
        pipelineCiPack.multisampleStateCi.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        // depth test 打开后，只有更靠近摄像机的片元才能覆盖已有结果。
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
    TerminateWindow();
    return 0;
}