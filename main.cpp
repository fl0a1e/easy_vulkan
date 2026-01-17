#include "GlfwGeneral.hpp"
#include "easyVk.hpp"

using namespace vulkan;

pipelineLayout pipelineLayout_triangle; //管线布局
pipeline pipeline_triangle;             //管线

//该函数调用easyVulkan::CreateRpwf_Screen()并存储返回的引用到静态变量
const auto& RenderPassAndFramebuffers() {
    static const auto& rpwf = easyVulkan::CreateRpwf_Screen(); // static只在第一次调用被赋值
    return rpwf;
}

void CreateLayout() {
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
    pipelineLayout_triangle.Create(pipelineLayoutCreateInfo);
}

void CreatePipeline() {
    static shaderModule vs("shaders/triangle.vs.spv");
    static shaderModule ps("shaders/triangle.ps.spv");
    static VkPipelineShaderStageCreateInfo shaderStageCreateInfos_triangle[2] = {
        vs.StageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT),
        ps.StageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT)
    };
    auto Create = [] {
        graphicsPipelineCreateInfoPack pipelineCiPack;
        pipelineCiPack.createInfo.layout = pipelineLayout_triangle;
        pipelineCiPack.createInfo.renderPass = RenderPassAndFramebuffers().renderPass;
        pipelineCiPack.inputAssemblyStateCi.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
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
        pipeline_triangle.~pipeline();
        };
    graphicsBase::Base().AddCallback_CreateSwapchain(Create);
    graphicsBase::Base().AddCallback_DestroySwapchain(Destroy);
    //调用Create()以创建管线
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


        //获取交换链图像索引
        graphicsBase::Base().SwapImage(semaphore_imageIsAvailable);

        //因为framebuffer与所获取的交换链图像一一对应，获取交换链图像索引
        /*新增*/ auto i = graphicsBase::Base().CurrentImageIndex();
        
        //开始录制命令缓冲区
        commandBuffer.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        /*开始渲染通道*/ renderPass.CmdBegin(commandBuffer, framebuffers[i], { {}, windowSize }, clearColor);
        /*渲染命令*/vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_triangle);
        /*渲染命令*/vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        /*结束渲染通道*/ renderPass.CmdEnd(commandBuffer);
        commandBuffer.End();

        /*提交命令缓冲区*/
        graphicsBase::Base().SubmitCommandBuffer_Graphics(commandBuffer, semaphore_imageIsAvailable, semaphore_renderingIsOver, fence);
        /*呈现图像，待后续填充*/
        graphicsBase::Base().PresentImage(semaphore_renderingIsOver);

        glfwPollEvents();
        TitleFps();

        //等待并重置fence，放最后是为了第一帧能进来
        fence.WaitAndReset();
    }
    TerminateWindow();
    return 0;
}