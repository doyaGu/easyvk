/*
   Copyright 2023 Reese	Levine,	Devon McKee, Sean Siddens

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include "easyvk.h"

#include <cstring>
#include <cstdarg>
#include <iostream>
#include <fstream>

// TODO: extend this to include ios logging lib
void evk_log(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
#ifdef __ANDROID__
    __android_log_vprint(ANDROID_LOG_INFO, "EasyVK", fmt, args);
#else
    vprintf(fmt, args);
#endif
    va_end(args);
}

// Would use string_VkResult() for this but vk_enum_string_helper.h is no more...
inline const char *vkResultString(VkResult res) {
    switch (res) {
    // 1.0
    case VK_SUCCESS:
        return "VK_SUCCESS";
    case VK_NOT_READY:
        return "VK_NOT_READY";
    case VK_TIMEOUT:
        return "VK_TIMEOUT";
    case VK_EVENT_SET:
        return "VK_EVENT_SET";
    case VK_EVENT_RESET:
        return "VK_EVENT_RESET";
    case VK_INCOMPLETE:
        return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:
        return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:
        return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED:
        return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT:
        return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:
        return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:
        return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
        return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS:
        return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED:
        return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_FRAGMENTED_POOL:
        return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_UNKNOWN:
        return "VK_ERROR_UNKNOWN";
    // 1.1
    case VK_ERROR_OUT_OF_POOL_MEMORY:
        return "VK_ERROR_OUT_OF_POOL_MEMORY";
    case VK_ERROR_INVALID_EXTERNAL_HANDLE:
        return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
    // 1.2
    case VK_ERROR_FRAGMENTATION:
        return "VK_ERROR_FRAGMENTATION";
    case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS:
        return "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS";
    // 1.3
    case VK_PIPELINE_COMPILE_REQUIRED: return "VK_PIPELINE_COMPILE_REQUIRED";
    default:
        return "UNKNOWN_ERROR";
    }
}

// Returns readable vendor name from vendorID, based on vulkan.gpuinfo.org entries
inline const char *vkVendorName(uint32_t vid) {
    switch (vid) {
    case 0x10DE:
        return "NVIDIA";
    case 0x1002:
        return "AMD";
    case 0x8086:
        return "Intel";
    case 0x106B:
        return "Apple";
    case 0x13B5:
        return "ARM";
    case 0x5143:
        return "Qualcomm";
    default:
        return "UNKNOWN";
    }
}

namespace easyvk {
    void vkCheck(VkResult result, const char *file, int line) {
        if (result != VK_SUCCESS) {
            std::string message = "Vulkan error: " + std::string(vkResultString(result));
            throw VulkanError(result, message, file, line);
        }
    }

    const char *vkDeviceType(VkPhysicalDeviceType type) {
        switch (type) {
        case VK_PHYSICAL_DEVICE_TYPE_OTHER:
            return "VK_PHYSICAL_DEVICE_TYPE_OTHER";
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            return "VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU";
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            return "VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            return "VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU";
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
            return "VK_PHYSICAL_DEVICE_TYPE_CPU";
        default:
            return "UNKNOWN_DEVICE_TYPE";
        }
    }

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugReporter(VkDebugReportFlagsEXT, VkDebugReportObjectTypeEXT, uint64_t, size_t, int32_t, const char *pLayerPrefix, const char *pMessage, void *pUserData) {
        std::cerr << "\x1B[31m[Vulkan:" << pLayerPrefix << "]\033[0m " << pMessage << "\n";
        return VK_FALSE;
    }

    Instance::Instance(bool enableValidationLayers)
        : enableValidationLayers_(enableValidationLayers),
          instance_(VK_NULL_HANDLE),
          debugReportCallback_(VK_NULL_HANDLE),
          tornDown_(false) {
        std::vector<const char *> enabledLayers;
        std::vector<const char *> enabledExtensions;
        VK_CHECK(volkInitialize());

        if (enableValidationLayers_) {
            enabledLayers.push_back("VK_LAYER_KHRONOS_validation");
            enabledExtensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
        }

#ifdef __APPLE__
        enabledExtensions.push_back("VK_KHR_portability_enumeration");
#endif

        VkApplicationInfo appInfo{
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pNext = nullptr,
            .pApplicationName = "EasyVK Application",
            .applicationVersion = 0,
            .pEngineName = "Heterogeneous Programming Group",
            .engineVersion = 0,
            .apiVersion = VK_API_VERSION_1_3
        };

#ifdef __APPLE__
        VkInstanceCreateFlags instanceCreateFlags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#else
        VkInstanceCreateFlags instanceCreateFlags = 0;
#endif

        // Define instance create info
        VkInstanceCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = instanceCreateFlags,
            .pApplicationInfo = &appInfo,
            .enabledLayerCount = static_cast<uint32_t>(enabledLayers.size()),
            .ppEnabledLayerNames = enabledLayers.data(),
            .enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size()),
            .ppEnabledExtensionNames = enabledExtensions.data()
        };

        // Create instance
        VK_CHECK(vkCreateInstance(&createInfo, nullptr, &instance_));
        volkLoadInstance(instance_);

        if (enableValidationLayers_) {
            VkDebugReportCallbackCreateInfoEXT debugCreateInfo{
                .sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT,
                .pNext = nullptr,
                .flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT,
                .pfnCallback = debugReporter,
                .pUserData = nullptr
            };

            // Load debug report callback extension
            auto createFN = PFN_vkCreateDebugReportCallbackEXT(vkGetInstanceProcAddr(instance_, "vkCreateDebugReportCallbackEXT"));
            if (createFN) {
                createFN(instance_, &debugCreateInfo, nullptr, &debugReportCallback_);
            }
        }

        // Print out vulkan's instance version
        uint32_t version;
        auto my_EnumerateInstanceVersion = (PFN_vkEnumerateInstanceVersion) vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion");
        if (nullptr != my_EnumerateInstanceVersion) {
            my_EnumerateInstanceVersion(&version);
        }
    }

    Instance::Instance(Instance &&other) noexcept
        : enableValidationLayers_(other.enableValidationLayers_),
          instance_(other.instance_),
          debugReportCallback_(other.debugReportCallback_),
          tornDown_(other.tornDown_) {
        // Reset other object to prevent double cleanup
        other.instance_ = VK_NULL_HANDLE;
        other.debugReportCallback_ = VK_NULL_HANDLE;
        other.tornDown_ = true;
    }

    Instance &Instance::operator=(Instance &&other) noexcept {
        if (this != &other) {
            // Cleanup current resources
            teardown();

            // Transfer ownership from other
            enableValidationLayers_ = other.enableValidationLayers_;
            instance_ = other.instance_;
            debugReportCallback_ = other.debugReportCallback_;
            tornDown_ = other.tornDown_;

            // Reset other object
            other.instance_ = VK_NULL_HANDLE;
            other.debugReportCallback_ = VK_NULL_HANDLE;
            other.tornDown_ = true;
        }
        return *this;
    }

    Instance::~Instance() {
        if (!tornDown_) {
            teardown();
        }
    }

    std::vector<VkPhysicalDevice> Instance::physicalDevices() {
        // Get physical device count
        uint32_t deviceCount = 0;
        VK_CHECK(vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr));

        // Enumerate physical devices based on deviceCount
        std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
        VK_CHECK(vkEnumeratePhysicalDevices(instance_, &deviceCount, physicalDevices.data()));

        return physicalDevices;
    }

    void Instance::teardown() {
        if (tornDown_) return;

        // Destroy debug report callback extension
        if (enableValidationLayers_ && debugReportCallback_ != VK_NULL_HANDLE) {
            auto destroyFn = PFN_vkDestroyDebugReportCallbackEXT(vkGetInstanceProcAddr(instance_, "vkDestroyDebugReportCallbackEXT"));
            if (destroyFn) {
                destroyFn(instance_, debugReportCallback_, nullptr);
            }
            debugReportCallback_ = VK_NULL_HANDLE;
        }

        // Destroy instance
        if (instance_ != VK_NULL_HANDLE) {
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
        }

        tornDown_ = true;
    }

    uint32_t getComputeFamilyId(VkPhysicalDevice physicalDevice) {
        // Get queue family count
        uint32_t queueFamilyPropertyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyPropertyCount, nullptr);

        std::vector<VkQueueFamilyProperties> familyProperties(queueFamilyPropertyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyPropertyCount, familyProperties.data());

        // Get compute family id based on size of family properties
        for (uint32_t i = 0; i < familyProperties.size(); ++i) {
            if (familyProperties[i].queueCount > 0 && (familyProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
                return i;
            }
        }
        return INVALID_QUEUE_FAMILY;
    }

    Device::Device(easyvk::Instance &instance, VkPhysicalDevice physicalDevice)
        : device(VK_NULL_HANDLE),
          computeFamilyId(getComputeFamilyId(physicalDevice)),
          computeQueue(VK_NULL_HANDLE),
          supportsAMDShaderStats(false),
          instance_(instance),
          physicalDevice_(physicalDevice),
          tornDown_(false) {
        if (computeFamilyId == INVALID_QUEUE_FAMILY) {
            throw std::runtime_error("No compute queue family found");
        }

        // Define device queue info
        float priority = 1.0f;
        VkDeviceQueueCreateInfo queueCreateInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queueFamilyIndex = computeFamilyId,
            .queueCount = 1,
            .pQueuePriorities = &priority
        };

        // Check for support for extensions
        uint32_t pPropertyCount;
        vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &pPropertyCount, nullptr);
        std::vector<VkExtensionProperties> extensions(pPropertyCount);
        vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &pPropertyCount, extensions.data());

        std::vector<const char *> enabledExtensions{};
        for (const auto &extension : extensions) {
            if (strcmp(extension.extensionName, "VK_AMD_shader_info") == 0) {
                enabledExtensions.push_back(VK_AMD_SHADER_INFO_EXTENSION_NAME);
                supportsAMDShaderStats = true;
            } else if (strcmp(extension.extensionName, "VK_KHR_pipeline_executable_properties") == 0) {
                enabledExtensions.push_back(VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME);
            } else if (strcmp(extension.extensionName, "VK_KHR_portability_subset") == 0) {
                enabledExtensions.push_back("VK_KHR_portability_subset");
            } else if (strcmp(extension.extensionName, "VK_KHR_shader_non_semantic_info") == 0) {
                enabledExtensions.push_back("VK_KHR_shader_non_semantic_info");
            }
        }

        // Enable support for computeFullSubgroups
        VkPhysicalDeviceVulkan13Features vulkan13Features{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
            .pNext = nullptr
        };

        // Enable pipeline executable properties reporting
        VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR pipelineProperties{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR,
            .pNext = &vulkan13Features
        };

        // Mostly for enabling buffer device addresses
        VkPhysicalDeviceVulkan12Features vulkan12Features{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
            .pNext = &pipelineProperties
        };

        VkPhysicalDeviceFeatures2 features2{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &vulkan12Features
        };

        vkGetPhysicalDeviceFeatures2(physicalDevice_, &features2);
        features2.features.robustBufferAccess = VK_FALSE;

        // Define device info
        VkDeviceCreateInfo deviceCreateInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = &vulkan12Features,
            .flags = 0,
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &queueCreateInfo,
            .enabledLayerCount = 0,
            .ppEnabledLayerNames = nullptr,
            .enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size()),
            .ppEnabledExtensionNames = enabledExtensions.data(),
            .pEnabledFeatures = &features2.features
        };

        // Create device
        VK_CHECK(vkCreateDevice(physicalDevice_, &deviceCreateInfo, nullptr, &device));
        // Get queue handle
        vkGetDeviceQueue(device, computeFamilyId, 0, &computeQueue);

        // Get device properties
        vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
    }

    Device::Device(Device &&other) noexcept
        : device(other.device),
          properties(other.properties),
          computeFamilyId(other.computeFamilyId),
          computeQueue(other.computeQueue),
          supportsAMDShaderStats(other.supportsAMDShaderStats),
          instance_(other.instance_),
          physicalDevice_(other.physicalDevice_),
          tornDown_(other.tornDown_) {
        // Reset other object to prevent double cleanup
        other.device = VK_NULL_HANDLE;
        other.computeQueue = VK_NULL_HANDLE;
        other.physicalDevice_ = VK_NULL_HANDLE;
        other.tornDown_ = true;
    }

    Device &Device::operator=(Device &&other) noexcept {
        if (this != &other) {
            // Cleanup current resources
            teardown();

            // Transfer ownership from other
            device = other.device;
            properties = other.properties;
            computeFamilyId = other.computeFamilyId;
            computeQueue = other.computeQueue;
            supportsAMDShaderStats = other.supportsAMDShaderStats;
            physicalDevice_ = other.physicalDevice_;
            tornDown_ = other.tornDown_;

            // Reset other object
            other.device = VK_NULL_HANDLE;
            other.computeQueue = VK_NULL_HANDLE;
            other.physicalDevice_ = VK_NULL_HANDLE;
            other.tornDown_ = true;
        }
        return *this;
    }

    Device::~Device() {
        if (!tornDown_) {
            teardown();
        }
    }

    uint32_t Device::selectMemory(uint32_t memoryTypeBits, VkMemoryPropertyFlags flags) {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((memoryTypeBits & (1u << i)) && ((flags & memProperties.memoryTypes[i].propertyFlags) == flags)) {
                return i;
            }
        }
        throw std::runtime_error("Failed to find suitable memory type");
    }

    uint32_t Device::subgroupSize() {
        VkPhysicalDeviceSubgroupProperties subgroupProperties{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES,
            .pNext = nullptr
        };

        VkPhysicalDeviceProperties2 physicalDeviceProperties{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
            .pNext = &subgroupProperties
        };

        vkGetPhysicalDeviceProperties2(physicalDevice_, &physicalDeviceProperties);
        return subgroupProperties.subgroupSize;
    }

    const char *Device::vendorName() {
        return vkVendorName(properties.vendorID);
    }

    void Device::teardown() {
        if (tornDown_) return;

        if (device != VK_NULL_HANDLE) {
            vkDestroyDevice(device, nullptr);
            device = VK_NULL_HANDLE;
        }

        tornDown_ = true;
    }

    // -------------------------------------------------------------------------------

    Buffer::Buffer(Device &device, uint64_t sizeBytes, bool deviceLocal)
        : device(device),
          commandPool(VK_NULL_HANDLE),
          commandBuffer(VK_NULL_HANDLE),
          memory(VK_NULL_HANDLE),
          buffer(VK_NULL_HANDLE),
          size(sizeBytes),
          deviceLocal(deviceLocal),
          tornDown_(false) {
        if (sizeBytes == 0) {
            throw std::invalid_argument("Buffer size cannot be zero");
        }

        // Create VkBuffer
        VkMemoryPropertyFlags memProp = deviceLocal ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT : VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

        createVkBuffer(&buffer, &memory, sizeBytes, usage, memProp);

        // Create command pool for copy commands
        VkCommandPoolCreateInfo commandPoolCreateInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queueFamilyIndex = device.computeFamilyId
        };
        VK_CHECK(vkCreateCommandPool(device.device, &commandPoolCreateInfo, nullptr, &commandPool));

        // Allocate command buffer from command pool
        VkCommandBufferAllocateInfo commandBufferAllocInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1
        };
        VK_CHECK(vkAllocateCommandBuffers(device.device, &commandBufferAllocInfo, &commandBuffer));
    }

    Buffer::Buffer(Buffer &&other) noexcept
        : device(other.device),
          commandPool(other.commandPool),
          commandBuffer(other.commandBuffer),
          memory(other.memory),
          buffer(other.buffer),
          size(other.size),
          deviceLocal(other.deviceLocal),
          tornDown_(other.tornDown_) {
        // Reset other object to prevent double cleanup
        other.commandPool = VK_NULL_HANDLE;
        other.commandBuffer = VK_NULL_HANDLE;
        other.memory = VK_NULL_HANDLE;
        other.buffer = VK_NULL_HANDLE;
        other.tornDown_ = true;
    }

    Buffer &Buffer::operator=(Buffer &&other) noexcept {
        if (this != &other) {
            // Cleanup current resources
            teardown();

            // Transfer ownership from other
            commandPool = other.commandPool;
            commandBuffer = other.commandBuffer;
            memory = other.memory;
            buffer = other.buffer;
            size = other.size;
            deviceLocal = other.deviceLocal;
            tornDown_ = other.tornDown_;

            // Reset other object
            other.commandPool = VK_NULL_HANDLE;
            other.commandBuffer = VK_NULL_HANDLE;
            other.memory = VK_NULL_HANDLE;
            other.buffer = VK_NULL_HANDLE;
            other.tornDown_ = true;
        }
        return *this;
    }

    Buffer::~Buffer() {
        if (!tornDown_) {
            teardown();
        }
    }

    void Buffer::validateRange(uint64_t offset, uint64_t len, const char *operation) const {
        if (offset + len > size || len == 0) {
            throw std::out_of_range(std::string("Buffer ") + operation + " out of bounds: offset=" +
                std::to_string(offset) + " len=" + std::to_string(len) + " size=" + std::to_string(size));
        }
    }

    void Buffer::createVkBuffer(VkBuffer *buf, VkDeviceMemory *mem, uint64_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props) {
        // Creating VkBuffer
        VkBufferCreateInfo bufferInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .size = size,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr
        };
        VK_CHECK(vkCreateBuffer(device.device, &bufferInfo, nullptr, buf));

        // Allocating memory to buffer
        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(device.device, *buf, &memReqs);

        VkMemoryAllocateInfo allocInfo{
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = nullptr,
            .allocationSize = memReqs.size,
            .memoryTypeIndex = device.selectMemory(memReqs.memoryTypeBits, props)
        };
        VK_CHECK(vkAllocateMemory(device.device, &allocInfo, nullptr, mem));
        VK_CHECK(vkBindBufferMemory(device.device, *buf, *mem, 0));
    }

    void Buffer::teardown() {
        if (tornDown_) return;

        if (commandBuffer != VK_NULL_HANDLE && commandPool != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(device.device, commandPool, 1, &commandBuffer);
            commandBuffer = VK_NULL_HANDLE;
        }

        if (commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device.device, commandPool, nullptr);
            commandPool = VK_NULL_HANDLE;
        }

        if (memory != VK_NULL_HANDLE) {
            vkFreeMemory(device.device, memory, nullptr);
            memory = VK_NULL_HANDLE;
        }

        if (buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device.device, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
        }

        tornDown_ = true;
    }

    void Buffer::copyInternal(VkBuffer src, VkBuffer dst, uint64_t len, uint64_t srcOffset, uint64_t dstOffset) {
        // Begin recording command buffer, record command to copy buffer to buffer, end command buffer record
        VkCommandBufferBeginInfo beginInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr
        };
        VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

        VkBufferCopy copyRegion{
            .srcOffset = srcOffset,
            .dstOffset = dstOffset,
            .size = len
        };
        vkCmdCopyBuffer(commandBuffer, src, dst, 1, &copyRegion);
        VK_CHECK(vkEndCommandBuffer(commandBuffer));

        // Submit command buffer to queue, wait for completion
        VkSubmitInfo submitInfo{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = nullptr,
            .waitSemaphoreCount = 0,
            .pWaitSemaphores = nullptr,
            .pWaitDstStageMask = nullptr,
            .commandBufferCount = 1,
            .pCommandBuffers = &commandBuffer,
            .signalSemaphoreCount = 0,
            .pSignalSemaphores = nullptr
        };
        VK_CHECK(vkQueueSubmit(device.computeQueue, 1, &submitInfo, VK_NULL_HANDLE));
        VK_CHECK(vkQueueWaitIdle(device.computeQueue));

        // Reset command pool (and all buffers in it) for next use
        VK_CHECK(vkResetCommandPool(device.device, commandPool, 0));
    }

    void Buffer::copy(Buffer &dst, uint64_t len, uint64_t srcOffset, uint64_t dstOffset) {
        validateRange(srcOffset, len, "copy source");
        dst.validateRange(dstOffset, len, "copy destination");
        copyInternal(buffer, dst.buffer, len, srcOffset, dstOffset);
    }

    void Buffer::store(const void *src, uint64_t len, uint64_t srcOffset, uint64_t dstOffset) {
        if (src == nullptr) {
            throw std::invalid_argument("Source pointer cannot be null");
        }
        validateRange(dstOffset, len, "store");

        if (deviceLocal) {
            // Allocate staging buffer of copy size
            VkBuffer staging;
            VkDeviceMemory stagingMemory;
            createVkBuffer(&staging, &stagingMemory, len, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

            // Copy src region to staging buffer region
            void *stagingPtr;
            VK_CHECK(vkMapMemory(device.device, stagingMemory, 0, len, 0, &stagingPtr));
            memcpy(stagingPtr, static_cast<const char *>(src) + srcOffset, len);
            vkUnmapMemory(device.device, stagingMemory);

            // Copy staging buffer region to device local buffer region
            copyInternal(staging, buffer, len, 0, dstOffset);

            // Free staging buffer
            vkFreeMemory(device.device, stagingMemory, nullptr);
            vkDestroyBuffer(device.device, staging, nullptr);
        } else {
            // Map host visible buffer, copy memory, unmap
            void *bufferPtr;
            VK_CHECK(vkMapMemory(device.device, memory, dstOffset, len, 0, &bufferPtr));
            memcpy(bufferPtr, static_cast<const char *>(src) + srcOffset, len);
            vkUnmapMemory(device.device, memory);
        }
    }

    void Buffer::load(void *dst, uint64_t len, uint64_t srcOffset, uint64_t dstOffset) {
        if (dst == nullptr) {
            throw std::invalid_argument("Destination pointer cannot be null");
        }
        validateRange(srcOffset, len, "load");

        if (deviceLocal) {
            VkBuffer staging;
            VkDeviceMemory stagingMemory;
            createVkBuffer(&staging, &stagingMemory, len, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

            // Copy device local buffer region to staging buffer region
            copyInternal(buffer, staging, len, srcOffset, 0);

            void *stagingPtr;
            VK_CHECK(vkMapMemory(device.device, stagingMemory, 0, len, 0, &stagingPtr));
            memcpy(static_cast<char *>(dst) + dstOffset, static_cast<char *>(stagingPtr), len);
            vkUnmapMemory(device.device, stagingMemory);

            // Free staging buffer
            vkFreeMemory(device.device, stagingMemory, nullptr);
            vkDestroyBuffer(device.device, staging, nullptr);
        } else {
            // Map host visible buffer, copy memory, unmap
            void *bufferPtr;
            VK_CHECK(vkMapMemory(device.device, memory, srcOffset, len, 0, &bufferPtr));
            memcpy(static_cast<char *>(dst) + dstOffset, static_cast<char *>(bufferPtr), len);
            vkUnmapMemory(device.device, memory);
        }
    }

    void Buffer::fill(uint32_t word, uint64_t offset) {
        validateRange(offset, size - offset, "fill");

        // Begin command buffer, encode commands to fill buffer and staging buffer with word
        VkCommandBufferBeginInfo beginInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr
        };
        VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));
        vkCmdFillBuffer(commandBuffer, buffer, offset, size - offset, word);
        VK_CHECK(vkEndCommandBuffer(commandBuffer));

        // Submit command buffer to queue, wait for completion
        VkSubmitInfo submitInfo{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = nullptr,
            .waitSemaphoreCount = 0,
            .pWaitSemaphores = nullptr,
            .pWaitDstStageMask = nullptr,
            .commandBufferCount = 1,
            .pCommandBuffers = &commandBuffer,
            .signalSemaphoreCount = 0,
            .pSignalSemaphores = nullptr
        };
        VK_CHECK(vkQueueSubmit(device.computeQueue, 1, &submitInfo, VK_NULL_HANDLE));
        VK_CHECK(vkQueueWaitIdle(device.computeQueue));

        // Reset command pool (and all buffers in it) for next use
        VK_CHECK(vkResetCommandPool(device.device, commandPool, 0));
    }

    void Buffer::clear() {
        fill(0);
    }

    // -------------------------------------------------------------------------------

    std::vector<uint32_t> read_spirv(const char *filename) {
        auto fin = std::ifstream(filename, std::ios::binary | std::ios::ate);
        if (!fin.is_open()) {
            throw std::runtime_error(std::string("failed opening file ") + filename + " for reading");
        }
        const auto stream_size = static_cast<unsigned>(fin.tellg());
        fin.seekg(0);

        auto ret = std::vector<std::uint32_t>((stream_size + 3) / 4, 0);
        std::copy(std::istreambuf_iterator<char>(fin), std::istreambuf_iterator<char>(), reinterpret_cast<char *>(ret.data()));
        return ret;
    }

    VkShaderModule initShaderModule(Device &device, const std::vector<uint32_t> &spvCode) {
        VkShaderModuleCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .codeSize = spvCode.size() * sizeof(uint32_t),
            .pCode = spvCode.data()
        };

        VkShaderModule shaderModule;
        VK_CHECK(vkCreateShaderModule(device.device, &createInfo, nullptr, &shaderModule));
        return shaderModule;
    }

    VkShaderModule initShaderModule(Device &device, const char *filepath) {
        std::vector<uint32_t> code = read_spirv(filepath);
        // Create shader module with spv code
        return initShaderModule(device, code);
    }

    VkDescriptorSetLayout createDescriptorSetLayout(Device &device, uint32_t size) {
        std::vector<VkDescriptorSetLayoutBinding> layouts;
        // Create descriptor set with binding
        for (uint32_t i = 0; i < size; i++) {
            layouts.push_back(VkDescriptorSetLayoutBinding{
                .binding = i,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                .pImmutableSamplers = nullptr
            });
        }

        // Define descriptor set layout info
        VkDescriptorSetLayoutCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .bindingCount = size,
            .pBindings = layouts.data()
        };

        VkDescriptorSetLayout descriptorSetLayout;
        VK_CHECK(vkCreateDescriptorSetLayout(device.device, &createInfo, nullptr, &descriptorSetLayout));
        return descriptorSetLayout;
    }

    // This function brings descriptorSet, buffers, and bufferInfo to create writeDescriptorSets,
    // which describes a descriptor set write operation
    void writeSets(VkDescriptorSet &descriptorSet,
                   std::vector<Buffer> &buffers,
                   std::vector<VkWriteDescriptorSet> &writeDescriptorSets,
                   std::vector<VkDescriptorBufferInfo> &bufferInfos) {
        bufferInfos.clear();
        writeDescriptorSets.clear();

        // Define descriptor buffer info
        for (size_t i = 0; i < buffers.size(); i++) {
            bufferInfos.push_back(VkDescriptorBufferInfo{
                .buffer = buffers[i].buffer,
                .offset = 0,
                .range = VK_WHOLE_SIZE
            });
        }

        // wow this bug sucked: https://medium.com/@arpytoth/the-dangerous-pointer-to-vector-a139cc42a192
        for (size_t i = 0; i < buffers.size(); i++) {
            writeDescriptorSets.push_back(VkWriteDescriptorSet{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext = nullptr,
                .dstSet = descriptorSet,
                .dstBinding = static_cast<uint32_t>(i),
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pImageInfo = nullptr,
                .pBufferInfo = &bufferInfos[i],
                .pTexelBufferView = nullptr
            });
        }
    }

    Program::Program(Device &device, const char *filepath, std::vector<Buffer> &buffers,
                     uint32_t pushConstantSizeBytes)
        : buffers_(buffers),
          shaderModule_(initShaderModule(device, filepath)),
          device_(device),
          descriptorSetLayout_(VK_NULL_HANDLE),
          descriptorPool_(VK_NULL_HANDLE),
          descriptorSet_(VK_NULL_HANDLE),
          pipelineLayout_(VK_NULL_HANDLE),
          pipeline_(VK_NULL_HANDLE),
          commandPool_(VK_NULL_HANDLE),
          numWorkgroups_(1),
          workgroupSize_(1),
          pushConstantSizeBytes_(pushConstantSizeBytes),
          fence_(VK_NULL_HANDLE),
          commandBuffer_(VK_NULL_HANDLE),
          timestampQueryPool_(VK_NULL_HANDLE),
          initialized_(false),
          tornDown_(false) {
    }

    Program::Program(Device &device, const std::vector<uint32_t> &spvCode, std::vector<Buffer> &buffers,
                     uint32_t pushConstantSizeBytes)
        : buffers_(buffers),
          shaderModule_(initShaderModule(device, spvCode)),
          device_(device),
          descriptorSetLayout_(VK_NULL_HANDLE),
          descriptorPool_(VK_NULL_HANDLE),
          descriptorSet_(VK_NULL_HANDLE),
          pipelineLayout_(VK_NULL_HANDLE),
          pipeline_(VK_NULL_HANDLE),
          commandPool_(VK_NULL_HANDLE),
          numWorkgroups_(1),
          workgroupSize_(1),
          pushConstantSizeBytes_(pushConstantSizeBytes),
          fence_(VK_NULL_HANDLE),
          commandBuffer_(VK_NULL_HANDLE),
          timestampQueryPool_(VK_NULL_HANDLE),
          initialized_(false),
          tornDown_(false) {
    }

    Program::Program(Program &&other) noexcept
        : buffers_(other.buffers_),
          workgroupMemoryLengths_(std::move(other.workgroupMemoryLengths_)),
          shaderModule_(other.shaderModule_),
          device_(other.device_),
          descriptorSetLayout_(other.descriptorSetLayout_),
          descriptorPool_(other.descriptorPool_),
          descriptorSet_(other.descriptorSet_),
          writeDescriptorSets_(std::move(other.writeDescriptorSets_)),
          bufferInfos_(std::move(other.bufferInfos_)),
          pipelineLayout_(other.pipelineLayout_),
          pipeline_(other.pipeline_),
          commandPool_(other.commandPool_),
          numWorkgroups_(other.numWorkgroups_),
          workgroupSize_(other.workgroupSize_),
          pushConstantSizeBytes_(other.pushConstantSizeBytes_),
          fence_(other.fence_),
          commandBuffer_(other.commandBuffer_),
          timestampQueryPool_(other.timestampQueryPool_),
          initialized_(other.initialized_),
          tornDown_(other.tornDown_) {
        // Reset other object to prevent double cleanup
        other.shaderModule_ = VK_NULL_HANDLE;
        other.descriptorSetLayout_ = VK_NULL_HANDLE;
        other.descriptorPool_ = VK_NULL_HANDLE;
        other.descriptorSet_ = VK_NULL_HANDLE;
        other.pipelineLayout_ = VK_NULL_HANDLE;
        other.pipeline_ = VK_NULL_HANDLE;
        other.commandPool_ = VK_NULL_HANDLE;
        other.fence_ = VK_NULL_HANDLE;
        other.commandBuffer_ = VK_NULL_HANDLE;
        other.timestampQueryPool_ = VK_NULL_HANDLE;
        other.initialized_ = false;
        other.tornDown_ = true;
    }

    Program &Program::operator=(Program &&other) noexcept {
        if (this != &other) {
            // Cleanup current resources
            teardown();

            // Transfer ownership from other
            workgroupMemoryLengths_ = std::move(other.workgroupMemoryLengths_);
            shaderModule_ = other.shaderModule_;
            descriptorSetLayout_ = other.descriptorSetLayout_;
            descriptorPool_ = other.descriptorPool_;
            descriptorSet_ = other.descriptorSet_;
            writeDescriptorSets_ = std::move(other.writeDescriptorSets_);
            bufferInfos_ = std::move(other.bufferInfos_);
            pipelineLayout_ = other.pipelineLayout_;
            pipeline_ = other.pipeline_;
            commandPool_ = other.commandPool_;
            numWorkgroups_ = other.numWorkgroups_;
            workgroupSize_ = other.workgroupSize_;
            pushConstantSizeBytes_ = other.pushConstantSizeBytes_;
            fence_ = other.fence_;
            commandBuffer_ = other.commandBuffer_;
            timestampQueryPool_ = other.timestampQueryPool_;
            initialized_ = other.initialized_;
            tornDown_ = other.tornDown_;

            // Reset other object
            other.shaderModule_ = VK_NULL_HANDLE;
            other.descriptorSetLayout_ = VK_NULL_HANDLE;
            other.descriptorPool_ = VK_NULL_HANDLE;
            other.descriptorSet_ = VK_NULL_HANDLE;
            other.pipelineLayout_ = VK_NULL_HANDLE;
            other.pipeline_ = VK_NULL_HANDLE;
            other.commandPool_ = VK_NULL_HANDLE;
            other.fence_ = VK_NULL_HANDLE;
            other.commandBuffer_ = VK_NULL_HANDLE;
            other.timestampQueryPool_ = VK_NULL_HANDLE;
            other.initialized_ = false;
            other.tornDown_ = true;
        }
        return *this;
    }

    Program::~Program() {
        if (!tornDown_) {
            teardown();
        }
    }

    void Program::initialize(const char *entryPoint, VkPipelineShaderStageCreateFlags pipelineFlags) {
        if (initialized_) {
            throw std::runtime_error("Program already initialized");
        }

        descriptorSetLayout_ = createDescriptorSetLayout(device_, static_cast<uint32_t>(buffers_.size()));

        // Define pipeline layout info
        VkPushConstantRange pushConstantRange{
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .offset = 0,
            .size = pushConstantSizeBytes_
        };

        VkPipelineLayoutCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .setLayoutCount = 1,
            .pSetLayouts = &descriptorSetLayout_,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &pushConstantRange
        };

        // Create a new pipeline layout object
        VK_CHECK(vkCreatePipelineLayout(device_.device, &createInfo, nullptr, &pipelineLayout_));

        // Define descriptor pool size
        VkDescriptorPoolSize poolSize{
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = static_cast<uint32_t>(buffers_.size())
        };

        // Create a new descriptor pool object
        VkDescriptorPoolCreateInfo descriptorPoolCreateInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .maxSets = 1,
            .poolSizeCount = 1,
            .pPoolSizes = &poolSize
        };
        VK_CHECK(vkCreateDescriptorPool(device_.device, &descriptorPoolCreateInfo, nullptr, &descriptorPool_));

        // Allocate descriptor set
        VkDescriptorSetAllocateInfo descriptorSetAllocateInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext = nullptr,
            .descriptorPool = descriptorPool_,
            .descriptorSetCount = 1,
            .pSetLayouts = &descriptorSetLayout_
        };
        VK_CHECK(vkAllocateDescriptorSets(device_.device, &descriptorSetAllocateInfo, &descriptorSet_));

        writeSets(descriptorSet_, buffers_, writeDescriptorSets_, bufferInfos_);

        // Update contents of descriptor set object
        vkUpdateDescriptorSets(device_.device, static_cast<uint32_t>(writeDescriptorSets_.size()), writeDescriptorSets_.data(), 0, nullptr);

        uint32_t numSpecConstants = 3 + static_cast<uint32_t>(workgroupMemoryLengths_.size());
        std::vector<VkSpecializationMapEntry> specMap(numSpecConstants);
        std::vector<uint32_t> specMapContent(numSpecConstants);

        // First three specialization constants are the workgroup size
        specMap[0] = VkSpecializationMapEntry{0, 0, sizeof(uint32_t)};
        specMapContent[0] = workgroupSize_;
        specMap[1] = VkSpecializationMapEntry{1, 4, sizeof(uint32_t)};
        specMapContent[1] = 1;
        specMap[2] = VkSpecializationMapEntry{2, 8, sizeof(uint32_t)};
        specMapContent[2] = 1;

        // Key is index, value is length
        for (const auto &[key, value] : workgroupMemoryLengths_) {
            specMap[3 + key] = VkSpecializationMapEntry{3 + key, (3 + key) * 4, sizeof(uint32_t)};
            specMapContent[3 + key] = value;
        }

        VkSpecializationInfo specInfo{
            .mapEntryCount = numSpecConstants,
            .pMapEntries = specMap.data(),
            .dataSize = numSpecConstants * sizeof(uint32_t),
            .pData = specMapContent.data()
        };

        // Define shader stage create info
        VkPipelineShaderStageCreateInfo stageCI{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = pipelineFlags,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = shaderModule_,
            .pName = entryPoint,
            .pSpecializationInfo = &specInfo
        };

        // Define compute pipeline create info
        VkComputePipelineCreateInfo pipelineCI{
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = stageCI,
            .layout = pipelineLayout_,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = 0
        };

        // Create compute pipelines
        VK_CHECK(vkCreateComputePipelines(device_.device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &pipeline_));

        // Create fence
        VkFenceCreateInfo fenceCreateInfo{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0
        };
        VK_CHECK(vkCreateFence(device_.device, &fenceCreateInfo, nullptr, &fence_));

        // Define command pool info
        VkCommandPoolCreateInfo commandPoolCreateInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = device_.computeFamilyId
        };

        // Create command pool
        VK_CHECK(vkCreateCommandPool(device_.device, &commandPoolCreateInfo, nullptr, &commandPool_));

        // Define command buffer info
        VkCommandBufferAllocateInfo commandBufferAI{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = commandPool_,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1
        };

        // Allocate command buffers
        VK_CHECK(vkAllocateCommandBuffers(device_.device, &commandBufferAI, &commandBuffer_));

        // Create timestamp query pool
        // TODO: Device support limits need to be queried.
        VkQueryPoolCreateInfo queryPoolCreateInfo{
            .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queryType = VK_QUERY_TYPE_TIMESTAMP,
            .queryCount = 2,
            .pipelineStatistics = 0
        };
        VK_CHECK(vkCreateQueryPool(device_.device, &queryPoolCreateInfo, nullptr, &timestampQueryPool_));

        initialized_ = true;
    }

    std::vector<ShaderStatistics> Program::getShaderStats() {
        if (!initialized_) {
            throw std::runtime_error("Program not initialized");
        }

        std::vector<ShaderStatistics> stats;
        if (device_.supportsAMDShaderStats) {
            VkShaderStatisticsInfoAMD statInfo = {};
            size_t infoSize = sizeof(statInfo);
            auto pfnGetShaderInfoAMD = (PFN_vkGetShaderInfoAMD) vkGetDeviceProcAddr(device_.device, "vkGetShaderInfoAMD");
            VK_CHECK(pfnGetShaderInfoAMD(
                device_.device,
                pipeline_,
                VK_SHADER_STAGE_COMPUTE_BIT,
                VK_SHADER_INFO_TYPE_STATISTICS_AMD,
                &infoSize,
                &statInfo));
            stats.push_back(ShaderStatistics{"Physical Vgprs", "Physical vector general purpose registers", 2, statInfo.numPhysicalVgprs});
            stats.push_back(ShaderStatistics{"Physical Sgprs", "Physical scalar general purpose registers", 2, statInfo.numPhysicalSgprs});
            stats.push_back(ShaderStatistics{"Compiler Vgprs", "Compiler vector general purpose registers", 2, statInfo.numAvailableVgprs});
            stats.push_back(ShaderStatistics{"Compiler Sgprs", "Compiler scalar general purpose registers", 2, statInfo.numAvailableSgprs});
        }

        // We assume there is only one executable (e.g. shader) associated with this pipeline, or at least, the first one is the one we want stats for
        VkPipelineExecutableInfoKHR pExecutableInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR,
            .pNext = nullptr,
            .pipeline = pipeline_,
            .executableIndex = 0
        };
        auto pfnGetPipelineExecutableStatistics = (PFN_vkGetPipelineExecutableStatisticsKHR) vkGetDeviceProcAddr(device_.device, "vkGetPipelineExecutableStatisticsKHR");
        uint32_t executableCount = 0;
        // Get the count of statistics
        pfnGetPipelineExecutableStatistics(device_.device, &pExecutableInfo, &executableCount, nullptr);
        std::vector<VkPipelineExecutableStatisticKHR> statistics(executableCount, {VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR});
        // Get the actual statistics
        pfnGetPipelineExecutableStatistics(device_.device, &pExecutableInfo, &executableCount, statistics.data());

        // Output statistics
        for (const auto &stat : statistics) {
            switch (stat.format) {
            case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR:
                stats.push_back(ShaderStatistics{stat.name, stat.description, 0, stat.value.b32});
                break;
            case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR:
                stats.push_back(ShaderStatistics{stat.name, stat.description, 1, static_cast<uint64_t>(stat.value.i64)});
                break;
            case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR:
                stats.push_back(ShaderStatistics{stat.name, stat.description, 2, stat.value.u64});
                break;
            case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_FLOAT64_KHR:
                stats.push_back(ShaderStatistics{stat.name, stat.description, 3, static_cast<uint64_t>(stat.value.f64)});
                break;
            default:
                break;
            }
        }
        return stats;
    }

    void Program::run() {
        if (!initialized_) {
            throw std::runtime_error("Program not initialized");
        }

        // Start recording command buffer
        VkCommandBufferBeginInfo beginInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = 0,
            .pInheritanceInfo = nullptr
        };
        VK_CHECK(vkBeginCommandBuffer(commandBuffer_, &beginInfo));

        // Bind pipeline and descriptor sets
        vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
        vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipelineLayout_, 0, 1, &descriptorSet_, 0, nullptr);

        // Bind push constants
        uint32_t pValues[3] = {0, 0, 0};
        vkCmdPushConstants(commandBuffer_, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, pushConstantSizeBytes_, &pValues);

        VkMemoryBarrier barrier{
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_HOST_READ_BIT
        };
        vkCmdPipelineBarrier(commandBuffer_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,
                             1, &barrier, 0, nullptr, 0, nullptr);

        // Dispatch compute work items
        vkCmdDispatch(commandBuffer_, numWorkgroups_, 1, 1);

        vkCmdPipelineBarrier(commandBuffer_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,
                             1, &barrier, 0, nullptr, 0, nullptr);

        // End recording command buffer
        VK_CHECK(vkEndCommandBuffer(commandBuffer_));

        // Define submit info
        VkSubmitInfo submitInfo{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = nullptr,
            .waitSemaphoreCount = 0,
            .pWaitSemaphores = nullptr,
            .pWaitDstStageMask = nullptr,
            .commandBufferCount = 1,
            .pCommandBuffers = &commandBuffer_,
            .signalSemaphoreCount = 0,
            .pSignalSemaphores = nullptr
        };

        // Submit command buffer to queue, signals fence on completion
        VK_CHECK(vkQueueSubmit(device_.computeQueue, 1, &submitInfo, fence_));
        // Wait on fence
        VK_CHECK(vkWaitForFences(device_.device, 1, &fence_, VK_TRUE, UINT64_MAX));
        // Reset fence signal
        VK_CHECK(vkResetFences(device_.device, 1, &fence_));
    }

    float Program::runWithDispatchTiming() {
        if (!initialized_) {
            throw std::runtime_error("Program not initialized");
        }

        // Start recording command buffer
        VkCommandBufferBeginInfo beginInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = 0,
            .pInheritanceInfo = nullptr
        };
        VK_CHECK(vkBeginCommandBuffer(commandBuffer_, &beginInfo));

        // Reset query pool
        vkCmdResetQueryPool(commandBuffer_, timestampQueryPool_, 0, 2);

        // Bind pipeline and descriptor sets
        vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
        vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipelineLayout_, 0, 1, &descriptorSet_, 0, nullptr);

        // Bind push constants
        uint32_t pValues[3] = {0, 0, 0};
        vkCmdPushConstants(commandBuffer_, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, pushConstantSizeBytes_,
                           &pValues);

        VkMemoryBarrier barrier{
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_HOST_READ_BIT
        };
        vkCmdPipelineBarrier(commandBuffer_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,
                             1, &barrier, 0, nullptr, 0, nullptr);

        // Write first timestamp
        vkCmdWriteTimestamp(commandBuffer_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, timestampQueryPool_, 0);

        // Dispatch compute work items
        vkCmdDispatch(commandBuffer_, numWorkgroups_, 1, 1);

        vkCmdPipelineBarrier(commandBuffer_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,
                             1, &barrier, 0, nullptr, 0, nullptr);

        // Write second timestamp
        vkCmdWriteTimestamp(commandBuffer_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, timestampQueryPool_, 1);

        // End recording command buffer
        VK_CHECK(vkEndCommandBuffer(commandBuffer_));

        // Define submit info
        VkSubmitInfo submitInfo{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = nullptr,
            .waitSemaphoreCount = 0,
            .pWaitSemaphores = nullptr,
            .pWaitDstStageMask = nullptr,
            .commandBufferCount = 1,
            .pCommandBuffers = &commandBuffer_,
            .signalSemaphoreCount = 0,
            .pSignalSemaphores = nullptr
        };

        // Submit command buffer to queue, signals fence on completion
        VK_CHECK(vkQueueSubmit(device_.computeQueue, 1, &submitInfo, fence_));
        // Wait on fence
        VK_CHECK(vkWaitForFences(device_.device, 1, &fence_, VK_TRUE, UINT64_MAX));
        // Reset fence signal
        VK_CHECK(vkResetFences(device_.device, 1, &fence_));

        // Get timestamp query results
        uint64_t queryResults[2] = {0, 0};
        VK_CHECK(vkGetQueryPoolResults(
            device_.device,
            timestampQueryPool_,
            0,
            2,
            sizeof(uint64_t) * 2,
            queryResults,
            sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));

        return static_cast<float>(queryResults[1] - queryResults[0]) * device_.properties.limits.timestampPeriod;
    }

    void Program::setWorkgroups(uint32_t numWorkgroups) {
        numWorkgroups_ = numWorkgroups;
    }

    void Program::setWorkgroupSize(uint32_t workgroupSize) {
        workgroupSize_ = workgroupSize;
    }

    void Program::setWorkgroupMemoryLength(uint32_t length, uint32_t index) {
        workgroupMemoryLengths_[index] = length;
    }

    void Program::teardown() {
        if (tornDown_) return;

        if (shaderModule_ != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_.device, shaderModule_, nullptr);
            shaderModule_ = VK_NULL_HANDLE;
        }
        if (descriptorPool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device_.device, descriptorPool_, nullptr);
            descriptorPool_ = VK_NULL_HANDLE;
        }
        if (descriptorSetLayout_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device_.device, descriptorSetLayout_, nullptr);
            descriptorSetLayout_ = VK_NULL_HANDLE;
        }
        if (pipelineLayout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device_.device, pipelineLayout_, nullptr);
            pipelineLayout_ = VK_NULL_HANDLE;
        }
        if (pipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_.device, pipeline_, nullptr);
            pipeline_ = VK_NULL_HANDLE;
        }
        if (fence_ != VK_NULL_HANDLE) {
            vkDestroyFence(device_.device, fence_, nullptr);
            fence_ = VK_NULL_HANDLE;
        }
        if (commandBuffer_ != VK_NULL_HANDLE && commandPool_ != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(device_.device, commandPool_, 1, &commandBuffer_);
            commandBuffer_ = VK_NULL_HANDLE;
        }
        if (commandPool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_.device, commandPool_, nullptr);
            commandPool_ = VK_NULL_HANDLE;
        }
        if (timestampQueryPool_ != VK_NULL_HANDLE) {
            vkDestroyQueryPool(device_.device, timestampQueryPool_, nullptr);
            timestampQueryPool_ = VK_NULL_HANDLE;
        }

        tornDown_ = true;
    }
}
