#pragma once

#include "vkStart.h"
#include "vkBase.h"

namespace rayTracing {
    struct BlasInput {
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceSize vertexOffset = 0;
        uint32_t vertexCount = 0;
        VkDeviceSize vertexStride = 0;
        VkFormat vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;

        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceSize indexOffset = 0;
        uint32_t indexCount = 0;
        VkIndexType indexType = VK_INDEX_TYPE_UINT32;
    };

    struct TlasInstanceInput {
        uint32_t blasIndex = 0;
        glm::mat4 transform = glm::mat4(1.0f);
        uint32_t customIndex = 0;
        uint8_t mask = 0xFF;
    };

    class SceneAS {
    public:
        void Create(const std::vector<BlasInput>& blasInputs, const std::vector<TlasInstanceInput>& instances);
        void Destroy();
        VkAccelerationStructureKHR Tlas() const { return m_tlas; }

    private:
        struct AccelStruct {
            VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
            vulkan::buffer buffer;
            vulkan::deviceMemory memory;
            uint64_t deviceAddress = 0;
        };

        AccelStruct BuildBLAS(const BlasInput& input);
        void BuildTLAS(const std::vector<TlasInstanceInput>& instances);

        uint64_t GetBufferDeviceAddress(VkBuffer buffer) const;
        vulkan::buffer CreateScratchBuffer(VkDeviceSize size, vulkan::deviceMemory& outMemory) const;
        vulkan::buffer CreateDeviceAddressBuffer(
            VkDeviceSize size,
            VkBufferUsageFlags usage,
            vulkan::deviceMemory& outMemory,
            VkMemoryPropertyFlags memoryProperties) const;
        void SubmitImmediate(const std::function<void(VkCommandBuffer)>& record) const;

        PFN_vkCreateAccelerationStructureKHR m_vkCreateAccelerationStructureKHR = nullptr;
        PFN_vkDestroyAccelerationStructureKHR m_vkDestroyAccelerationStructureKHR = nullptr;
        PFN_vkGetAccelerationStructureBuildSizesKHR m_vkGetAccelerationStructureBuildSizesKHR = nullptr;
        PFN_vkCmdBuildAccelerationStructuresKHR m_vkCmdBuildAccelerationStructuresKHR = nullptr;
        PFN_vkGetAccelerationStructureDeviceAddressKHR m_vkGetAccelerationStructureDeviceAddressKHR = nullptr;

        std::vector<AccelStruct> m_blas;
        AccelStruct m_tlas;
    };
}
