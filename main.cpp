#include "GlfwGeneral.hpp"


int main() {
    if (!InitializeWindow({ 1280, 720 }))
        return -1;


    fence fence(VK_FENCE_CREATE_SIGNALED_BIT); //以置位状态创建栅栏
    semaphore semaphore_imageIsAvailable;
    semaphore semaphore_renderingIsOver;

    commandBuffer commandBuffer;
    commandPool commandPool(graphicsBase::Base().QueueFamilyIndex_Graphics(), VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandPool.AllocateBuffers(commandBuffer);


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
        
        //开始录制命令缓冲区
        commandBuffer.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        /*渲染命令，待填充*/
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