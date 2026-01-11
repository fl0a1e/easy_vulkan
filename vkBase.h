#pragma once

#include "vkStart.h"

// define vulkan namespace
namespace vulkan {

	// 单例类
	class graphicsBase {
		uint32_t apiVersion = VK_API_VERSION_1_0;

		VkInstance instance; // vulkan实例
		std::vector<const char*> instanceLayers; // vulkan实例层
		std::vector<const char*> instanceExtensions; // vulkan实例扩展

		VkDebugUtilsMessengerEXT debugMessenger; 

		VkSurfaceKHR surface;

		VkPhysicalDevice physicalDevice; // 物理设备
		VkPhysicalDeviceProperties physicalDeviceProperties; // 物理设备属性
		VkPhysicalDeviceMemoryProperties physicalDeviceMemoryProperties; // 物理设备内存属性
		std::vector<VkPhysicalDevice> availablePhysicalDevices; 

		VkDevice device; // 逻辑设备
		//有效的索引从0开始，因此使用特殊值VK_QUEUE_FAMILY_IGNORED（为UINT32_MAX）为队列族索引的默认值
		uint32_t queueFamilyIndex_graphics = VK_QUEUE_FAMILY_IGNORED;
		uint32_t queueFamilyIndex_presentation = VK_QUEUE_FAMILY_IGNORED;
		uint32_t queueFamilyIndex_compute = VK_QUEUE_FAMILY_IGNORED;
		VkQueue queue_graphics; // 图形
		VkQueue queue_presentation; // 呈现
		VkQueue queue_compute; // 计算

		std::vector<const char*> deviceExtensions;

		std::vector <VkSurfaceFormatKHR> availableSurfaceFormats;

		VkSwapchainKHR swapchain; // 交换链
		std::vector <VkImage> swapchainImages; // VkImage=一片设备内存（device memory），将该片内存上的数据用作图像
		std::vector <VkImageView> swapchainImageViews; // VkImageView指定图像的使用方式
		//保存交换链的创建信息以便重建交换链
		VkSwapchainCreateInfoKHR swapchainCreateInfo = {};

		// static
		static graphicsBase singleton; // 只是声明
		// -------------------------
		graphicsBase() = default;
		graphicsBase(graphicsBase&&) = delete; // 不可移动，没有定义复制构造器、复制赋值、移动赋值，四个函数全部无法使用
		~graphicsBase() {};

		//该函数被CreateSwapchain(...)和RecreateSwapchain()调用
		VkResult CreateSwapchain_Internal() {
			/*待Ch1-4填充*/
		}

		//该函数被DeterminePhysicalDevice(...)调用，用于检查物理设备是否满足所需的队列族类型，并将对应的队列族索引返回到queueFamilyIndices，执行成功时直接将索引写入相应成员变量
		VkResult GetQueueFamilyIndices(VkPhysicalDevice physicalDevice, bool enableGraphicsQueue, bool enableComputeQueue, uint32_t(&queueFamilyIndices)[3]) {
			/*待Ch1-3填充*/
		}

		//以下函数用于创建debug messenger
		VkResult CreateDebugMessenger() {
			/*待Ch1-3填充*/
		}

		//该函数用于向instanceLayers或instanceExtensions容器中添加字符串指针，并确保不重复
		static void AddLayerOrExtension(std::vector<const char*>& container, const char* name) {
			for (auto& i : container)
				if (!strcmp(name, i)) //strcmp(...)在字符串匹配时返回0
					return;           //如果层/扩展的名称已在容器中，直接返回
			container.push_back(name);
		}


	public:
		//Getter
		uint32_t ApiVersion() const {
			return apiVersion;
		}
		VkResult UseLatestApiVersion() {
			/*待Ch1-3填充*/
		}

		VkInstance Instance() const {
			return instance;
		}

		const std::vector<const char*>& InstanceLayers() const {
			return instanceLayers;
		}

		const std::vector<const char*>& InstanceExtensions() const {
			return instanceExtensions;
		}

		VkSurfaceKHR Surface() const {
			return surface;
		}

		//该函数用于选择物理设备前
		void Surface(VkSurfaceKHR surface) {
			if (!this->surface)
				this->surface = surface;
		}

		VkPhysicalDevice PhysicalDevice() const {
			return physicalDevice;
		}
		const VkPhysicalDeviceProperties& PhysicalDeviceProperties() const {
			return physicalDeviceProperties;
		}
		const VkPhysicalDeviceMemoryProperties& PhysicalDeviceMemoryProperties() const {
			return physicalDeviceMemoryProperties;
		}
		VkPhysicalDevice AvailablePhysicalDevice(uint32_t index) const {
			return availablePhysicalDevices[index];
		}
		uint32_t AvailablePhysicalDeviceCount() const {
			return uint32_t(availablePhysicalDevices.size());
		}

