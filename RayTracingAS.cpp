#include "RayTracingAS.hpp"

namespace rayTracing {
    namespace {
        uint32_t FindMemoryTypeIndexForAS(uint32_t memoryTypeBits, VkMemoryPropertyFlags requiredProperties) {
            const auto& memoryProperties = vulkan::graphicsBase::Base().PhysicalDeviceMemoryProperties();
            for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
                const bool typeMatches = memoryTypeBits & (1u << i);
                const bool propertyMatches =
                    (memoryProperties.memoryTypes[i].propertyFlags & requiredProperties) == requiredProperties;
                if (typeMatches && propertyMatches)
                    return i;
            }

            std::cout << std::format("[ RayTracingAS ] ERROR\nFailed to find memory type with flags: {}\n", static_cast<uint32_t>(requiredProperties));
            abort();
        }

        VkTransformMatrixKHR ToVkTransformMatrix(const glm::mat4& matrix) {
            VkTransformMatrixKHR out{};
            for (uint32_t row = 0; row < 3; ++row) {
                for (uint32_t col = 0; col < 4; ++col)
                    out.matrix[row][col] = matrix[col][row];
            }
            return out;
        }
    }

    void SceneAS::Create(const std::vector<BlasInput>& blasInputs, const std::vector<TlasInstanceInput>& instances) {
        Destroy();

        auto device = vulkan::graphicsBase::Base().Device();
        m_vkCreateAccelerationStructureKHR = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(
            vkGetDeviceProcAddr(device, "vkCreateAccelerationStructureKHR"));
        m_vkDestroyAccelerationStructureKHR = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
            vkGetDeviceProcAddr(device, "vkDestroyAccelerationStructureKHR"));
        m_vkGetAccelerationStructureBuildSizesKHR = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(
            vkGetDeviceProcAddr(device, "vkGetAccelerationStructureBuildSizesKHR"));
        m_vkCmdBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(
            vkGetDeviceProcAddr(device, "vkCmdBuildAccelerationStructuresKHR"));
        m_vkGetAccelerationStructureDeviceAddressKHR = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(
            vkGetDeviceProcAddr(device, "vkGetAccelerationStructureDeviceAddressKHR"));

        if (!m_vkCreateAccelerationStructureKHR ||
            !m_vkDestroyAccelerationStructureKHR ||
            !m_vkGetAccelerationStructureBuildSizesKHR ||
            !m_vkCmdBuildAccelerationStructuresKHR ||
            !m_vkGetAccelerationStructureDeviceAddressKHR) {
            std::cout << "[ RayTracingAS ] ERROR\nFailed to load KHR acceleration structure function pointers.\n";
            abort();
        }

        m_blas.reserve(blasInputs.size());
        for (const auto& input : blasInputs)
            m_blas.push_back(BuildBLAS(input));

        BuildTLAS(instances);
    }

    void SceneAS::Destroy() {
        auto device = vulkan::graphicsBase::Base().Device();

        if (m_tlas.handle && m_vkDestroyAccelerationStructureKHR)
            m_vkDestroyAccelerationStructureKHR(device, m_tlas.handle, nullptr);
        m_tlas = {};

        for (auto& blas : m_blas) {
            if (blas.handle && m_vkDestroyAccelerationStructureKHR)
                m_vkDestroyAccelerationStructureKHR(device, blas.handle, nullptr);
            blas = {};
        }
        m_blas.clear();
    }

    SceneAS::AccelStruct SceneAS::BuildBLAS(const BlasInput& input) {
        VkAccelerationStructureGeometryTrianglesDataKHR triangles{};
        triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        triangles.vertexFormat = input.vertexFormat;
        triangles.vertexData.deviceAddress = GetBufferDeviceAddress(input.vertexBuffer) + input.vertexOffset;
        triangles.vertexStride = input.vertexStride;
        triangles.maxVertex = input.vertexCount ? (input.vertexCount - 1) : 0;
        triangles.indexType = input.indexType;
        triangles.indexData.deviceAddress = GetBufferDeviceAddress(input.indexBuffer) + input.indexOffset;

        VkAccelerationStructureGeometryKHR geometry{};
        geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        geometry.geometry.triangles = triangles;

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &geometry;

        const uint32_t primitiveCount = input.indexCount / 3;
        VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
        sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        m_vkGetAccelerationStructureBuildSizesKHR(
            vulkan::graphicsBase::Base().Device(),
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &buildInfo,
            &primitiveCount,
            &sizeInfo);

        AccelStruct blas{};
        blas.buffer = CreateDeviceAddressBuffer(
            sizeInfo.accelerationStructureSize,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
            blas.memory,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VkAccelerationStructureCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        createInfo.buffer = blas.buffer;
        createInfo.size = sizeInfo.accelerationStructureSize;
        createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        if (VkResult result = m_vkCreateAccelerationStructureKHR(vulkan::graphicsBase::Base().Device(), &createInfo, nullptr, &blas.handle)) {
            std::cout << std::format("[ RayTracingAS ] ERROR\nFailed to create BLAS!\nError code: {}\n", int32_t(result));
            abort();
        }

        vulkan::deviceMemory scratchMemory;
        auto scratchBuffer = CreateScratchBuffer(sizeInfo.buildScratchSize, scratchMemory);
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.dstAccelerationStructure = blas.handle;
        buildInfo.scratchData.deviceAddress = GetBufferDeviceAddress(scratchBuffer);

        VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
        rangeInfo.primitiveCount = primitiveCount;
        const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

        SubmitImmediate([&](VkCommandBuffer cmd) {
            m_vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRangeInfo);
        });

        VkAccelerationStructureDeviceAddressInfoKHR addressInfo{};
        addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        addressInfo.accelerationStructure = blas.handle;
        blas.deviceAddress = m_vkGetAccelerationStructureDeviceAddressKHR(vulkan::graphicsBase::Base().Device(), &addressInfo);
        return blas;
    }

    void SceneAS::BuildTLAS(const std::vector<TlasInstanceInput>& instances) {
        if (instances.empty()) {
            std::cout << "[ RayTracingAS ] ERROR\nNo instances were provided for TLAS build.\n";
            abort();
        }

        std::vector<VkAccelerationStructureInstanceKHR> vkInstances;
        vkInstances.reserve(instances.size());
        for (const auto& instance : instances) {
            if (instance.blasIndex >= m_blas.size()) {
                std::cout << std::format("[ RayTracingAS ] ERROR\nInvalid BLAS index: {}\n", instance.blasIndex);
                abort();
            }

            VkAccelerationStructureInstanceKHR asInstance{};
            asInstance.transform = ToVkTransformMatrix(instance.transform);
            asInstance.instanceCustomIndex = instance.customIndex;
            asInstance.mask = instance.mask;
            asInstance.instanceShaderBindingTableRecordOffset = 0;
            asInstance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
            asInstance.accelerationStructureReference = m_blas[instance.blasIndex].deviceAddress;
            vkInstances.push_back(asInstance);
        }

        vulkan::buffer instanceBuffer;
        vulkan::deviceMemory instanceMemory;
        const VkDeviceSize instanceBufferSize = sizeof(VkAccelerationStructureInstanceKHR) * vkInstances.size();
        instanceBuffer = CreateDeviceAddressBuffer(
            instanceBufferSize,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
            instanceMemory,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        instanceMemory.Write(vkInstances.data(), static_cast<size_t>(instanceBufferSize));

        VkAccelerationStructureGeometryInstancesDataKHR instancesData{};
        instancesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        instancesData.arrayOfPointers = VK_FALSE;
        instancesData.data.deviceAddress = GetBufferDeviceAddress(instanceBuffer);

        VkAccelerationStructureGeometryKHR geometry{};
        geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geometry.geometry.instances = instancesData;

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &geometry;

        const uint32_t primitiveCount = static_cast<uint32_t>(vkInstances.size());
        VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
        sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        m_vkGetAccelerationStructureBuildSizesKHR(
            vulkan::graphicsBase::Base().Device(),
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &buildInfo,
            &primitiveCount,
            &sizeInfo);

        m_tlas.buffer = CreateDeviceAddressBuffer(
            sizeInfo.accelerationStructureSize,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
            m_tlas.memory,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VkAccelerationStructureCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        createInfo.buffer = m_tlas.buffer;
        createInfo.size = sizeInfo.accelerationStructureSize;
        createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        if (VkResult result = m_vkCreateAccelerationStructureKHR(vulkan::graphicsBase::Base().Device(), &createInfo, nullptr, &m_tlas.handle)) {
            std::cout << std::format("[ RayTracingAS ] ERROR\nFailed to create TLAS!\nError code: {}\n", int32_t(result));
            abort();
        }

        vulkan::deviceMemory scratchMemory;
        auto scratchBuffer = CreateScratchBuffer(sizeInfo.buildScratchSize, scratchMemory);
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.dstAccelerationStructure = m_tlas.handle;
        buildInfo.scratchData.deviceAddress = GetBufferDeviceAddress(scratchBuffer);

        VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
        rangeInfo.primitiveCount = primitiveCount;
        const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

        SubmitImmediate([&](VkCommandBuffer cmd) {
            m_vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRangeInfo);
        });

        VkAccelerationStructureDeviceAddressInfoKHR addressInfo{};
        addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        addressInfo.accelerationStructure = m_tlas.handle;
        m_tlas.deviceAddress = m_vkGetAccelerationStructureDeviceAddressKHR(vulkan::graphicsBase::Base().Device(), &addressInfo);
    }

    uint64_t SceneAS::GetBufferDeviceAddress(VkBuffer buffer) const {
        VkBufferDeviceAddressInfo addressInfo{};
        addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addressInfo.buffer = buffer;
        return vkGetBufferDeviceAddress(vulkan::graphicsBase::Base().Device(), &addressInfo);
    }

    vulkan::buffer SceneAS::CreateDeviceAddressBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        vulkan::deviceMemory& outMemory,
        VkMemoryPropertyFlags memoryProperties) const {
        vulkan::buffer outBuffer;
        outBuffer.Create(size, usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

        const auto requirements = outBuffer.MemoryRequirements();
        VkMemoryAllocateFlagsInfo flagsInfo{};
        flagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.pNext = &flagsInfo;
        allocInfo.allocationSize = requirements.size;
        allocInfo.memoryTypeIndex = FindMemoryTypeIndexForAS(
            requirements.memoryTypeBits,
            memoryProperties);
        outMemory.Create(allocInfo);
        outBuffer.BindMemory(outMemory);
        return outBuffer;
    }

    vulkan::buffer SceneAS::CreateScratchBuffer(VkDeviceSize size, vulkan::deviceMemory& outMemory) const {
        return CreateDeviceAddressBuffer(
            size,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            outMemory,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    }

    void SceneAS::SubmitImmediate(const std::function<void(VkCommandBuffer)>& record) const {
        vulkan::commandPool pool(vulkan::graphicsBase::Base().QueueFamilyIndex_Graphics(), VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
        vulkan::commandBuffer cmd;
        pool.AllocateBuffers(cmd);
        cmd.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        record(cmd);
        cmd.End();

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = cmd.Address();
        vulkan::graphicsBase::Base().SubmitCommandBuffer_Graphics(submitInfo);
        vulkan::graphicsBase::Base().WaitIdle();
    }
}
