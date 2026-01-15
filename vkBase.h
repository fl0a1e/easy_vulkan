#pragma once

#include "vkStart.h"
#define VK_RESULT_THROW

#ifndef NDEBUG
#define ENABLE_DEBUG_MESSENGER true
#else
#define ENABLE_DEBUG_MESSENGER false
#endif


#define DestroyHandleBy(Func) if (handle) { Func(graphicsBase::Base().Device(), handle, nullptr); handle = VK_NULL_HANDLE; }
#define MoveHandle handle = other.handle; other.handle = VK_NULL_HANDLE;
#define DefineMoveAssignmentOperator(type) type& operator=(type&& other) { this->~type(); MoveHandle; return *this; }
#define DefineHandleTypeOperator operator decltype(handle)() const { return handle; }
#define DefineAddressFunction const decltype(handle)* Address() const { return &handle; }

#define ExecuteOnce(...) { static bool executed = false; if (executed) return __VA_ARGS__; executed = true; }

// define vulkan namespace
namespace vulkan {

	// 全局常量用constexpr修饰定义在类外：
	constexpr VkExtent2D defaultWindowSize = { 1280, 720 };
	// 方便把错误信息输出到自定义的位置
	inline auto& outStream = std::cout;

	// 继续封装一些工具类
	
	///Vulkan 的 API 返回的是 VkResult 错误码，而不是异常
	/// 忘记检查返回值，错误被忽略
	/// 所以“错误码必须被消费，否则就是程序错误”
	/// result_t是一个“必须被处理”的错误码包装器
//情况1：根据函数返回值确定是否抛异常
#ifdef VK_RESULT_THROW 
	class result_t {
		VkResult _result;
	public:
		static std::function<void(VkResult)> callback_throw; // 静态 全局的错误处理钩子，析构抛异常前会调用它
		result_t(VkResult result) : _result(result){} // 包装vk函数返回值
		result_t(result_t&& other) noexcept : _result(other._result) { other._result = VK_SUCCESS; } // 防止other在析构时抛异常，因为所有权已经转移了
		//若一个result_t类型对象在析构时仍旧保有一个错误代码，那么说明错误没有得到任何处理，抛出异常
		// 默认析构函数是 noexcept(true)，本来是不让抛异常的，容易资源释放不干净导致内存泄露
		~result_t() noexcept(false) {
			if (uint32_t(_result) < VK_RESULT_MAX_ENUM) { // 如果不是错误码，什么都不做
				return;
			}
			if (callback_throw) { // 是错误码，可以用回调函数处理一下
				callback_throw(_result);
			}
			throw _result; // 处理不了这个错误，在对象生命周期结束时炸掉程序
		}
		// 类型转换运算符，result_t -> VKResult
		operator VkResult() {
			VkResult result = this->_result;
			this->_result = VK_SUCCESS;
			return result;
		}
	};
	inline std::function<void(VkResult)> result_t::callback_throw;

//情况2：若抛弃函数返回值，让编译器发出警告
#elif VK_RESULT_NODISCARD
	struct [[nodiscard]] result_t { // [[nodiscard]], 没有使用这个类型的变量，编译器必须警告你
		VkResult _result;
		result_t(VkResult result) : _result(result) {}
		operator VkResult() const { return _result; }
	};
//关闭弃值提醒
#pragma warning(disable:4834)
#pragma warning(disable:6031)
#else //情况3：啥都不干
	using result_t = VkResult;
#endif