		VkDevice Device() const {
			return device;
		}
		uint32_t QueueFamilyIndex_Graphics() const {
			return queueFamilyIndex_graphics;
		}
		uint32_t QueueFamilyIndex_Presentation() const {
			return queueFamilyIndex_presentation;
		}
		uint32_t QueueFamilyIndex_Compute() const {
			return queueFamilyIndex_compute;
		}
		VkQueue Queue_Graphics() const {
			return queue_graphics;
		}
		VkQueue Queue_Presentation() const {
			return queue_presentation;
		}
		VkQueue Queue_Compute() const {
			return queue_compute;
		}

		const std::vector<const char*>& DeviceExtensions() const {
			return deviceExtensions;
		}

		const VkFormat& AvailableSurfaceFormat(uint32_t index) const {
			return availableSurfaceFormats[index].format;
		}
		const VkColorSpaceKHR& AvailableSurfaceColorSpace(uint32_t index) const {
			return availableSurfaceFormats[index].colorSpace;
		}
		uint32_t AvailableSurfaceFormatCount() const {
			return uint32_t(availableSurfaceFormats.size());
		}

		VkSwapchainKHR Swapchain() const {
			return swapchain;
		}
		VkImage SwapchainImage(uint32_t index) const {
			return swapchainImages[index];
		}
		VkImageView SwapchainImageView(uint32_t index) const {
			return swapchainImageViews[index];
		}
		uint32_t SwapchainImageCount() const {
			return uint32_t(swapchainImages.size());
		}
		const VkSwapchainCreateInfoKHR& SwapchainCreateInfo() const {
			return swapchainCreateInfo;
		}

		VkResult GetSurfaceFormats() {
			/*待Ch1-4填充*/
		}
		VkResult SetSurfaceFormat(VkSurfaceFormatKHR surfaceFormat) {
			/*待Ch1-4填充*/
		}
		//该函数用于创建交换链
		VkResult CreateSwapchain(bool limitFrameRate = true, VkSwapchainCreateFlagsKHR flags = 0) {
			/*待Ch1-4填充*/
		}
		//该函数用于重建交换链
		VkResult RecreateSwapchain() {
			/*待Ch1-4填充*/
		}


		//以下函数用于创建Vulkan实例前
		void AddInstanceLayer(const char* layerName) {
			AddLayerOrExtension(instanceLayers, layerName);
		}
		void AddInstanceExtension(const char* extensionName) {
			AddLayerOrExtension(instanceExtensions, extensionName);
		}
		//该函数用于创建Vulkan实例
		VkResult CreateInstance(VkInstanceCreateFlags flags = 0) {
			/*待Ch1-3填充*/
		}
		//以下函数用于创建Vulkan实例失败后
		VkResult CheckInstanceLayers(std::span<const char*> layersToCheck) {
			/*待Ch1-3填充*/
		}
		void InstanceLayers(const std::vector<const char*>& layerNames) {
			instanceLayers = layerNames;
		}
		VkResult CheckInstanceExtensions(std::span<const char*> extensionsToCheck, const char* layerName = nullptr) const {
			/*待Ch1-3填充*/
		}
		void InstanceExtensions(const std::vector<const char*>& extensionNames) {
			instanceExtensions = extensionNames;
		}

		//该函数用于创建逻辑设备前
		void AddDeviceExtension(const char* extensionName) {
			AddLayerOrExtension(deviceExtensions, extensionName);
		}
		//该函数用于获取物理设备
		VkResult GetPhysicalDevices() {
			/*待Ch1-3填充*/
		}
		//该函数用于指定所用物理设备并调用GetQueueFamilyIndices(...)取得队列族索引
		VkResult DeterminePhysicalDevice(uint32_t deviceIndex = 0, bool enableGraphicsQueue, bool enableComputeQueue = true) {
			/*待Ch1-3填充*/
		}
		//该函数用于创建逻辑设备，并取得队列
		VkResult CreateDevice(VkDeviceCreateFlags flags = 0) {
			/*待Ch1-3填充*/
		}
		//以下函数用于创建逻辑设备失败后
		VkResult CheckDeviceExtensions(std::span<const char*> extensionsToCheck, const char* layerName = nullptr) const {
			/*待Ch1-3填充*/
		}
		void DeviceExtensions(const std::vector<const char*>& extensionNames) {
			deviceExtensions = extensionNames;
		}

		static graphicsBase& Base() {
			return singleton;
		}
	};
	inline graphicsBase graphicsBase::singleton; // 定义, inline 变量, C++17 允许在类定义中直接初始化

}
