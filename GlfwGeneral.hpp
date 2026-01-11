#include "vkBase.h"
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#pragma comment(lib, "glfw3.lib") //链接编译所需的静态库


GLFWwindow* pWindow; //窗口指针
GLFWmonitor* pMonitor; //显示器信息指针
const char* windowTitle = "EasyVK"; //窗口标题


// 初始化成功时返回true，否则返回false
// size: 窗口大小
// fullScreen: 指定是否以全屏初始化窗口
// isResizable: 指定窗口是否可拉伸，游戏窗口通常是不可任意拉伸的
// limitFrameRate: 指定是否将帧数限制到不超过屏幕刷新率，在本节先不实现这个参数的作用
bool InitializeWindow(VkExtent2D size, bool fullScreen = false, bool isResizable = true, bool limitFrameRate = true) {
	if(!glfwInit()) {
		std::cout << std::format("[ InitializeWindow ] ERROR\nFailed to initialize GLFW!\n");
		return false;
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, isResizable);

	{
	// 获取vulkan实例扩展
	uint32_t extensionCount = 0;
	const char** extensionNames;
	extensionNames = glfwGetRequiredInstanceExtensions(&extensionCount);
	if (!extensionNames) {
		std::cout << std::format("[ InitializeWindow ]\nVulkan is not available on this machine!\n");
		glfwTerminate();
		return false;
	}
	for (size_t i = 0; i < extensionCount; i++) {
		vulkan::graphicsBase::Base().AddInstanceExtension(extensionNames[i]);
	}
	vulkan::graphicsBase::Base().AddDeviceExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

	vulkan::graphicsBase::Base().CreateInstance();
	}

	pMonitor = glfwGetPrimaryMonitor(); // 获取当前显示器信息
	const GLFWvidmode* pMode = glfwGetVideoMode(pMonitor); // 获取当前显示器视频模式
	pWindow = fullScreen ? 
		glfwCreateWindow(pMode->width, pMode->height, windowTitle, pMonitor, nullptr):
		glfwCreateWindow(size.width, size.height, windowTitle, nullptr, nullptr);
	if (!pWindow) {
		std::cout << std::format("[ InitializeWindow ]\nFailed to create a glfw window!\n");
		glfwTerminate();
		return false;
	}

	//
	return true;
}

// 终止窗口时，清理GLFW
void TerminateWindow() {
	glfwTerminate();
}

// show fps
void TitleFps() {
	// init
	static double time0 = glfwGetTime();
	static double time1;
	static double dt;
	static int dframe = -1;
	static std::stringstream info;
	// every frame
	time1 = glfwGetTime();
	++dframe;
	// update fps
	if ((dt = time1 - time0) >= 1) {
		info.precision(1);
		info << windowTitle << "    " << std::fixed << dframe / dt << " FPS";
		glfwSetWindowTitle(pWindow, info.str().c_str());
		info.str(""); //别忘了在设置完窗口标题后清空所用的stringstream
		time0 = time1;
		dframe = 0;
	}
}

void MakeWindowFullScreen() {
	const GLFWvidmode* pMode = glfwGetVideoMode(pMonitor);
	glfwSetWindowMonitor(pWindow, pMonitor, 0, 0, pMode->width, pMode->height, pMode->refreshRate);
}

void MakeWindowWindowed(VkOffset2D position, VkExtent2D size) {
	const GLFWvidmode* pMode = glfwGetVideoMode(pMonitor);
	glfwSetWindowMonitor(pWindow, nullptr, position.x, position.y, size.width, size.height, pMode->refreshRate);
}