	template<typename T>
	class arrayRef {
		T* const pArray = nullptr;
		size_t count = 0;
	public:
		//从空参数构造，count为0
		arrayRef() = default;
		//从单个对象构造，count为1
		arrayRef(T& data) :pArray(&data), count(1) {}
		//从顶级数组构造
		template<size_t ElementCount>
		arrayRef(T(&data)[ElementCount]) : pArray(data), count(ElementCount) {}
		//从指针和元素个数构造
		arrayRef(T* pData, size_t elementCount) :pArray(pData), count(elementCount) {}
		//若T带const修饰，兼容从对应的无const修饰版本的arrayRef构造
		arrayRef(const arrayRef<std::remove_const_t<T>>& other) :pArray(other.Pointer()), count(other.Count()) {}
		//Getter
		T* Pointer() const { return pArray; }
		size_t Count() const { return count; }
		//Const Function
		T& operator[](size_t index) const { return pArray[index]; }
		T* begin() const { return pArray; }
		T* end() const { return pArray + count; }
		//Non-const Function
		//禁止复制/移动赋值（arrayRef旨在模拟“对数组的引用”，用处归根结底只是传参，故使其同C++引用的底层地址一样，防止初始化后被修改）
		arrayRef& operator=(const arrayRef&) = delete;
	};


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
		std::vector<std::function<void()>> callbacks_createDevice;
		std::vector<std::function<void()>> callbacks_destroyDevice;

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
		VkSwapchainCreateInfoKHR swapchainCreateInfo = {}; //保存交换链的创建信息以便重建交换链
		std::vector<std::function<void()>> callbacks_createSwapchain;  // 提升程序的可维护性
		std::vector<std::function<void()>> callbacks_destroySwapchain;


		// static
		static graphicsBase singleton; // 只是声明
		// -------------------------
		graphicsBase() = default;
		graphicsBase(graphicsBase&&) = delete; // 不可移动，没有定义复制构造器、复制赋值、移动赋值，四个函数全部无法使用
		~graphicsBase() {};

		static void ExecuteCallbacks(std::vector<std::function<void()>> callbacks) {
			for (size_t size = callbacks.size(), i = 0; i < size; i++)
				callbacks[i]();
		}

