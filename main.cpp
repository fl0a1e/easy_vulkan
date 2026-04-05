#include "GlfwGeneral.hpp"
#include "easyVk.hpp"

using namespace vulkan;

pipelineLayout pipelineLayout_triangle; //管线布局
pipeline pipeline_triangle;             //管线

// 这个函数把渲染到屏幕所需的 render pass + framebuffer 集合缓存起来。
// 做成静态局部变量，是因为这套对象全局只需要一份，后续直接复用即可。
const auto& RenderPassAndFramebuffers() {
    static const auto& rpwf = easyVulkan::CreateRpwf_Screen(); // static只在第一次调用被赋值
    return rpwf;
}

void CreateLayout() {
    // 当前示例没有 descriptor set 和 push constant，
    // 所以这里创建的是一个空的 pipeline layout。
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
    pipelineLayout_triangle.Create(pipelineLayoutCreateInfo);
}

void CreatePipeline() {
    // shaderModule 负责从 .spv 文件加载二进制并创建 VkShaderModule。
    static shaderModule vs("shaders/triangle.vs.spv");
    static shaderModule ps("shaders/triangle.ps.spv");
    static VkPipelineShaderStageCreateInfo shaderStageCreateInfos_triangle[2] = {
        vs.StageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT),
        ps.StageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT)
    };
    auto Create = [] {
        // graphicsPipelineCreateInfoPack 把图形管线相关的大量状态结构体集中管理，
        // 这样上层代码不用手动维护一堆 pNext / count / 指针。
        graphicsPipelineCreateInfoPack pipelineCiPack;
        pipelineCiPack.createInfo.layout = pipelineLayout_triangle;
        pipelineCiPack.createInfo.renderPass = RenderPassAndFramebuffers().renderPass;
        pipelineCiPack.inputAssemblyStateCi.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        // 这里使用固定 viewport/scissor，尺寸直接匹配当前交换链。
        pipelineCiPack.viewports.emplace_back(0.f, 0.f, float(windowSize.width), float(windowSize.height), 0.f, 1.f);
        pipelineCiPack.scissors.emplace_back(VkOffset2D{}, windowSize);
        pipelineCiPack.multisampleStateCi.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        pipelineCiPack.colorBlendAttachmentStates.push_back({ .colorWriteMask = 0b1111 });
        pipelineCiPack.UpdateAllArrays();
        pipelineCiPack.createInfo.stageCount = 2;
        pipelineCiPack.createInfo.pStages = shaderStageCreateInfos_triangle;
        pipeline_triangle.Create(pipelineCiPack);
        };
    auto Destroy = [] {
        // 管线依赖 render pass 和交换链尺寸；交换链重建时需要销毁旧管线。
        pipeline_triangle.~pipeline();
        };
    graphicsBase::Base().AddCallback_CreateSwapchain(Create);
    graphicsBase::Base().AddCallback_DestroySwapchain(Destroy);
    // 首次启动时手动创建一次，之后交给交换链回调在重建时处理
    Create();
}


int main() {
    if (!InitializeWindow({ 1280, 720 }))
        return -1;

    const auto& [renderPass, framebuffers] = RenderPassAndFramebuffers();
    CreateLayout();
    CreatePipeline();

    fence fence(VK_FENCE_CREATE_SIGNALED_BIT); //以置位状态创建栅栏
    semaphore semaphore_imageIsAvailable;
    semaphore semaphore_renderingIsOver;

    commandBuffer commandBuffer;
    // 命令池绑定到 graphics queue family，命令缓冲从这里分配。
    commandPool commandPool(graphicsBase::Base().QueueFamilyIndex_Graphics(), VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandPool.AllocateBuffers(commandBuffer);

    VkClearValue clearColor = { .color = { .5f, 0.5f, 0.5f, 1.f } }; //ClearValue

    //fence在渲染完成后被置位。
    //渲染循环的开头等待栅栏被置位，因此以置位状态创建fence（为了在首次执行渲染循环时能完成等待）。
    //semaphore_imageIsAvailable在取得交换链图像后被置位，在执行命令前等待它。
    //semaphore_renderingIsOver在渲染完成后被置位，在呈现图像前等待它。
    while (!glfwWindowShouldClose(pWindow)) {
        //窗口最小化时，阻塞----------------------------
        while (glfwGetWindowAttrib(pWindow, GLFW_ICONIFIED))
            glfwWaitEvents();
        //----------------------------------------


        // 从交换链拿到这一帧要渲染到的图像，并在 graphicsBase 内部记录 currentImageIndex
        graphicsBase::Base().SwapImage(semaphore_imageIsAvailable);

        //因为framebuffer与所获取的交换链图像一一对应，获取交换链图像索引
        /*新增*/ auto i = graphicsBase::Base().CurrentImageIndex();
        
        // 录制本帧命令：开始 render pass -> 绑定管线 -> 绘制 -> 结束 render pass
        commandBuffer.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        renderPass.CmdBegin(commandBuffer, framebuffers[i], { {}, windowSize }, clearColor);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_triangle);
        // 当前没有顶点缓冲，顶点位置在 vertex shader 中通过 SV_VertexID 生成。
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        renderPass.CmdEnd(commandBuffer);
        commandBuffer.End();

        // 提交到 graphics queue：等待 imageAvailable，渲染结束后 signal renderingIsOver
        graphicsBase::Base().SubmitCommandBuffer_Graphics(commandBuffer, semaphore_imageIsAvailable, semaphore_renderingIsOver, fence);
        // present queue 在呈现前等待 renderingIsOver，确保颜色输出已经完成
        graphicsBase::Base().PresentImage(semaphore_renderingIsOver);

        glfwPollEvents();
        TitleFps();

        // 等待本帧 GPU 工作完成，再复用命令缓冲和同步对象。
        // 放在循环末尾，是为了首帧不会先等待一个还没提交过的 fence。
        fence.WaitAndReset();
    }
    TerminateWindow();
    return 0;
}