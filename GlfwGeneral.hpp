#include "vkBase.h"
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#pragma comment(lib, "glfw3.lib") //链接编译所需的静态库


GLFWwindow* pWindow; //窗口指针
GLFWmonitor* pMonitor; //显示器信息指针
const char* windowTitle = "EasyVK"; //窗口标题

using namespace vulkan;

// 初始化成功时返回true，否则返回false
// size: 窗口大小
// fullScreen: 指定是否以全屏初始化窗口
// isResizable: 指定窗口是否可拉伸，游戏窗口通常是不可任意拉伸的
// limitFrameRate: 指定是否将帧数限制到不超过屏幕刷新率，在本节先不实现这个参数的作用
bool InitializeWindow(VkExtent2D size, bool fullScreen = false, bool isResizable = true, bool limitFrameRate = false) {

	if(!glfwInit()) {
		std::cout << std::format("[ InitializeWindow ] ERROR\nFailed to initialize GLFW!\n");
		return false;
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, isResizable);
	pMonitor = glfwGetPrimaryMonitor(); // 获取当前显示器信息
	const GLFWvidmode* pMode = glfwGetVideoMode(pMonitor); // 获取当前显示器视频模式
	pWindow = fullScreen ?
		glfwCreateWindow(pMode->width, pMode->height, windowTitle, pMonitor, nullptr) :
		glfwCreateWindow(size.width, size.height, windowTitle, nullptr, nullptr);
	if (!pWindow) {
		std::cout << std::format("[ InitializeWindow ]\nFailed to create a glfw window!\n");
		glfwTerminate();
		return false;
	}

	{
		// 获取vulkan实例相关信息
		// 扩展
		uint32_t extensionCount = 0;
		const char** extensionNames;
		extensionNames = glfwGetRequiredInstanceExtensions(&extensionCount);
		if (!extensionNames) {
			std::cout << std::format("[ InitializeWindow ]\nVulkan is not available on this machine!\n");
			glfwTerminate();
			return false;
		}
		for (size_t i = 0; i < extensionCount; i++) {
			graphicsBase::Base().AddInstanceExtension(extensionNames[i]);
		}
		// 做窗口显示必须启用 swapchain 扩展。
		graphicsBase::Base().AddDeviceExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
		graphicsBase::Base().AddDeviceExtension(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
		graphicsBase::Base().AddDeviceExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
		graphicsBase::Base().AddDeviceExtension(VK_KHR_RAY_QUERY_EXTENSION_NAME);
		graphicsBase::Base().AddDeviceExtension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
		// surface 依赖 instance，因此先创建 Vulkan instance
		graphicsBase::Base().UseLatestApiVersion();
		if (graphicsBase::Base().CreateInstance())
			return false;

		// 让 GLFW 为当前窗口创建 VkSurfaceKHR，供设备选择和交换链创建使用
		VkSurfaceKHR surface = VK_NULL_HANDLE;
		if (VkResult result = glfwCreateWindowSurface(graphicsBase::Base().Instance(), pWindow, nullptr, &surface)) {
			std::cout << std::format("[ InitializeWindow ] ERROR\nFailed to create a window surface!\nError code: {}\n", int32_t(result));
			glfwTerminate();
			return false;
		}
		graphicsBase::Base().Surface(surface);

		// 初始化主链路：枚举物理设备 -> 选择支持 graphics/present 的设备 -> 创建逻辑设备
		if (//获取物理设备，并使用列表中的第一个物理设备，这里不考虑以下任意函数失败后更换物理设备的情况
			graphicsBase::Base().GetPhysicalDevices() ||
			//一个true一个false，暂时不需要计算用的队列,现在要了变true了
			graphicsBase::Base().DeterminePhysicalDevice(0, true, true) ||
			//创建逻辑设备
			graphicsBase::Base().CreateDevice())
			return false;
		//----------------------------------------

		// 交换链负责把渲染结果送到窗口。
		if (graphicsBase::Base().CreateSwapchain(limitFrameRate))
			return false;
	}

	return true;
}

// 终止窗口时，清理GLFW
void TerminateWindow() {
	// 退出前先等待 GPU 空闲，避免资源仍在使用时就被销毁。
	graphicsBase::Base().WaitIdle();
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
	// 每秒更新一次标题，避免每帧都改窗口标题
	if ((dt = time1 - time0) >= 0.05) {
		info.precision(0);
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