		//该函数被CreateSwapchain(...)和RecreateSwapchain()调用
		result_t CreateSwapchain_Internal() {
			if (VkResult result = vkCreateSwapchainKHR(device, &swapchainCreateInfo, nullptr, &swapchain)) {
				outStream << std::format("[ graphicsBase ] ERROR\nFailed to create a swapchain!\nError code: {}\n", int32_t(result));
				return result;
			}

			//获取交换连图像
			uint32_t swapchainImageCount;
			if (VkResult result = vkGetSwapchainImagesKHR(device, swapchain, &swapchainImageCount, nullptr)) {
				outStream << std::format("[ graphicsBase ] ERROR\nFailed to get the count of swapchain images!\nError code: {}\n", int32_t(result));
				return result;
			}
			swapchainImages.resize(swapchainImageCount);
			if (VkResult result = vkGetSwapchainImagesKHR(device, swapchain, &swapchainImageCount, swapchainImages.data())) {
				outStream << std::format("[ graphicsBase ] ERROR\nFailed to get swapchain images!\nError code: {}\n", int32_t(result));
				return result;
			}

			//创建image view
			swapchainImageViews.resize(swapchainImageCount);
			VkImageViewCreateInfo imageViewCreateInfo = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.format = swapchainCreateInfo.imageFormat,
				//.components = {}, //四个成员皆为VK_COMPONENT_SWIZZLE_IDENTITY
				.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
			};
			for (size_t i = 0; i < swapchainImageCount; i++) {
				imageViewCreateInfo.image = swapchainImages[i];
				if (VkResult result = vkCreateImageView(device, &imageViewCreateInfo, nullptr, &swapchainImageViews[i])) {
					outStream << std::format("[ graphicsBase ] ERROR\nFailed to create a swapchain image view!\nError code: {}\n", int32_t(result));
					return result;
				}
			}
			return VK_SUCCESS;
		}

		//该函数被DeterminePhysicalDevice(...)调用，用于检查物理设备是否满足所需的队列族类型，并将对应的队列族索引返回到queueFamilyIndices，执行成功时直接将索引写入相应成员变量
		result_t GetQueueFamilyIndices(VkPhysicalDevice physicalDevice, bool enableGraphicsQueue, bool enableComputeQueue, uint32_t(&queueFamilyIndices)[3]) {
			uint32_t queueFamilyCount = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
			if (!queueFamilyCount)
				return VK_RESULT_MAX_ENUM;
			std::vector<VkQueueFamilyProperties> queueFamilyPropertieses(queueFamilyCount);
			vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilyPropertieses.data());

			auto& [ig, ip, ic] = queueFamilyIndices; //分别对应图形、呈现、计算
			ig = ip = ic = VK_QUEUE_FAMILY_IGNORED;
			//遍历所有队列族的索引
			for (uint32_t i = 0; i < queueFamilyCount; i++) {
				//这三个VkBool32变量指示是否可获取（指应该被获取且能获取）相应队列族索引
				//用VkQueueFamilyProperties::queueFlags与VkQueueFlagBits的枚举项做位与，即可确定队列族支持的操作类型
				VkBool32
					//只在enableGraphicsQueue为true时获取支持图形操作的队列族的索引
					supportGraphics = enableGraphicsQueue && queueFamilyPropertieses[i].queueFlags & VK_QUEUE_GRAPHICS_BIT,
					supportPresentation = false,
					//只在enableComputeQueue为true时获取支持计算的队列族的索引
					supportCompute = enableComputeQueue && queueFamilyPropertieses[i].queueFlags & VK_QUEUE_COMPUTE_BIT;
				//只在创建了window surface时获取支持呈现的队列族的索引
				if (surface)
					if (VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &supportPresentation)) {
						outStream << std::format("[ graphicsBase ] ERROR\nFailed to determine if the queue family supports presentation!\nError code: {}\n", int32_t(result));
						return result;
					}
				//若某队列族同时支持图形操作和计算
				if (supportGraphics && supportCompute) {
					//若需要呈现，最好是三个队列族索引全部相同
					if (supportPresentation) {
						ig = ip = ic = i;
						break;
					}
					//除非ig和ic都已取得且相同，否则将它们的值覆写为i，以确保两个队列族索引相同
					if (ig != ic || ig == VK_QUEUE_FAMILY_IGNORED)
						ig = ic = i;
					//如果不需要呈现，那么已经可以break了
					if (!surface)
						break;
				}
				//若任何一个队列族索引可以被取得但尚未被取得，将其值覆写为i
				if (supportGraphics && ig == VK_QUEUE_FAMILY_IGNORED)
					ig = i;
				if (supportPresentation && ip == VK_QUEUE_FAMILY_IGNORED)
					ip = i;
				if (supportCompute && ic == VK_QUEUE_FAMILY_IGNORED)
					ic = i;
			}
			//若任何需要被取得的队列族索引尚未被取得，则函数执行失败
			if (ig == VK_QUEUE_FAMILY_IGNORED && enableGraphicsQueue ||
				ip == VK_QUEUE_FAMILY_IGNORED && surface ||
				ic == VK_QUEUE_FAMILY_IGNORED && enableComputeQueue)
				return VK_RESULT_MAX_ENUM;
			//函数执行成功时，将所取得的队列族索引写入到成员变量
			//用不着的队列族索引对应的成员变量会被覆写为VK_QUEUE_FAMILY_IGNORED
			queueFamilyIndex_graphics = ig;
			queueFamilyIndex_presentation = ip;
			queueFamilyIndex_compute = ic;
			return VK_SUCCESS;
		}

		//以下函数用于创建debug messenger
		result_t CreateDebugMessenger() {
			static PFN_vkDebugUtilsMessengerCallbackEXT DebugUtilsMessengerCallback = [](
				VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
				VkDebugUtilsMessageTypeFlagsEXT messageTypes,
				const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
				void* pUserData)->VkBool32 {
					outStream << std::format("{}\n\n", pCallbackData->pMessage);
					return VK_FALSE;
				};
			VkDebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfo = {
				.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
				.messageSeverity =
					VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
					VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
				.messageType =
					VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
					VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
					VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
				.pfnUserCallback = DebugUtilsMessengerCallback
			};
			PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessenger =
				reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
			if (vkCreateDebugUtilsMessenger) {
				VkResult result = vkCreateDebugUtilsMessenger(instance, &debugUtilsMessengerCreateInfo, nullptr, &debugMessenger);
				if (result)
					outStream << std::format("[ graphicsBase ] ERROR\nFailed to create a debug messenger!\nError code: {}\n", int32_t(result));
				return result;
			}
			outStream << std::format("[ graphicsBase ] ERROR\nFailed to get the function pointer of vkCreateDebugUtilsMessengerEXT!\n");
			return VK_RESULT_MAX_ENUM;
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

		result_t UseLatestApiVersion() {
			if (vkGetInstanceProcAddr(instance, "vkEnumerateInstanceVersion")) 
				return vkEnumerateInstanceVersion(&apiVersion);
			return VK_SUCCESS;
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

		// 取得surface的可用图像格式及色彩空间
		result_t GetSurfaceFormats() {
			uint32_t surfaceFormatCount;
			if (VkResult result = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &surfaceFormatCount, nullptr)) {
				outStream << std::format("[ graphicsBase ] ERROR\nFailed to get the count of surface formats!\nError code: {}\n", int32_t(result));
				return result;
			}
			if (!surfaceFormatCount)
				outStream << std::format("[ graphicsBase ] ERROR\nFailed to find any supported surface format!\n"),
				abort();
			availableSurfaceFormats.resize(surfaceFormatCount);
			VkResult result = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &surfaceFormatCount, availableSurfaceFormats.data());
			if (result)
				outStream << std::format("[ graphicsBase ] ERROR\nFailed to get surface formats!\nError code: {}\n", int32_t(result));
			return result;
		}

		result_t SetSurfaceFormat(VkSurfaceFormatKHR surfaceFormat) {
			bool formatIsAvailable = false;
			if (!surfaceFormat.format) {
				//如果格式未指定，只匹配色彩空间，图像格式有啥就用啥
				for (auto& i : availableSurfaceFormats)
					if (i.colorSpace == surfaceFormat.colorSpace) {
						swapchainCreateInfo.imageFormat = i.format;
						swapchainCreateInfo.imageColorSpace = i.colorSpace;
						formatIsAvailable = true;
						break;
					}
			}
			else
				//否则匹配格式和色彩空间
				for (auto& i : availableSurfaceFormats)
					if (i.format == surfaceFormat.format &&
						i.colorSpace == surfaceFormat.colorSpace) {
						swapchainCreateInfo.imageFormat = i.format;
						swapchainCreateInfo.imageColorSpace = i.colorSpace;
						formatIsAvailable = true;
						break;
					}
			//如果没有符合的格式，恰好有个语义相符的错误代码
			if (!formatIsAvailable)
				return VK_ERROR_FORMAT_NOT_SUPPORTED;
			//如果交换链已存在，调用RecreateSwapchain()重建交换链
			if (swapchain)
				return RecreateSwapchain();
			return VK_SUCCESS;
		}

		//该函数用于创建交换链
		result_t CreateSwapchain(bool limitFrameRate = true, VkSwapchainCreateFlagsKHR flags = 0) {
			VkSurfaceCapabilitiesKHR surfaceCapabilities = {};
			if (VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCapabilities)) {
				outStream << std::format("[ graphicsBase ] ERROR\nFailed to get physical device surface capabilities!\nError code: {}\n", int32_t(result));
				return result;
			}
			//指定图像数量
			swapchainCreateInfo.minImageCount = surfaceCapabilities.minImageCount + (surfaceCapabilities.maxImageCount > surfaceCapabilities.minImageCount);
			//指定图像大小
			swapchainCreateInfo.imageExtent =
				surfaceCapabilities.currentExtent.width == -1 ?
				VkExtent2D{
					glm::clamp(defaultWindowSize.width, surfaceCapabilities.minImageExtent.width, surfaceCapabilities.maxImageExtent.width),
					glm::clamp(defaultWindowSize.height, surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height) } :
					surfaceCapabilities.currentExtent;
			
			//指定变换方式
			swapchainCreateInfo.preTransform = surfaceCapabilities.currentTransform;
			//指定处理透明通道的方式
			if (surfaceCapabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)
				swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
			else
				for (size_t i = 0; i < 4; i++)
					if (surfaceCapabilities.supportedCompositeAlpha & 1 << i) {
						swapchainCreateInfo.compositeAlpha = VkCompositeAlphaFlagBitsKHR(surfaceCapabilities.supportedCompositeAlpha & 1 << i);
						break;
					}
			//指定图像用途
			swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; // 颜色附件
			if (surfaceCapabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) // 数据传送的来源
				swapchainCreateInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
			if (surfaceCapabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) // 数据传送的目标
				swapchainCreateInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			else
				outStream << std::format("[ graphicsBase ] WARNING\nVK_IMAGE_USAGE_TRANSFER_DST_BIT isn't supported!\n");
			
			//指定图像格式
			if (availableSurfaceFormats.empty())
				if (VkResult result = GetSurfaceFormats()) // 判断是否已获取surface格式
					return result;
			if (!swapchainCreateInfo.imageFormat)
				//用&&操作符来短路执行
				if (SetSurfaceFormat({ VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR }) &&
					SetSurfaceFormat({ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })) {
					//如果找不到上述图像格式和色彩空间的组合，那只能有什么用什么，采用availableSurfaceFormats中的第一组
					swapchainCreateInfo.imageFormat = availableSurfaceFormats[0].format;
					swapchainCreateInfo.imageColorSpace = availableSurfaceFormats[0].colorSpace;
					outStream << std::format("[ graphicsBase ] WARNING\nFailed to select a four-component UNORM surface format!\n");
				}

			// 指定呈现模式
			uint32_t surfacePresentModeCount;
			if (VkResult result = vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &surfacePresentModeCount, nullptr)) {
				outStream << std::format("[ graphicsBase ] ERROR\nFailed to get the count of surface present modes!\nError code: {}\n", int32_t(result));
				return result;
			}
			if (!surfacePresentModeCount)
				outStream << std::format("[ graphicsBase ] ERROR\nFailed to find any surface present mode!\n"),
				abort();
			std::vector<VkPresentModeKHR> surfacePresentModes(surfacePresentModeCount);
			if (VkResult result = vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &surfacePresentModeCount, surfacePresentModes.data())) {
				outStream << std::format("[ graphicsBase ] ERROR\nFailed to get surface present modes!\nError code: {}\n", int32_t(result));
				return result;
			}
			// 有几种呈现模式
			swapchainCreateInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
			if (!limitFrameRate)
				for (size_t i = 0; i < surfacePresentModeCount; i++)
					if (surfacePresentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
						swapchainCreateInfo.presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
						break;
					}

			//剩余参数
			swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
			swapchainCreateInfo.flags = flags;
			swapchainCreateInfo.surface = surface;
			swapchainCreateInfo.imageArrayLayers = 1;
			swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
			swapchainCreateInfo.clipped = VK_TRUE;

			// create
			if (VkResult result = CreateSwapchain_Internal())
				return result;
			//执行回调函数，ExecuteCallbacks(...)见后文
			ExecuteCallbacks(callbacks_createSwapchain);
			return VK_SUCCESS;
		}

		//该函数用于重建交换链
		result_t RecreateSwapchain() {
			VkSurfaceCapabilitiesKHR surfaceCapabilities = {};
			if (VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCapabilities)) {
				outStream << std::format("[ graphicsBase ] ERROR\nFailed to get physical device surface capabilities!\nError code: {}\n", int32_t(result));
				return result;
			}
			if (surfaceCapabilities.currentExtent.width == 0 ||
				surfaceCapabilities.currentExtent.height == 0)
				return VK_SUBOPTIMAL_KHR;
			swapchainCreateInfo.imageExtent = surfaceCapabilities.currentExtent;

			swapchainCreateInfo.oldSwapchain = swapchain;

			VkResult result = vkQueueWaitIdle(queue_graphics);
			//仅在等待图形队列成功，且图形与呈现所用队列不同时等待呈现队列
			if (!result &&
				queue_graphics != queue_presentation)
				result = vkQueueWaitIdle(queue_presentation);
			if (result) {
				outStream << std::format("[ graphicsBase ] ERROR\nFailed to wait for the queue to be idle!\nError code: {}\n", int32_t(result));
				return result;
			}

			//销毁旧交换链相关对象
			ExecuteCallbacks(callbacks_destroySwapchain);
			// 销毁旧有的image view
			for (auto& i : swapchainImageViews)
				if (i)
					vkDestroyImageView(device, i, nullptr);
			swapchainImageViews.resize(0);
			//创建新交换链及与之相关的对象
			if (result = CreateSwapchain_Internal())
				return result;
			//执行回调函数，ExecuteCallbacks(...)见后文
			ExecuteCallbacks(callbacks_createSwapchain);
			return VK_SUCCESS;
		}

		void AddCallback_CreateSwapchain(std::function<void()> function) {
			callbacks_createSwapchain.push_back(function);
		}
		void AddCallback_DestroySwapchain(std::function<void()> function) {
			callbacks_destroySwapchain.push_back(function);
		}


		//以下函数用于创建Vulkan实例前
		void AddInstanceLayer(const char* layerName) {
			AddLayerOrExtension(instanceLayers, layerName);
		}

		void AddInstanceExtension(const char* extensionName) {
			AddLayerOrExtension(instanceExtensions, extensionName);
		}

		//该函数用于创建Vulkan实例
		result_t CreateInstance(VkInstanceCreateFlags flags = 0) {
			if constexpr (ENABLE_DEBUG_MESSENGER)
				AddInstanceLayer("VK_LAYER_KHRONOS_validation"), // 验证层
				AddInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME); // debug扩展

			VkApplicationInfo applicationInfo = {
				.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
				.apiVersion = apiVersion,
			};
			VkInstanceCreateInfo instanceCreateInfo = {
				.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
				.pNext = nullptr,
				.flags = flags,
				.pApplicationInfo = &applicationInfo,
				.enabledLayerCount = static_cast<uint32_t>(instanceLayers.size()),
				.ppEnabledLayerNames = instanceLayers.data(),
				.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size()),
				.ppEnabledExtensionNames = instanceExtensions.data(),
			};

			if (VkResult result = vkCreateInstance(&instanceCreateInfo, nullptr, &instance)) {
				outStream << std::format("[ graphicsBase ] ERROR\nFailed to create a vulkan instance!\nError code: {}\n", int32_t(result));
				return result;
			}
			//成功创建Vulkan实例后，输出Vulkan版本
			outStream << std::format(
				"Vulkan API Version: {}.{}.{}\n",
				VK_VERSION_MAJOR(apiVersion),
				VK_VERSION_MINOR(apiVersion),
				VK_VERSION_PATCH(apiVersion));

			if constexpr (ENABLE_DEBUG_MESSENGER)
				//创建完Vulkan实例后紧接着创建debug messenger
				CreateDebugMessenger();
			return VK_SUCCESS;
		}


		//以下函数用于创建Vulkan实例失败后
		result_t CheckInstanceLayers(std::span<const char*> layersToCheck) {
			uint32_t layerCount;
			std::vector<VkLayerProperties> availableLayers;
			if (VkResult result = vkEnumerateInstanceLayerProperties(&layerCount, nullptr)) {
				outStream << std::format("[ graphicsBase ] ERROR\nFailed to get the count of instance layers!\n");
				return result;
			}
			if (layerCount) {
				availableLayers.resize(layerCount);
				if (VkResult result = vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data())) {
					outStream << std::format("[ graphicsBase ] ERROR\nFailed to enumerate instance layer properties!\nError code: {}\n", int32_t(result));
					return result;
				}
				for (auto& i : layersToCheck) {
					bool found = false;
					for (auto& j : availableLayers)
						if (!strcmp(i, j.layerName)) {
							found = true;
							break;
						}
					if (!found)
						i = nullptr;
				}
			}
			else {
				for (auto& i : layersToCheck)
					i = nullptr;
			}
			return VK_SUCCESS;
		}

		void InstanceLayers(const std::vector<const char*>& layerNames) {
			instanceLayers = layerNames;
		}

		result_t CheckInstanceExtensions(std::span<const char*> extensionsToCheck, const char* layerName = nullptr) const {
			uint32_t extensionCount;
			std::vector<VkExtensionProperties> availableExtensions;
			if (VkResult result = vkEnumerateInstanceExtensionProperties(layerName, &extensionCount, nullptr)) {
				layerName ?
					outStream << std::format("[ graphicsBase ] ERROR\nFailed to get the count of instance extensions!\nLayer name:{}\n", layerName) :
					outStream << std::format("[ graphicsBase ] ERROR\nFailed to get the count of instance extensions!\n");
				return result;
			}
			if (extensionCount) {
				availableExtensions.resize(extensionCount);
				if (VkResult result = vkEnumerateInstanceExtensionProperties(layerName, &extensionCount, availableExtensions.data())) {
					outStream << std::format("[ graphicsBase ] ERROR\nFailed to enumerate instance extension properties!\nError code: {}\n", int32_t(result));
					return result;
				}
				for (auto& i : extensionsToCheck) {
					bool found = false;
					for (auto& j : availableExtensions)
						if (!strcmp(i, j.extensionName)) {
							found = true;
							break;
						}
					if (!found)
						i = nullptr;
				}
			}
			else
				for (auto& i : extensionsToCheck)
					i = nullptr;
			return VK_SUCCESS;
		}

		void InstanceExtensions(const std::vector<const char*>& extensionNames) {
			instanceExtensions = extensionNames;
		}

		//该函数用于创建逻辑设备前
		void AddDeviceExtension(const char* extensionName) {
			AddLayerOrExtension(deviceExtensions, extensionName);
		}

		//该函数用于获取物理设备
		result_t GetPhysicalDevices() {
			uint32_t deviceCount;
			if (VkResult result = vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr)) { // check
				outStream << std::format("[ graphicsBase ] ERROR\nFailed to get the count of physical devices!\nError code: {}\n", int32_t(result));
				return result;
			}
			if (!deviceCount)
				outStream << std::format("[ graphicsBase ] ERROR\nFailed to find any physical device supports vulkan!\n"),
				abort();
			availablePhysicalDevices.resize(deviceCount); // get availablePhysicalDevices
			VkResult result = vkEnumeratePhysicalDevices(instance, &deviceCount, availablePhysicalDevices.data());
			if (result)
				outStream << std::format("[ graphicsBase ] ERROR\nFailed to enumerate physical devices!\nError code: {}\n", int32_t(result));
			return result;
		}

		//该函数用于指定所用物理设备并调用GetQueueFamilyIndices(...)取得队列族索引
		result_t DeterminePhysicalDevice(uint32_t deviceIndex = 0, bool enableGraphicsQueue = true, bool enableComputeQueue = true) {
			//定义一个特殊值用于标记一个队列族索引已被找过但未找到
			static constexpr uint32_t notFound = INT32_MAX; //== VK_QUEUE_FAMILY_IGNORED & INT32_MAX
			//定义队列族索引组合的结构体
			struct queueFamilyIndexCombination {
				uint32_t graphics = VK_QUEUE_FAMILY_IGNORED;
				uint32_t presentation = VK_QUEUE_FAMILY_IGNORED;
				uint32_t compute = VK_QUEUE_FAMILY_IGNORED;
			};
			//queueFamilyIndexCombinations用于为各个物理设备保存一份队列族索引组合
			static std::vector<queueFamilyIndexCombination> queueFamilyIndexCombinations(availablePhysicalDevices.size());
			auto& [ig, ip, ic] = queueFamilyIndexCombinations[deviceIndex];

			//如果有任何队列族索引已被找过但未找到，返回VK_RESULT_MAX_ENUM
			if (ig == notFound && enableGraphicsQueue ||
				ip == notFound && surface ||
				ic == notFound && enableComputeQueue)
				return VK_RESULT_MAX_ENUM;

			//如果有任何队列族索引应被获取但还未被找过
			if (ig == VK_QUEUE_FAMILY_IGNORED && enableGraphicsQueue ||
				ip == VK_QUEUE_FAMILY_IGNORED && surface ||
				ic == VK_QUEUE_FAMILY_IGNORED && enableComputeQueue) {
				uint32_t indices[3];
				VkResult result = GetQueueFamilyIndices(availablePhysicalDevices[deviceIndex], enableGraphicsQueue, enableComputeQueue, indices);
				//若GetQueueFamilyIndices(...)返回VK_SUCCESS或VK_RESULT_MAX_ENUM（vkGetPhysicalDeviceSurfaceSupportKHR(...)执行成功但没找齐所需队列族），
				//说明对所需队列族索引已有结论，保存结果到queueFamilyIndexCombinations[deviceIndex]中相应变量
				//应被获取的索引若仍为VK_QUEUE_FAMILY_IGNORED，说明未找到相应队列族，VK_QUEUE_FAMILY_IGNORED（~0u）与INT32_MAX做位与得到的数值等于notFound
				if (result == VK_SUCCESS ||
					result == VK_RESULT_MAX_ENUM) {
					if (enableGraphicsQueue)
						ig = indices[0] & INT32_MAX;
					if (surface)
						ip = indices[1] & INT32_MAX;
					if (enableComputeQueue)
						ic = indices[2] & INT32_MAX;
				}
				//如果GetQueueFamilyIndices(...)执行失败，return
				if (result)
					return result;
			}

			//若以上两个if分支皆不执行，则说明所需的队列族索引皆已被获取，从queueFamilyIndexCombinations[deviceIndex]中取得索引
			else {
				queueFamilyIndex_graphics = enableGraphicsQueue ? ig : VK_QUEUE_FAMILY_IGNORED;
				queueFamilyIndex_presentation = surface ? ip : VK_QUEUE_FAMILY_IGNORED;
				queueFamilyIndex_compute = enableComputeQueue ? ic : VK_QUEUE_FAMILY_IGNORED;
			}
			physicalDevice = availablePhysicalDevices[deviceIndex];
			return VK_SUCCESS;
		}

		//该函数用于创建逻辑设备，并取得队列
		result_t CreateDevice(VkDeviceCreateFlags flags = 0) {
			float queuePriority = 1.f;
			VkDeviceQueueCreateInfo queueCreateInfos[3] = {
				{
					.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
					.queueCount = 1,
					.pQueuePriorities = &queuePriority },
				{
					.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
					.queueCount = 1,
					.pQueuePriorities = &queuePriority },
				{
					.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
					.queueCount = 1,
					.pQueuePriorities = &queuePriority } };
			uint32_t queueCreateInfoCount = 0;
			if (queueFamilyIndex_graphics != VK_QUEUE_FAMILY_IGNORED)
				queueCreateInfos[queueCreateInfoCount++].queueFamilyIndex = queueFamilyIndex_graphics;
			if (queueFamilyIndex_presentation != VK_QUEUE_FAMILY_IGNORED &&
				queueFamilyIndex_presentation != queueFamilyIndex_graphics)
				queueCreateInfos[queueCreateInfoCount++].queueFamilyIndex = queueFamilyIndex_presentation;
			if (queueFamilyIndex_compute != VK_QUEUE_FAMILY_IGNORED &&
				queueFamilyIndex_compute != queueFamilyIndex_graphics &&
				queueFamilyIndex_compute != queueFamilyIndex_presentation)
				queueCreateInfos[queueCreateInfoCount++].queueFamilyIndex = queueFamilyIndex_compute;
			VkPhysicalDeviceFeatures physicalDeviceFeatures;
			vkGetPhysicalDeviceFeatures(physicalDevice, &physicalDeviceFeatures);
			VkDeviceCreateInfo deviceCreateInfo = {
				.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
				.flags = flags,
				.queueCreateInfoCount = queueCreateInfoCount,
				.pQueueCreateInfos = queueCreateInfos,
				.enabledExtensionCount = uint32_t(deviceExtensions.size()),
				.ppEnabledExtensionNames = deviceExtensions.data(),
				.pEnabledFeatures = &physicalDeviceFeatures
			};
			if (VkResult result = vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device)) {
				outStream << std::format("[ graphicsBase ] ERROR\nFailed to create a vulkan logical device!\nError code: {}\n", int32_t(result));
				return result;
			}
			if (queueFamilyIndex_graphics != VK_QUEUE_FAMILY_IGNORED)
				vkGetDeviceQueue(device, queueFamilyIndex_graphics, 0, &queue_graphics);
			if (queueFamilyIndex_presentation != VK_QUEUE_FAMILY_IGNORED)
				vkGetDeviceQueue(device, queueFamilyIndex_presentation, 0, &queue_presentation);
			if (queueFamilyIndex_compute != VK_QUEUE_FAMILY_IGNORED)
				vkGetDeviceQueue(device, queueFamilyIndex_compute, 0, &queue_compute);
			vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);
			vkGetPhysicalDeviceMemoryProperties(physicalDevice, &physicalDeviceMemoryProperties);
			//输出所用的物理设备名称
			outStream << std::format("Renderer: {}\n", physicalDeviceProperties.deviceName);
			
			ExecuteCallbacks(callbacks_createDevice);
			return VK_SUCCESS;
		}

		void AddCallback_CreateDevice(std::function<void()> function) {
			callbacks_createDevice.push_back(function);
		}
		void AddCallback_DestroyDevice(std::function<void()> function) {
			callbacks_destroyDevice.push_back(function);
		}

		result_t WaitIdle() const {
			VkResult result = vkDeviceWaitIdle(device);
			if (result)
				outStream << std::format("[ graphicsBase ] ERROR\nFailed to wait for the device to be idle!\nError code: {}\n", int32_t(result));
			return result;
		}

		//以下函数用于创建逻辑设备失败后
		result_t CheckDeviceExtensions(std::span<const char*> extensionsToCheck, const char* layerName = nullptr) const {
			/*待Ch1-3填充*/
			return VK_SUCCESS;
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
