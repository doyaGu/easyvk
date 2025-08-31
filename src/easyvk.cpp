/*
   Copyright 2023 Reese Levine, Devon McKee, Sean Siddens

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
#include <algorithm>
#include <thread>
#include <map>

#ifdef __ANDROID__
#include <android/log.h>
#endif

#ifdef EASYVK_USE_SPIRV_TOOLS
#include <spirv-tools/libspirv.hpp>
#endif

// Simple logging function
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

namespace easyvk {
    // -------- VkResult to string conversion -------------------------------------
    const char *vkResultString(VkResult res) {
        switch (res) {
        // 1.0
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_EVENT_SET: return "VK_EVENT_SET";
        case VK_EVENT_RESET: return "VK_EVENT_RESET";
        case VK_INCOMPLETE: return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
        case VK_ERROR_UNKNOWN: return "VK_ERROR_UNKNOWN";
        // 1.1
        case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY";
        case VK_ERROR_INVALID_EXTERNAL_HANDLE: return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
        // 1.2
        case VK_ERROR_FRAGMENTATION: return "VK_ERROR_FRAGMENTATION";
        case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS: return "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS";
        // 1.3
        case VK_PIPELINE_COMPILE_REQUIRED: return "VK_PIPELINE_COMPILE_REQUIRED";
        default: return "UNKNOWN_ERROR";
        }
    }

    // Returns readable vendor name from vendorID
    const char *vkVendorName(uint32_t vid) {
        switch (vid) {
        case 0x10DE: return "NVIDIA";
        case 0x1002: return "AMD";
        case 0x8086: return "Intel";
        case 0x106B: return "Apple";
        case 0x13B5: return "ARM";
        case 0x5143: return "Qualcomm";
        default: return "UNKNOWN";
        }
    }

    // -------- Error handling utilities ------------------------------------------
    void vkCheck(VkResult result, const char *file, int line) {
        if (result != VK_SUCCESS) {
            std::string message = "Vulkan error: " + std::string(vkResultString(result));
#ifdef EASYVK_NO_EXCEPTIONS
            evk_log("VK_CHECK failed: %s at %s:%d\n", message.c_str(), file, line);
#else
            throw VulkanError(result, message, file, line);
#endif
        }
    }

    const char *vkDeviceType(VkPhysicalDeviceType type) {
        switch (type) {
        case VK_PHYSICAL_DEVICE_TYPE_OTHER: return "VK_PHYSICAL_DEVICE_TYPE_OTHER";
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU";
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU";
        case VK_PHYSICAL_DEVICE_TYPE_CPU: return "VK_PHYSICAL_DEVICE_TYPE_CPU";
        default: return "UNKNOWN_DEVICE_TYPE";
        }
    }

    // -------- Alignment utilities -----------------------------------------------
    VkDeviceSize alignDown(VkDeviceSize value, VkDeviceSize alignment) {
        if (alignment == 0) return value;
        return (value / alignment) * alignment;
    }

    VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) {
        if (alignment == 0) return value;
        return ((value + alignment - 1) / alignment) * alignment;
    }

    // -------- Debug callback ----------------------------------------------------
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugUtilsCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageTypes,
        const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
        void *pUserData) {
        std::cerr << "\x1B[31m[Vulkan:" << pCallbackData->pMessageIdName << "]\033[0m "
            << pCallbackData->pMessage << "\n";
        return VK_FALSE;
    }

    // -------- SPIR-V validation -------------------------------------------------
    bool isValidSPIRV(const std::vector<uint32_t> &code) {
        if (code.empty()) return false;
        if (code[0] != 0x07230203) return false;
        return code.size() >= 5;
    }

#ifdef EASYVK_USE_SPIRV_TOOLS
    bool validateSPIRV(const std::vector<uint32_t> &code, std::string &errorMessage) {
        spv_target_env env = SPV_ENV_VULKAN_1_3;
        spvtools::SpirvTools tools(env);

        tools.SetMessageConsumer([&errorMessage](spv_message_level_t level, const char *source,
                                                 const spv_position_t &position, const char *message) {
            errorMessage = std::string("line ") + std::to_string(position.index) + ": " + message;
        });

        return tools.Validate(code.data(), code.size());
    }
#endif

    // -------- Instance implementation -------------------------------------------
    Instance::Instance() : Instance(InstanceCreateInfo{}) {}

    Instance::Instance(const InstanceCreateInfo &info)
        : instance_(VK_NULL_HANDLE),
          debugMessenger_(VK_NULL_HANDLE),
          validationEnabled_(info.enableValidationLayers),
          debugUtilsEnabled_(info.enableDebugUtils),
          tornDown_(false) {
        // Load global Vulkan entry points (required before any vk* global calls)
        VK_CHECK(volkInitialize());

        std::vector<const char *> enabledLayers;
        std::vector<const char *> enabledExtensions;

        auto dedupCStrs = [](std::vector<const char *> &v) {
            std::vector<const char *> out;
            for (auto *s : v) {
                bool seen = false;
                for (auto *t : out) {
                    if (std::strcmp(s, t) == 0) {
                        seen = true;
                        break;
                    }
                }
                if (!seen) out.push_back(s);
            }
            v.swap(out);
        };

        // Validation layer
        if (validationEnabled_) {
            uint32_t layerCount = 0;
            vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
            std::vector<VkLayerProperties> availableLayers(layerCount);
            vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

            bool hasValidationLayer = false;
            for (const auto &layer : availableLayers) {
                if (strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
                    hasValidationLayer = true;
                    break;
                }
            }
            if (hasValidationLayer) {
                enabledLayers.push_back("VK_LAYER_KHRONOS_validation");
            } else {
                evk_log("Warning: VK_LAYER_KHRONOS_validation not available\n");
                validationEnabled_ = false;
            }
        }

        // Enumerate instance extensions once
        uint32_t extensionCount = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data());

        bool hasDebugUtils = false;
        bool hasLayerSettings = false;
        bool hasPortabilityEnum = false;
        for (const auto &ext : availableExtensions) {
            if (strcmp(ext.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0) {
                hasDebugUtils = true;
            } else if (strcmp(ext.extensionName, "VK_EXT_layer_settings") == 0) {
                hasLayerSettings = true;
            } else if (strcmp(ext.extensionName, "VK_KHR_portability_enumeration") == 0) {
                hasPortabilityEnum = true;
            }
        }

        // Extensions
        if (debugUtilsEnabled_) {
            if (hasDebugUtils) {
                enabledExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            } else {
                evk_log("Warning: VK_EXT_debug_utils not available\n");
                debugUtilsEnabled_ = false;
            }
        }
        if (info.enableLayerSettings) {
            if (hasLayerSettings) {
                enabledExtensions.push_back("VK_EXT_layer_settings");
            } else {
                evk_log("Warning: VK_EXT_layer_settings not available\n");
            }
        }

        // Add extra extensions/layers from info
        for (const char *ext : info.extraExtensions) {
            enabledExtensions.push_back(ext);
        }
        for (const char *layer : info.extraLayers) {
            enabledLayers.push_back(layer);
        }

        // Avoid duplicate entries
        dedupCStrs(enabledExtensions);
        dedupCStrs(enabledLayers);

        // Configurable portability enumeration support
        VkInstanceCreateFlags instanceCreateFlags = 0;
        bool wantPortability =
#ifdef EASYVK_ENABLE_PORTABILITY_ENUMERATION
                true
#else
                info.enablePortabilityEnumeration
#endif
            ;

        if (wantPortability && hasPortabilityEnum) {
            enabledExtensions.push_back("VK_KHR_portability_enumeration");
            dedupCStrs(enabledExtensions);
            instanceCreateFlags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        } else if (wantPortability && !hasPortabilityEnum) {
            evk_log("Warning: VK_KHR_portability_enumeration not available on this platform\n");
        }

        VkApplicationInfo appInfo{
            VK_STRUCTURE_TYPE_APPLICATION_INFO,
            nullptr,
            info.applicationName,
            info.applicationVersion,
            "EasyVK",
            EASYVK_VERSION_MAJOR * 10000 + EASYVK_VERSION_MINOR * 100 + EASYVK_VERSION_PATCH,
            info.apiVersion
        };

        // Setup debug messenger for instance creation if validation is enabled
        VkDebugUtilsMessengerCreateInfoEXT debugUtilsCreateInfo{};
        if (debugUtilsEnabled_) {
            debugUtilsCreateInfo = {
                VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
                nullptr,
                0,
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
                VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
                debugUtilsCallback,
                nullptr
            };
        }

        // Optional layer settings chain
        VkLayerSettingsCreateInfoEXT layerSettingsCI{};
        const void *pNextHead = nullptr;
        if (info.enableLayerSettings && hasLayerSettings && !info.layerSettings.empty()) {
            layerSettingsCI = {
                VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT,
                nullptr,
                static_cast<uint32_t>(info.layerSettings.size()),
                info.layerSettings.data()
            };
            layerSettingsCI.pNext = debugUtilsEnabled_ ? &debugUtilsCreateInfo : nullptr;
            pNextHead = &layerSettingsCI;
        } else if (debugUtilsEnabled_) {
            pNextHead = &debugUtilsCreateInfo;
        }

        VkInstanceCreateInfo createInfo{
            VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            pNextHead,
            instanceCreateFlags,
            &appInfo,
            static_cast<uint32_t>(enabledLayers.size()),
            enabledLayers.data(),
            static_cast<uint32_t>(enabledExtensions.size()),
            enabledExtensions.data()
        };

        VK_CHECK(vkCreateInstance(&createInfo, nullptr, &instance_));

        // Load instance-scoped entry points (EXT loader functions included)
        volkLoadInstance(instance_);

        if (debugUtilsEnabled_) {
            VK_CHECK(vkCreateDebugUtilsMessengerEXT(instance_, &debugUtilsCreateInfo, nullptr, &debugMessenger_));
        }
    }

#ifndef EASYVK_NO_EXCEPTIONS
    Instance::Instance(bool enableValidationLayers)
        : instance_(VK_NULL_HANDLE),
          debugMessenger_(VK_NULL_HANDLE),
          validationEnabled_(false),
#if defined(EASYVK_DEFAULT_ENABLE_DEBUG_UTILS)
          debugUtilsEnabled_(true),
#else
          debugUtilsEnabled_(false),
#endif
          tornDown_(false) {
        InstanceCreateInfo info;
        info.enableValidationLayers = enableValidationLayers;
        *this = Instance(info);
    }
#endif

    Instance::Instance(Instance &&other) noexcept
        : instance_(other.instance_),
          debugMessenger_(other.debugMessenger_),
          validationEnabled_(other.validationEnabled_),
          debugUtilsEnabled_(other.debugUtilsEnabled_),
          tornDown_(other.tornDown_) {
        other.instance_ = VK_NULL_HANDLE;
        other.debugMessenger_ = VK_NULL_HANDLE;
        other.tornDown_ = true;
    }

    Instance &Instance::operator=(Instance &&other) noexcept {
        if (this != &other) {
            teardown();
            instance_ = other.instance_;
            debugMessenger_ = other.debugMessenger_;
            validationEnabled_ = other.validationEnabled_;
            debugUtilsEnabled_ = other.debugUtilsEnabled_;
            tornDown_ = other.tornDown_;

            other.instance_ = VK_NULL_HANDLE;
            other.debugMessenger_ = VK_NULL_HANDLE;
            other.tornDown_ = true;
        }
        return *this;
    }

    Instance::~Instance() noexcept {
        if (!tornDown_) {
            teardown();
        }
    }

    std::vector<VkPhysicalDevice> Instance::physicalDevices() const {
        uint32_t deviceCount = 0;
        VK_CHECK(vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr));

        std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
        VK_CHECK(vkEnumeratePhysicalDevices(instance_, &deviceCount, physicalDevices.data()));

        return physicalDevices;
    }

    void Instance::teardown() {
        if (tornDown_) return;

        if (debugUtilsEnabled_ && debugMessenger_ != VK_NULL_HANDLE) {
            vkDestroyDebugUtilsMessengerEXT(instance_, debugMessenger_, nullptr);
            debugMessenger_ = VK_NULL_HANDLE;
        }

        if (instance_ != VK_NULL_HANDLE) {
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
        }

        tornDown_ = true;
    }

    // -------- Device implementation ----------------------------------------------
    uint32_t findComputeQueueFamily(VkPhysicalDevice physicalDevice) {
        uint32_t queueFamilyPropertyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyPropertyCount, nullptr);

        std::vector<VkQueueFamilyProperties> familyProperties(queueFamilyPropertyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyPropertyCount, familyProperties.data());

        // First pass: look for dedicated compute queue
        for (uint32_t i = 0; i < familyProperties.size(); ++i) {
            if (familyProperties[i].queueCount > 0 &&
                (familyProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
                !(familyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                return i;
            }
        }

        // Fall back to any compute queue
        for (uint32_t i = 0; i < familyProperties.size(); ++i) {
            if (familyProperties[i].queueCount > 0 &&
                (familyProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
                return i;
            }
        }

        return UINT32_MAX;
    }

    VkPhysicalDevice selectBestDevice(const std::vector<VkPhysicalDevice> &devices, int preferredIndex) {
        if (devices.empty()) {
#ifdef EASYVK_NO_EXCEPTIONS
            return VK_NULL_HANDLE;
#else
            throw std::runtime_error("No physical devices available");
#endif
        }

        if (preferredIndex >= 0 && preferredIndex < static_cast<int>(devices.size())) {
            return devices[preferredIndex];
        }

        // Score devices: discrete > integrated > cpu > other
        VkPhysicalDevice bestDevice = VK_NULL_HANDLE;
        int bestScore = -1;

        for (VkPhysicalDevice device : devices) {
            if (findComputeQueueFamily(device) == UINT32_MAX) {
                continue; // No compute queue
            }

            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(device, &props);

            int score = 0;
            switch (props.deviceType) {
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: score = 3; break;
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: score = 2; break;
            case VK_PHYSICAL_DEVICE_TYPE_CPU: score = 1; break;
            default: score = 0; break;
            }

            if (score > bestScore) {
                bestScore = score;
                bestDevice = device;
            }
        }

        return bestDevice;
    }

    Device::Device(Instance &inst, const DeviceCreateInfo &info)
        : instance_(&inst),
          phys_(VK_NULL_HANDLE),
          device_(VK_NULL_HANDLE),
          queue_(VK_NULL_HANDLE),
          queueFamilyIndex_(UINT32_MAX),
          limits_(),
          transferCmdPool_(VK_NULL_HANDLE),
          robustAccessEnabled_(false),
          robustness2Enabled_(false),
          debugMarkersEnabled_(false),
          supportsTimestamps_(false),
          timestampPeriod_(0.0),
          tornDown_(false)
#ifdef EASYVK_USE_VMA
          , allocator_(VK_NULL_HANDLE)
#endif
    {
        std::vector<VkPhysicalDevice> devices = inst.physicalDevices();
        phys_ = selectBestDevice(devices, info.preferredIndex);

        if (phys_ == VK_NULL_HANDLE) {
            EVK_FAIL("No suitable physical device found");
        }

        queueFamilyIndex_ = findComputeQueueFamily(phys_);
        if (queueFamilyIndex_ == UINT32_MAX) {
            EVK_FAIL("No compute queue family found");
        }

        // Get device properties and limits
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(phys_, &props);
        limits_ = props.limits;

        // Check timestamp support
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(phys_, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(phys_, &queueFamilyCount, queueFamilies.data());

        if (queueFamilyIndex_ < queueFamilies.size()) {
            supportsTimestamps_ = (queueFamilies[queueFamilyIndex_].timestampValidBits > 0) && (props.limits.timestampPeriod > 0);
            timestampPeriod_ = static_cast<double>(props.limits.timestampPeriod);
        }

        // Check for extension support
        uint32_t deviceExtensionCount;
        vkEnumerateDeviceExtensionProperties(phys_, nullptr, &deviceExtensionCount, nullptr);
        std::vector<VkExtensionProperties> extensions(deviceExtensionCount);
        vkEnumerateDeviceExtensionProperties(phys_, nullptr, &deviceExtensionCount, extensions.data());

        std::vector<const char *> enabledExtensions;
        bool hasRobustness2 = false;

        for (const auto &extension : extensions) {
            if (strcmp(extension.extensionName, "VK_EXT_robustness2") == 0) {
                hasRobustness2 = true;
                if (info.enableRobustness2) {
                    enabledExtensions.push_back("VK_EXT_robustness2");
                }
            } else if (strcmp(extension.extensionName, "VK_KHR_portability_subset") == 0) {
                enabledExtensions.push_back("VK_KHR_portability_subset");
            } else if (strcmp(extension.extensionName, "VK_EXT_debug_marker") == 0) {
                if (info.enableDebugMarkers) {
                    enabledExtensions.push_back("VK_EXT_debug_marker");
                    debugMarkersEnabled_ = true;
                }
            } else if (strcmp(extension.extensionName, VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME) == 0) {
                enabledExtensions.push_back(VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME);
            }
        }

        // Setup device features
        VkPhysicalDeviceRobustness2FeaturesEXT robustness2Features{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT,
            nullptr,
            VK_FALSE, VK_FALSE, VK_FALSE
        };

        if (hasRobustness2 && info.enableRobustness2) {
            VkPhysicalDeviceRobustness2FeaturesEXT supported{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT,
                nullptr
            };

            VkPhysicalDeviceFeatures2 features2{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                &supported
            };

            vkGetPhysicalDeviceFeatures2(phys_, &features2);

            robustness2Features.robustBufferAccess2 = supported.robustBufferAccess2;
            robustness2Features.robustImageAccess2 = supported.robustImageAccess2;
            robustness2Features.nullDescriptor = supported.nullDescriptor;
            robustness2Enabled_ = true;
        }

        VkPhysicalDeviceFeatures deviceFeatures{};
        deviceFeatures.robustBufferAccess = info.enableRobustBufferAccess ? VK_TRUE : VK_FALSE;
        robustAccessEnabled_ = info.enableRobustBufferAccess;

        float priority = 1.0f;
        VkDeviceQueueCreateInfo queueCreateInfo{
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            nullptr,
            0,
            queueFamilyIndex_,
            1,
            &priority
        };

        VkDeviceCreateInfo deviceCreateInfo{
            VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            robustness2Enabled_ ? &robustness2Features : nullptr,
            0,
            1, &queueCreateInfo,
            0, nullptr,
            static_cast<uint32_t>(enabledExtensions.size()), enabledExtensions.data(),
            &deviceFeatures
        };

        VK_CHECK(vkCreateDevice(phys_, &deviceCreateInfo, nullptr, &device_));
        vkGetDeviceQueue(device_, queueFamilyIndex_, 0, &queue_);

        // Load device-scoped entry points (fast path, skips loader dispatch)
        volkLoadDevice(device_);

        // Create transfer command pool
        VkCommandPoolCreateInfo poolInfo{
            VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            nullptr,
            VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            queueFamilyIndex_
        };
        VK_CHECK(vkCreateCommandPool(device_, &poolInfo, nullptr, &transferCmdPool_));

#ifdef EASYVK_USE_VMA
        // Initialize VMA allocator
        VmaAllocatorCreateInfo vmaCreateInfo{};
        vmaCreateInfo.instance = inst.vk();
        vmaCreateInfo.physicalDevice = phys_;
        vmaCreateInfo.device = device_;
        vmaCreateInfo.vulkanApiVersion = info.apiVersion;

        VK_CHECK(vmaCreateAllocator(&vmaCreateInfo, &allocator_));
#endif
    }

#ifndef EASYVK_NO_EXCEPTIONS
    Device::Device(Instance &inst, int preferredIndex)
        : instance_(VK_NULL_HANDLE),
          phys_(VK_NULL_HANDLE),
          device_(VK_NULL_HANDLE),
          queue_(VK_NULL_HANDLE),
          queueFamilyIndex_(UINT32_MAX),
          limits_(),
          transferCmdPool_(VK_NULL_HANDLE),
          robustAccessEnabled_(false),
          robustness2Enabled_(false),
          debugMarkersEnabled_(false),
          supportsTimestamps_(false),
          timestampPeriod_(0.0),
          tornDown_(false) {
        DeviceCreateInfo info;
        info.preferredIndex = preferredIndex;
        *this = Device(inst, info);
    }
#endif

    Device::Device(Device &&other) noexcept
        : instance_(other.instance_),
          phys_(other.phys_),
          device_(other.device_),
          queue_(other.queue_),
          queueFamilyIndex_(other.queueFamilyIndex_),
          limits_(other.limits_),
          transferCmdPool_(other.transferCmdPool_),
          robustAccessEnabled_(other.robustAccessEnabled_),
          robustness2Enabled_(other.robustness2Enabled_),
          debugMarkersEnabled_(other.debugMarkersEnabled_),
          supportsTimestamps_(other.supportsTimestamps_),
          timestampPeriod_(other.timestampPeriod_),
          tornDown_(other.tornDown_)
#ifdef EASYVK_USE_VMA
          , allocator_(other.allocator_)
#endif
    {
        other.device_ = VK_NULL_HANDLE;
        other.queue_ = VK_NULL_HANDLE;
        other.transferCmdPool_ = VK_NULL_HANDLE;
        other.phys_ = VK_NULL_HANDLE;
        other.tornDown_ = true;
#ifdef EASYVK_USE_VMA
        other.allocator_ = VK_NULL_HANDLE;
#endif
    }

    Device &Device::operator=(Device &&other) noexcept {
        if (this != &other) {
            teardown();
            instance_ = other.instance_;
            phys_ = other.phys_;
            device_ = other.device_;
            queue_ = other.queue_;
            queueFamilyIndex_ = other.queueFamilyIndex_;
            limits_ = other.limits_;
            transferCmdPool_ = other.transferCmdPool_;
            robustAccessEnabled_ = other.robustAccessEnabled_;
            robustness2Enabled_ = other.robustness2Enabled_;
            debugMarkersEnabled_ = other.debugMarkersEnabled_;
            supportsTimestamps_ = other.supportsTimestamps_;
            timestampPeriod_ = other.timestampPeriod_;
            tornDown_ = other.tornDown_;

#ifdef EASYVK_USE_VMA
            allocator_ = other.allocator_;
            other.allocator_ = VK_NULL_HANDLE;
#endif

            other.device_ = VK_NULL_HANDLE;
            other.queue_ = VK_NULL_HANDLE;
            other.transferCmdPool_ = VK_NULL_HANDLE;
            other.phys_ = VK_NULL_HANDLE;
            other.tornDown_ = true;
        }
        return *this;
    }

    Device::~Device() noexcept {
        if (!tornDown_) {
            teardown();
        }
    }

    bool Device::wait(const SubmitHandle &h, uint64_t timeoutNs) {
        if (h.fence == VK_NULL_HANDLE) return false;
        VkResult result = vkWaitForFences(device_, 1, &h.fence, VK_TRUE, timeoutNs);

        // Consume resources to prevent leaks on success or timeout
        if (h.cmdBuf != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(device_, transferCmdPool_, 1, &h.cmdBuf);
        }
        vkDestroyFence(device_, h.fence, nullptr);

        return result == VK_SUCCESS;
    }

    uint32_t Device::selectMemory(uint32_t memoryTypeBits, VkMemoryPropertyFlags flags) {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(phys_, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((memoryTypeBits & (1u << i)) &&
                ((flags & memProperties.memoryTypes[i].propertyFlags) == flags)) {
                return i;
            }
        }
#ifdef EASYVK_NO_EXCEPTIONS
        lastError_ = "Failed to find suitable memory type";
        return UINT32_MAX;
#else
        throw std::runtime_error("Failed to find suitable memory type");
#endif
    }

    uint32_t Device::subgroupSize() const {
        VkPhysicalDeviceSubgroupProperties subgroupProperties{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES,
            nullptr
        };

        VkPhysicalDeviceProperties2 physicalDeviceProperties{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
            &subgroupProperties
        };

        vkGetPhysicalDeviceProperties2(phys_, &physicalDeviceProperties);
        return subgroupProperties.subgroupSize;
    }

    const char *Device::vendorName() const {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(phys_, &props);
        return vkVendorName(props.vendorID);
    }

    void Device::teardown() {
        if (tornDown_) return;

        if (device_ != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device_);

#ifdef EASYVK_USE_VMA
            if (allocator_ != VK_NULL_HANDLE) {
                vmaDestroyAllocator(allocator_);
                allocator_ = VK_NULL_HANDLE;
            }
#endif

            if (transferCmdPool_ != VK_NULL_HANDLE) {
                vkDestroyCommandPool(device_, transferCmdPool_, nullptr);
                transferCmdPool_ = VK_NULL_HANDLE;
            }

            vkDestroyDevice(device_, nullptr);
            device_ = VK_NULL_HANDLE;
        }

        tornDown_ = true;
    }

    // -------- BufferMapping implementation --------------------------------------
    BufferMapping::BufferMapping() : buf_(nullptr), ptr_(nullptr), offset_(0), length_(0), write_(false) {}

    BufferMapping::~BufferMapping() noexcept {
        if (buf_ && ptr_) {
            if (write_) {
                buf_->flushRange(offset_, length_);
            }
#ifdef EASYVK_USE_VMA
            if (buf_->allocation_ != VK_NULL_HANDLE) {
                vmaUnmapMemory(buf_->device_->allocator(), buf_->allocation_);
            } else
#endif
            {
                vkUnmapMemory(buf_->device_->vk(), buf_->memory_);
            }
        }
    }

    BufferMapping::BufferMapping(BufferMapping &&other) noexcept
        : buf_(other.buf_),
          ptr_(other.ptr_),
          offset_(other.offset_),
          length_(other.length_),
          write_(other.write_) {
        other.buf_ = nullptr;
        other.ptr_ = nullptr;
    }

    BufferMapping &BufferMapping::operator=(BufferMapping &&other) noexcept {
        if (this != &other) {
            // Clean up current mapping
            if (buf_ && ptr_) {
                if (write_) {
                    buf_->flushRange(offset_, length_);
                }
#ifdef EASYVK_USE_VMA
                if (buf_->allocation_ != VK_NULL_HANDLE) {
                    vmaUnmapMemory(buf_->device_->allocator(), buf_->allocation_);
                } else
#endif
                {
                    vkUnmapMemory(buf_->device_->vk(), buf_->memory_);
                }
            }

            buf_ = other.buf_;
            ptr_ = other.ptr_;
            offset_ = other.offset_;
            length_ = other.length_;
            write_ = other.write_;

            other.buf_ = nullptr;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    // -------- Buffer implementation ----------------------------------------------
    VkBufferUsageFlags bufferUsageToVk(BufferUsage usage) {
        VkBufferUsageFlags flags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        switch (usage) {
        case BufferUsage::Storage:
            flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            break;
        case BufferUsage::Uniform:
            flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            break;
        case BufferUsage::Staging:
            // Just transfer flags
            break;
        case BufferUsage::TransferSrc:
            flags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            break;
        case BufferUsage::TransferDst:
            flags = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            break;
        }
        return flags;
    }

    Buffer::Buffer(Device &dev, const BufferCreateInfo &info)
        : device_(&dev),
          buffer_(VK_NULL_HANDLE),
          memory_(VK_NULL_HANDLE),
          size_(info.sizeBytes),
          memFlags_(0),
          hostAccess_(info.host),
          tornDown_(false)
#ifdef EASYVK_USE_VMA
          , allocation_(VK_NULL_HANDLE)
#endif
    {
        if (size_ == 0) {
            EVK_FAIL("Buffer size cannot be zero");
        }

        VkBufferUsageFlags usage = bufferUsageToVk(info.usage);

        // Determine memory properties based on host access
        if (hostAccess_ == HostAccess::None) {
            memFlags_ = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        } else {
            memFlags_ = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        }

        try {
            createVkBuffer(&buffer_, &memory_, size_, usage, memFlags_);
        } catch (const std::exception &) {
            if (hostAccess_ != HostAccess::None && (memFlags_ & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
                // Fall back to cached memory
                memFlags_ = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
                try {
                    createVkBuffer(&buffer_, &memory_, size_, usage, memFlags_);
                } catch (const std::exception &) {
                    // Fall back to host-visible (non-coherent)
                    memFlags_ = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
                    createVkBuffer(&buffer_, &memory_, size_, usage, memFlags_);
                }
            } else {
                throw;
            }
        }
    }

    Buffer::Buffer(Buffer &&other) noexcept
        : device_(other.device_),
          buffer_(other.buffer_),
          memory_(other.memory_),
          size_(other.size_),
          memFlags_(other.memFlags_),
          hostAccess_(other.hostAccess_),
          tornDown_(other.tornDown_)
#ifdef EASYVK_USE_VMA
          , allocation_(other.allocation_)
#endif
    {
        other.buffer_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
        other.tornDown_ = true;
#ifdef EASYVK_USE_VMA
        other.allocation_ = VK_NULL_HANDLE;
#endif
    }

    Buffer &Buffer::operator=(Buffer &&other) noexcept {
        if (this != &other) {
            teardown();
            device_ = other.device_;
            buffer_ = other.buffer_;
            memory_ = other.memory_;
            size_ = other.size_;
            memFlags_ = other.memFlags_;
            hostAccess_ = other.hostAccess_;
            tornDown_ = other.tornDown_;

#ifdef EASYVK_USE_VMA
            allocation_ = other.allocation_;
            other.allocation_ = VK_NULL_HANDLE;
#endif

            other.buffer_ = VK_NULL_HANDLE;
            other.memory_ = VK_NULL_HANDLE;
            other.tornDown_ = true;
        }
        return *this;
    }

    Buffer::~Buffer() noexcept {
        if (!tornDown_) {
            teardown();
        }
    }

    BufferMapping Buffer::mapWrite(VkDeviceSize offsetBytes, VkDeviceSize lengthBytes) {
        if (lengthBytes == VK_WHOLE_SIZE) {
            lengthBytes = size_ - offsetBytes;
        }
        validateRange(offsetBytes, lengthBytes, "mapWrite");

        if (hostAccess_ == HostAccess::None || (hostAccess_ != HostAccess::Write && hostAccess_ != HostAccess::ReadWrite)) {
            EVK_FAIL("Buffer does not support write mapping");
        }

        // Map an aligned superset per nonCoherentAtomSize (safe for both coherent and non-coherent)
        VkDeviceSize atom = device_->limits().nonCoherentAtomSize;
        VkDeviceSize alignedOff = alignDown(offsetBytes, atom);
        VkDeviceSize alignedLen = alignUp(lengthBytes + (offsetBytes - alignedOff), atom);
        // Clamp to allocation size
        if (alignedOff + alignedLen > size_) alignedLen = size_ - alignedOff;

        void *base = nullptr;
#ifdef EASYVK_USE_VMA
        if (allocation_ != VK_NULL_HANDLE) {
            // VMA maps the entire allocation starting at offset 0
            VK_CHECK(vmaMapMemory(device_->allocator(), allocation_, &base));
            void *userPtr = static_cast<char *>(base) + offsetBytes;
            return {this, userPtr, alignedOff, alignedLen, true};
        } else
#endif
        {
            // Raw Vulkan maps [alignedOff, alignedOff+alignedLen)
            VK_CHECK(vkMapMemory(device_->vk(), memory_, alignedOff, alignedLen, 0, &base));
            void *userPtr = static_cast<char *>(base) + (offsetBytes - alignedOff);
            return {this, userPtr, alignedOff, alignedLen, true};
        }
    }

    BufferMapping Buffer::mapRead(VkDeviceSize offsetBytes, VkDeviceSize lengthBytes) {
        if (lengthBytes == VK_WHOLE_SIZE) {
            lengthBytes = size_ - offsetBytes;
        }
        validateRange(offsetBytes, lengthBytes, "mapRead");

        if (hostAccess_ == HostAccess::None ||
            (hostAccess_ != HostAccess::Read && hostAccess_ != HostAccess::ReadWrite)) {
            EVK_FAIL("Buffer does not support read mapping");
        }

        // Map an aligned superset (see spec VUs for nonCoherentAtomSize alignment)
        VkDeviceSize atom = device_->limits().nonCoherentAtomSize;
        VkDeviceSize alignedOff = alignDown(offsetBytes, atom);
        VkDeviceSize alignedLen = alignUp(lengthBytes + (offsetBytes - alignedOff), atom);
        if (alignedOff + alignedLen > size_) alignedLen = size_ - alignedOff;

        void *base = nullptr;
#ifdef EASYVK_USE_VMA
        if (allocation_ != VK_NULL_HANDLE) {
            // VMA maps the entire allocation
            VK_CHECK(vmaMapMemory(device_->allocator(), allocation_, &base));
            void *userPtr = static_cast<char *>(base) + offsetBytes;
            // For non-coherent memory: invalidate the mapped span
            if (!(memFlags_ & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
                invalidateRange(alignedOff, alignedLen);
            }
            return {this, userPtr, alignedOff, alignedLen, false};
        } else
#endif
        {
            // Raw Vulkan maps the aligned subrange
            VK_CHECK(vkMapMemory(device_->vk(), memory_, alignedOff, alignedLen, 0, &base));
            void *userPtr = static_cast<char *>(base) + (offsetBytes - alignedOff);
            if (!(memFlags_ & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
                invalidateRange(alignedOff, alignedLen);
            }
            return {this, userPtr, alignedOff, alignedLen, false};
        }
    }

    bool Buffer::copyTo(Buffer &dst, VkDeviceSize bytes, VkDeviceSize srcOffset, VkDeviceSize dstOffset) {
        validateRange(srcOffset, bytes, "copyTo source");
        dst.validateRange(dstOffset, bytes, "copyTo destination");

        VkCommandBufferAllocateInfo allocInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            nullptr,
            device_->transferCmdPool_,
            VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            1
        };

        VkCommandBuffer cmdBuf;
        VK_CHECK(vkAllocateCommandBuffers(device_->vk(), &allocInfo, &cmdBuf));

        VkCommandBufferBeginInfo beginInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            nullptr,
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            nullptr
        };
        VK_CHECK(vkBeginCommandBuffer(cmdBuf, &beginInfo));

        VkBufferCopy copyRegion{srcOffset, dstOffset, bytes};
        vkCmdCopyBuffer(cmdBuf, buffer_, dst.buffer_, 1, &copyRegion);
        VK_CHECK(vkEndCommandBuffer(cmdBuf));

        VkFence fence;
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, 0};
        VK_CHECK(vkCreateFence(device_->vk(), &fenceInfo, nullptr, &fence));

        VkSubmitInfo submitInfo{
            VK_STRUCTURE_TYPE_SUBMIT_INFO,
            nullptr,
            0, nullptr, nullptr,
            1, &cmdBuf,
            0, nullptr
        };

        VK_CHECK(vkQueueSubmit(device_->queue_, 1, &submitInfo, fence));
        VkResult result = vkWaitForFences(device_->vk(), 1, &fence, VK_TRUE, UINT64_MAX);

        vkDestroyFence(device_->vk(), fence, nullptr);
        vkFreeCommandBuffers(device_->vk(), device_->transferCmdPool_, 1, &cmdBuf);

        return result == VK_SUCCESS;
    }

    SubmitHandle Buffer::copyToAsync(Buffer &dst, VkDeviceSize bytes, VkDeviceSize srcOffset, VkDeviceSize dstOffset) {
        validateRange(srcOffset, bytes, "copyToAsync source");
        dst.validateRange(dstOffset, bytes, "copyToAsync destination");

        VkCommandBufferAllocateInfo allocInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            nullptr,
            device_->transferCmdPool_,
            VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            1
        };

        VkCommandBuffer cmdBuf;
        VK_CHECK(vkAllocateCommandBuffers(device_->vk(), &allocInfo, &cmdBuf));

        VkCommandBufferBeginInfo beginInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            nullptr,
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            nullptr
        };
        VK_CHECK(vkBeginCommandBuffer(cmdBuf, &beginInfo));

        VkBufferCopy copyRegion{srcOffset, dstOffset, bytes};
        vkCmdCopyBuffer(cmdBuf, buffer_, dst.buffer_, 1, &copyRegion);
        VK_CHECK(vkEndCommandBuffer(cmdBuf));

        VkFence fence;
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, 0};
        VK_CHECK(vkCreateFence(device_->vk(), &fenceInfo, nullptr, &fence));

        VkSubmitInfo submitInfo{
            VK_STRUCTURE_TYPE_SUBMIT_INFO,
            nullptr,
            0, nullptr, nullptr,
            1, &cmdBuf,
            0, nullptr
        };

        VK_CHECK(vkQueueSubmit(device_->queue_, 1, &submitInfo, fence));
        return SubmitHandle{fence, cmdBuf};
    }

    void Buffer::validateRange(VkDeviceSize offset, VkDeviceSize len, const char *operation) const {
        if (offset + len > size_ || len == 0) {
#ifdef EASYVK_NO_EXCEPTIONS
            lastError_ = std::string("Buffer ") + operation + " out of bounds";
#else
            throw std::out_of_range(std::string("Buffer ") + operation + " out of bounds: offset=" +
                std::to_string(offset) + " len=" + std::to_string(len) + " size=" + std::to_string(size_));
#endif
        }
    }

    void Buffer::createVkBuffer(VkBuffer *buf, VkDeviceMemory *mem, VkDeviceSize sizeBytes,
                                VkBufferUsageFlags usage, VkMemoryPropertyFlags props) {
        VkBufferCreateInfo bufferInfo{
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            nullptr,
            0,
            sizeBytes,
            usage,
            VK_SHARING_MODE_EXCLUSIVE,
            0, nullptr
        };

#ifdef EASYVK_USE_VMA
        if (device_->allocator() != VK_NULL_HANDLE) {
            VmaAllocationCreateInfo allocInfo{};
            if (hostAccess_ == HostAccess::None) {
                allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
            } else {
                allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
                allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
                if (hostAccess_ == HostAccess::Read || hostAccess_ == HostAccess::ReadWrite) {
                    allocInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
                }
            }

            VmaAllocationInfo allocationInfo;
            VK_CHECK(vmaCreateBuffer(device_->allocator(), &bufferInfo, &allocInfo,
                                   buf, &allocation_, &allocationInfo));
            *mem = VK_NULL_HANDLE; // Memory owned by VMA
            return;
        }
#endif

        // Fallback to manual allocation
        VK_CHECK(vkCreateBuffer(device_->vk(), &bufferInfo, nullptr, buf));

        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(device_->vk(), *buf, &memReqs);

        uint32_t memoryTypeIndex = device_->selectMemory(memReqs.memoryTypeBits, props);
        if (memoryTypeIndex == UINT32_MAX) {
            vkDestroyBuffer(device_->vk(), *buf, nullptr);
            EVK_FAIL("Failed to find suitable memory type");
        }

        VkMemoryAllocateInfo allocInfo{
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            nullptr,
            memReqs.size,
            memoryTypeIndex
        };
        VK_CHECK(vkAllocateMemory(device_->vk(), &allocInfo, nullptr, mem));
        VK_CHECK(vkBindBufferMemory(device_->vk(), *buf, *mem, 0));
    }

    void Buffer::flushRange(VkDeviceSize offset, VkDeviceSize sizeBytes) {
        if (memFlags_ & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) return;

#ifdef EASYVK_USE_VMA
        if (allocation_ != VK_NULL_HANDLE) {
            VK_CHECK(vmaFlushAllocation(device_->allocator(), allocation_, offset, sizeBytes));
            return;
        }
#endif

        VkDeviceSize atomSize = device_->limits().nonCoherentAtomSize;
        VkDeviceSize alignedOffset = alignDown(offset, atomSize);
        VkDeviceSize alignedSize = alignUp(sizeBytes + (offset - alignedOffset), atomSize);

        // Clamp to buffer size
        if (alignedOffset + alignedSize > size_) {
            alignedSize = size_ - alignedOffset;
        }

        VkMappedMemoryRange range{
            VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            nullptr,
            memory_,
            alignedOffset,
            alignedSize
        };
        VK_CHECK(vkFlushMappedMemoryRanges(device_->vk(), 1, &range));
    }

    void Buffer::invalidateRange(VkDeviceSize offset, VkDeviceSize sizeBytes) {
        if (memFlags_ & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) return;

#ifdef EASYVK_USE_VMA
        if (allocation_ != VK_NULL_HANDLE) {
            VK_CHECK(vmaInvalidateAllocation(device_->allocator(), allocation_, offset, sizeBytes));
            return;
        }
#endif

        VkDeviceSize atomSize = device_->limits().nonCoherentAtomSize;
        VkDeviceSize alignedOffset = alignDown(offset, atomSize);
        VkDeviceSize alignedSize = alignUp(sizeBytes + (offset - alignedOffset), atomSize);

        // Clamp to buffer size
        if (alignedOffset + alignedSize > size_) {
            alignedSize = size_ - alignedOffset;
        }

        VkMappedMemoryRange range{
            VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            nullptr,
            memory_,
            alignedOffset,
            alignedSize
        };
        VK_CHECK(vkInvalidateMappedMemoryRanges(device_->vk(), 1, &range));
    }

    void Buffer::teardown() {
        if (tornDown_) return;

#ifdef EASYVK_USE_VMA
        if (allocation_ != VK_NULL_HANDLE) {
            vmaDestroyBuffer(device_->allocator(), buffer_, allocation_);
            buffer_ = VK_NULL_HANDLE;
            allocation_ = VK_NULL_HANDLE;
        } else
#endif
        {
            if (memory_ != VK_NULL_HANDLE) {
                vkFreeMemory(device_->vk(), memory_, nullptr);
                memory_ = VK_NULL_HANDLE;
            }

            if (buffer_ != VK_NULL_HANDLE) {
                vkDestroyBuffer(device_->vk(), buffer_, nullptr);
                buffer_ = VK_NULL_HANDLE;
            }
        }

        tornDown_ = true;
    }

    // -------- ComputeBindings implementation ------------------------------------
    void ComputeBindings::addStorage(uint32_t binding, const Buffer &buf, VkDeviceSize offset, VkDeviceSize range) {
        entries.emplace_back(
            binding,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            buf.vk(),
            offset,
            range == VK_WHOLE_SIZE ? buf.size() : range
        );
    }

    void ComputeBindings::addUniform(uint32_t binding, const Buffer &buf, VkDeviceSize offset, VkDeviceSize range) {
        entries.emplace_back(
            binding,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            buf.vk(),
            offset,
            range == VK_WHOLE_SIZE ? buf.size() : range
        );
    }

    void ComputeBindings::addStorageArray(uint32_t set, uint32_t binding, const std::vector<Buffer *> &buffers) {
        std::vector<VkDescriptorBufferInfo> bufferInfos;
        bufferInfos.reserve(buffers.size());
        for (const Buffer *buf : buffers) {
            bufferInfos.push_back({buf->vk(), 0, buf->size()});
        }
        entries.emplace_back(set, binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, bufferInfos);
    }

    void ComputeBindings::addUniformArray(uint32_t set, uint32_t binding, const std::vector<Buffer *> &buffers) {
        std::vector<VkDescriptorBufferInfo> bufferInfos;
        bufferInfos.reserve(buffers.size());
        for (const Buffer *buf : buffers) {
            bufferInfos.push_back({buf->vk(), 0, buf->size()});
        }
        entries.emplace_back(set, binding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, bufferInfos);
    }

    void ComputeBindings::addStorageToSet(uint32_t set, uint32_t binding, const Buffer &buf, VkDeviceSize offset, VkDeviceSize range) {
        std::vector<VkDescriptorBufferInfo> bufferInfos;
        bufferInfos.push_back({buf.vk(), offset, range == VK_WHOLE_SIZE ? buf.size() : range});
        entries.emplace_back(set, binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, bufferInfos);
    }

    void ComputeBindings::addUniformToSet(uint32_t set, uint32_t binding, const Buffer &buf, VkDeviceSize offset, VkDeviceSize range) {
        std::vector<VkDescriptorBufferInfo> bufferInfos;
        bufferInfos.push_back({buf.vk(), offset, range == VK_WHOLE_SIZE ? buf.size() : range});
        entries.emplace_back(set, binding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, bufferInfos);
    }

    // -------- ComputeProgram implementation -------------------------------------
    ComputeProgram::ComputeProgram()
        : device_(nullptr),
          layout_(VK_NULL_HANDLE),
          pipeline_(VK_NULL_HANDLE),
          dsp_(VK_NULL_HANDLE),
          shader_(VK_NULL_HANDLE),
          cmdPool_(VK_NULL_HANDLE),
          cmdBuf_(VK_NULL_HANDLE),
          fence_(VK_NULL_HANDLE),
          timestampQueryPool_(VK_NULL_HANDLE),
          pcCapacityBytes_(0),
          groupsX_(1),
          groupsY_(1),
          groupsZ_(1),
          localX_(1),
          localY_(1),
          localZ_(1),
          initialized_(false),
          tornDown_(false),
          timestampInFlight_(false),
          lastTimestamps_() {
        lastTimestamps_[0] = lastTimestamps_[1] = 0;
    }

    ComputeProgram::ComputeProgram(Device &dev, const ComputeProgramCreateInfo &info)
        : device_(&dev),
          layout_(VK_NULL_HANDLE),
          pipeline_(VK_NULL_HANDLE),
          dsp_(VK_NULL_HANDLE),
          shader_(VK_NULL_HANDLE),
          cmdPool_(VK_NULL_HANDLE),
          cmdBuf_(VK_NULL_HANDLE),
          fence_(VK_NULL_HANDLE),
          timestampQueryPool_(VK_NULL_HANDLE),
          pcCapacityBytes_(info.pushConstantBytes),
          pcCfg_{info.pushConstantBytes, 0},
          groupsX_(1),
          groupsY_(1),
          groupsZ_(1),
          localX_(info.localX),
          localY_(info.localY),
          localZ_(info.localZ),
          initialized_(true),
          tornDown_(false),
          timestampInFlight_(false),
          lastTimestamps_() {
        lastTimestamps_[0] = lastTimestamps_[1] = 0;

        if (!info.spirv || info.spirv->empty()) {
            EVK_FAIL_VOID("SPIR-V code is required");
        }

        if (!isValidSPIRV(*info.spirv)) {
            EVK_FAIL_VOID("Invalid SPIR-V code");
        }

#ifdef EASYVK_USE_SPIRV_TOOLS
        std::string spirvError;
        if (!validateSPIRV(*info.spirv, spirvError)) {
            EVK_FAIL_VOID("SPIR-V validation failed: " + spirvError);
        }
#endif

        // Validate push constant constraints
        if (pcCapacityBytes_ > 0) {
            if (pcCapacityBytes_ % 4 != 0) {
                EVK_FAIL_VOID("Push constant size must be 4-byte aligned");
            }
            if (pcCapacityBytes_ > device_->limits().maxPushConstantsSize) {
                EVK_FAIL_VOID("Push constant size exceeds device limit");
            }
            pcData_.resize(pcCapacityBytes_, 0);
        }

        // Validate local workgroup size against device limits
        if (localX_ == 0 || localY_ == 0 || localZ_ == 0) {
            EVK_FAIL_VOID("Local workgroup size must be >= 1 in all dimensions");
        }
        const auto &lim = device_->limits();
        if (localX_ > lim.maxComputeWorkGroupSize[0] ||
            localY_ > lim.maxComputeWorkGroupSize[1] ||
            localZ_ > lim.maxComputeWorkGroupSize[2]) {
            EVK_FAIL_VOID("Local workgroup size exceeds device maxComputeWorkGroupSize[]");
        }
        const uint64_t invocations = uint64_t(localX_) * uint64_t(localY_) * uint64_t(localZ_);
        if (invocations > lim.maxComputeWorkGroupInvocations) {
            EVK_FAIL_VOID("Local workgroup invocations exceed maxComputeWorkGroupInvocations");
        }

        // Validate descriptor buffer alignments
        for (const auto &entry : info.bindings.entries) {
            for (const auto &bufInfo : entry.buffers) {
                VkDeviceSize requiredAlignment = 0;
                if (entry.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
                    requiredAlignment = device_->limits().minUniformBufferOffsetAlignment;
                } else if (entry.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
                    requiredAlignment = device_->limits().minStorageBufferOffsetAlignment;
                }

                if (requiredAlignment > 0 && (bufInfo.offset % requiredAlignment) != 0) {
                    EVK_FAIL_VOID("Descriptor buffer offset violates alignment requirement");
                }
            }
        }

        try {
            // Create shader module
            VkShaderModuleCreateInfo createInfo{
                VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                nullptr,
                0,
                info.spirv->size() * sizeof(uint32_t),
                info.spirv->data()
            };
            VK_CHECK(vkCreateShaderModule(device_->vk(), &createInfo, nullptr, &shader_));

            // Group bindings by set
            std::map<uint32_t, std::vector<VkDescriptorSetLayoutBinding>> setBindings;
            for (const auto &entry : info.bindings.entries) {
                setBindings[entry.set].push_back(VkDescriptorSetLayoutBinding{
                    entry.binding,
                    entry.type,
                    static_cast<uint32_t>(entry.buffers.size()),
                    VK_SHADER_STAGE_COMPUTE_BIT,
                    nullptr
                });
            }

            // Create descriptor set layouts (fill gaps with empty layouts)
            setLayouts_.resize(setBindings.empty() ? 0 : setBindings.rbegin()->first + 1);
            for (const auto &pair : setBindings) {
                uint32_t setIndex = pair.first;
                const auto &bindings = pair.second;
                VkDescriptorSetLayoutCreateInfo dslCreateInfo{
                    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                    nullptr,
                    0,
                    static_cast<uint32_t>(bindings.size()),
                    bindings.data()
                };
                VK_CHECK(vkCreateDescriptorSetLayout(device_->vk(), &dslCreateInfo, nullptr, &setLayouts_[setIndex]));
            }
            for (VkDescriptorSetLayout &setLayout : setLayouts_) {
                if (setLayout == VK_NULL_HANDLE) {
                    // Create an empty (0-binding) layout for missing lower-index sets
                    VkDescriptorSetLayoutCreateInfo emptyDsl{
                        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                        nullptr, 0, 0, nullptr
                    };
                    VK_CHECK(vkCreateDescriptorSetLayout(device_->vk(), &emptyDsl, nullptr, &setLayout));
                }
            }

            // Create pipeline layout
            VkPushConstantRange pushRange{};
            if (pcCfg_.sizeBytes > 0) {
                pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
                pushRange.offset = pcCfg_.offset;
                pushRange.size = pcCfg_.sizeBytes;
            }

            VkPipelineLayoutCreateInfo layoutCreateInfo{
                VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                nullptr,
                0,
                static_cast<uint32_t>(setLayouts_.size()),
                setLayouts_.data(),
                pcCfg_.sizeBytes > 0 ? 1u : 0u,
                pcCfg_.sizeBytes > 0 ? &pushRange : nullptr
            };
            VK_CHECK(vkCreatePipelineLayout(device_->vk(), &layoutCreateInfo, nullptr, &layout_));

            // Create descriptor pool and sets
            if (!info.bindings.entries.empty()) {
                std::map<VkDescriptorType, uint32_t> typeCounts;
                for (const auto &entry : info.bindings.entries) {
                    typeCounts[entry.type] += static_cast<uint32_t>(entry.buffers.size());
                }

                std::vector<VkDescriptorPoolSize> poolSizes;
                poolSizes.reserve(typeCounts.size());
                for (const auto &pair : typeCounts) {
                    poolSizes.push_back({pair.first, pair.second});
                }

                VkDescriptorPoolCreateInfo poolCreateInfo{
                    VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                    nullptr,
                    0,
                    static_cast<uint32_t>(setLayouts_.size()),
                    static_cast<uint32_t>(poolSizes.size()),
                    poolSizes.data()
                };
                VK_CHECK(vkCreateDescriptorPool(device_->vk(), &poolCreateInfo, nullptr, &dsp_));

                descriptorSets_.resize(setLayouts_.size());
                VkDescriptorSetAllocateInfo allocInfo{
                    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                    nullptr,
                    dsp_,
                    static_cast<uint32_t>(setLayouts_.size()),
                    setLayouts_.data()
                };
                VK_CHECK(vkAllocateDescriptorSets(device_->vk(), &allocInfo, descriptorSets_.data()));

                // Update descriptor sets
                std::vector<VkWriteDescriptorSet> writes;
                writes.reserve(info.bindings.entries.size());
                for (const auto &entry : info.bindings.entries) {
                    writes.push_back(VkWriteDescriptorSet{
                        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                        nullptr,
                        descriptorSets_[entry.set],
                        entry.binding,
                        0,
                        static_cast<uint32_t>(entry.buffers.size()),
                        entry.type,
                        nullptr,
                        entry.buffers.data(),
                        nullptr
                    });
                }
                vkUpdateDescriptorSets(device_->vk(),
                                       static_cast<uint32_t>(writes.size()), writes.data(),
                                       0, nullptr);
            }

            // Create specialization constants for workgroup size
            std::vector<VkSpecializationMapEntry> specEntries = {
                {0, 0 * sizeof(uint32_t), sizeof(uint32_t)},
                {1, 1 * sizeof(uint32_t), sizeof(uint32_t)},
                {2, 2 * sizeof(uint32_t), sizeof(uint32_t)}
            };
            std::vector<uint32_t> specData = {localX_, localY_, localZ_};

            // Add local memory specialization constants
            for (const auto &mem : info.localMemory) {
                specEntries.push_back({3 + mem.first, static_cast<uint32_t>((3 + mem.first) * sizeof(uint32_t)), sizeof(uint32_t)});
                if (specData.size() <= 3 + mem.first) {
                    specData.resize(3 + mem.first + 1, 0);
                }
                specData[3 + mem.first] = mem.second;
            }

            VkSpecializationInfo specInfo{
                static_cast<uint32_t>(specEntries.size()),
                specEntries.data(),
                specData.size() * sizeof(uint32_t),
                specData.data()
            };

            // Create compute pipeline
            VkPipelineShaderStageCreateInfo stageInfo{
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                nullptr,
                0,
                VK_SHADER_STAGE_COMPUTE_BIT,
                shader_,
                (info.entryPointName && info.entryPointName[0]) ? info.entryPointName : "main",
                &specInfo
            };

            VkComputePipelineCreateInfo pipelineInfo{
                VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                nullptr,
                0,
                stageInfo,
                layout_,
                VK_NULL_HANDLE,
                0
            };
            VK_CHECK(vkCreateComputePipelines(device_->vk(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_));

            // Create command resources
            VkCommandPoolCreateInfo poolInfo{
                VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                nullptr,
                VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                device_->computeQueueFamilyIndex()
            };
            VK_CHECK(vkCreateCommandPool(device_->vk(), &poolInfo, nullptr, &cmdPool_));

            VkCommandBufferAllocateInfo allocInfo{
                VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                nullptr,
                cmdPool_,
                VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                1
            };
            VK_CHECK(vkAllocateCommandBuffers(device_->vk(), &allocInfo, &cmdBuf_));

            VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, 0};
            VK_CHECK(vkCreateFence(device_->vk(), &fenceInfo, nullptr, &fence_));

            // Create timestamp query pool if supported
            if (device_->supportsTimestamps()) {
                VkQueryPoolCreateInfo queryPoolInfo{
                    VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                    nullptr,
                    0,
                    VK_QUERY_TYPE_TIMESTAMP,
                    2,
                    0
                };
                VK_CHECK(vkCreateQueryPool(device_->vk(), &queryPoolInfo, nullptr, &timestampQueryPool_));
            }
        } catch (...) {
            teardown();
            throw;
        }
    }

    ComputeProgram::ComputeProgram(ComputeProgram &&other) noexcept
        : device_(other.device_),
          setLayouts_(std::move(other.setLayouts_)),
          layout_(other.layout_),
          pipeline_(other.pipeline_),
          dsp_(other.dsp_),
          descriptorSets_(std::move(other.descriptorSets_)),
          shader_(other.shader_),
          cmdPool_(other.cmdPool_),
          cmdBuf_(other.cmdBuf_),
          fence_(other.fence_),
          timestampQueryPool_(other.timestampQueryPool_),
          pcCapacityBytes_(other.pcCapacityBytes_),
          pcCfg_(other.pcCfg_),
          groupsX_(other.groupsX_),
          groupsY_(other.groupsY_),
          groupsZ_(other.groupsZ_),
          localX_(other.localX_),
          localY_(other.localY_),
          localZ_(other.localZ_),
          pcData_(std::move(other.pcData_)),
          initialized_(other.initialized_),
          tornDown_(other.tornDown_),
          timestampInFlight_(other.timestampInFlight_),
          lastTimestamps_() {
        lastTimestamps_[0] = other.lastTimestamps_[0];
        lastTimestamps_[1] = other.lastTimestamps_[1];

        other.layout_ = VK_NULL_HANDLE;
        other.pipeline_ = VK_NULL_HANDLE;
        other.dsp_ = VK_NULL_HANDLE;
        other.shader_ = VK_NULL_HANDLE;
        other.cmdPool_ = VK_NULL_HANDLE;
        other.cmdBuf_ = VK_NULL_HANDLE;
        other.fence_ = VK_NULL_HANDLE;
        other.timestampQueryPool_ = VK_NULL_HANDLE;
        other.initialized_ = false;
        other.tornDown_ = true;
        other.timestampInFlight_ = false;
    }

    ComputeProgram &ComputeProgram::operator=(ComputeProgram &&other) noexcept {
        if (this != &other) {
            teardown();

            device_ = other.device_;
            setLayouts_ = std::move(other.setLayouts_);
            layout_ = other.layout_;
            pipeline_ = other.pipeline_;
            dsp_ = other.dsp_;
            descriptorSets_ = std::move(other.descriptorSets_);
            shader_ = other.shader_;
            cmdPool_ = other.cmdPool_;
            cmdBuf_ = other.cmdBuf_;
            fence_ = other.fence_;
            timestampQueryPool_ = other.timestampQueryPool_;
            pcCapacityBytes_ = other.pcCapacityBytes_;
            pcCfg_ = other.pcCfg_;
            groupsX_ = other.groupsX_;
            groupsY_ = other.groupsY_;
            groupsZ_ = other.groupsZ_;
            localX_ = other.localX_;
            localY_ = other.localY_;
            localZ_ = other.localZ_;
            pcData_ = std::move(other.pcData_);
            initialized_ = other.initialized_;
            tornDown_ = other.tornDown_;
            timestampInFlight_ = other.timestampInFlight_;
            lastTimestamps_[0] = other.lastTimestamps_[0];
            lastTimestamps_[1] = other.lastTimestamps_[1];

            other.layout_ = VK_NULL_HANDLE;
            other.pipeline_ = VK_NULL_HANDLE;
            other.dsp_ = VK_NULL_HANDLE;
            other.shader_ = VK_NULL_HANDLE;
            other.cmdPool_ = VK_NULL_HANDLE;
            other.cmdBuf_ = VK_NULL_HANDLE;
            other.fence_ = VK_NULL_HANDLE;
            other.timestampQueryPool_ = VK_NULL_HANDLE;
            other.initialized_ = false;
            other.tornDown_ = true;
            other.timestampInFlight_ = false;
        }
        return *this;
    }

    ComputeProgram::~ComputeProgram() noexcept {
        if (!tornDown_) {
            teardown();
        }
    }

    void ComputeProgram::setPushConstantConfig(const PushConstantConfig &config) {
        pcCfg_ = config;
        if (pcCfg_.sizeBytes > 0) {
            pcData_.assign(pcCfg_.sizeBytes, 0);
        }
    }

    void ComputeProgram::setPushConstants(const void *data, uint32_t bytes, uint32_t offset) {
        if (pcData_.size() < pcCfg_.sizeBytes) pcData_.assign(pcCfg_.sizeBytes, 0);
        if (!data || bytes == 0) return;

        uint32_t end = std::min<uint32_t>(offset + bytes, pcCfg_.sizeBytes);
        if (offset < end) {
            std::memcpy(pcData_.data() + offset, data, end - offset);
        }
    }

    void ComputeProgram::setWorkgroups(uint32_t x, uint32_t y, uint32_t z) {
        if (x == 0 || y == 0 || z == 0) {
            EVK_FAIL_VOID("Workgroup counts must be greater than zero");
        }

        // Check against device limits
        if (x > device_->limits().maxComputeWorkGroupCount[0] ||
            y > device_->limits().maxComputeWorkGroupCount[1] ||
            z > device_->limits().maxComputeWorkGroupCount[2]) {
            EVK_FAIL_VOID("Workgroup count exceeds device limits");
        }

        groupsX_ = x;
        groupsY_ = y;
        groupsZ_ = z;
    }

    void ComputeProgram::dispatch() {
        submitAndWait(true);
    }

    void ComputeProgram::dispatchNoHostBarrier() {
        submitAndWait(false);
    }

    bool ComputeProgram::supportsTimestamps() const {
        return device_ && device_->supportsTimestamps() && (timestampQueryPool_ != VK_NULL_HANDLE);
    }

    double ComputeProgram::dispatchWithTimingNs() {
        if (!supportsTimestamps()) {
            dispatch();
            return 0.0;
        }

        if (!initialized_) {
            EVK_FAIL("Program not initialized");
        }

        SubmitHandle handle = submitAsync(true, true);
        device_->wait(handle);

        // Get timestamp results
        uint64_t timestamps[2];
        VK_CHECK(vkGetQueryPoolResults(device_->vk(), timestampQueryPool_, 0, 2,
            sizeof(timestamps), timestamps, sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));

        return static_cast<double>(timestamps[1] - timestamps[0]) * device_->timestampPeriod();
    }

    SubmitHandle ComputeProgram::dispatchWithTimingAsync() {
        if (!supportsTimestamps()) {
            return submitAsync(true, false);
        }

        timestampInFlight_ = true;
        return submitAsync(true, true);
    }

    bool ComputeProgram::tryGetTimingNs(double& outNs) {
        if (!supportsTimestamps() || !timestampInFlight_) {
            return false;
        }
        struct QueryPair { uint64_t value; uint64_t available; };
        QueryPair q[2] = {};
        VkResult result = vkGetQueryPoolResults(device_->vk(), timestampQueryPool_, 0, 2,
            sizeof(q), q, sizeof(QueryPair),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);

        if (result == VK_NOT_READY) {
            return false;
        }

        VK_CHECK(result);

        if (q[0].available == 0 || q[1].available == 0) {
            return false;
        }
        lastTimestamps_[0] = q[0].value;
        lastTimestamps_[1] = q[1].value;
        timestampInFlight_ = false;
        outNs = static_cast<double>(q[1].value - q[0].value) * device_->timestampPeriod();
        return true;
    }

    SubmitHandle ComputeProgram::submitAsync(bool addHostBarrier, bool enableTimestamps) {
        if (!initialized_) {
            EVK_FAIL("Program not initialized");
        }

        VK_CHECK(vkResetCommandBuffer(cmdBuf_, 0));

        VkCommandBufferBeginInfo beginInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            nullptr,
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            nullptr
        };
        VK_CHECK(vkBeginCommandBuffer(cmdBuf_, &beginInfo));

        if (enableTimestamps && timestampQueryPool_ != VK_NULL_HANDLE) {
            vkCmdResetQueryPool(cmdBuf_, timestampQueryPool_, 0, 2);
        }

        vkCmdBindPipeline(cmdBuf_, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);

        // Optional GPU label for captures
        if (vkCmdBeginDebugUtilsLabelEXT) {
            VkDebugUtilsLabelEXT label{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
            label.pLabelName = addHostBarrier ? "easyvk::dispatch" : "easyvk::dispatchNoHostBarrier";
            vkCmdBeginDebugUtilsLabelEXT(cmdBuf_, &label);
        }

        if (!descriptorSets_.empty()) {
            vkCmdBindDescriptorSets(cmdBuf_, VK_PIPELINE_BIND_POINT_COMPUTE, layout_, 0,
                                  static_cast<uint32_t>(descriptorSets_.size()),
                                  descriptorSets_.data(), 0, nullptr);
        }

        // Push constants
        if (pcCfg_.sizeBytes > 0) {
            if (pcData_.size() < pcCfg_.sizeBytes) pcData_.assign(pcCfg_.sizeBytes, 0);
            vkCmdPushConstants(cmdBuf_, layout_, VK_SHADER_STAGE_COMPUTE_BIT, pcCfg_.offset, pcCfg_.sizeBytes, pcData_.data());
        }

        // Host->Device barrier
        VkMemoryBarrier hostToDeviceBarrier{
            VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            nullptr,
            VK_ACCESS_HOST_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT
        };
        vkCmdPipelineBarrier(cmdBuf_, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &hostToDeviceBarrier, 0, nullptr, 0, nullptr);

        if (enableTimestamps && timestampQueryPool_ != VK_NULL_HANDLE) {
            vkCmdWriteTimestamp(cmdBuf_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, timestampQueryPool_, 0);
        }

        vkCmdDispatch(cmdBuf_, groupsX_, groupsY_, groupsZ_);

        if (enableTimestamps && timestampQueryPool_ != VK_NULL_HANDLE) {
            vkCmdWriteTimestamp(cmdBuf_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, timestampQueryPool_, 1);
        }

        if (addHostBarrier) {
            VkMemoryBarrier barrier{
                VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                nullptr,
                VK_ACCESS_SHADER_WRITE_BIT,
                VK_ACCESS_HOST_READ_BIT
            };
            vkCmdPipelineBarrier(cmdBuf_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                                 0, 1, &barrier, 0, nullptr, 0, nullptr);
        }

        if (vkCmdEndDebugUtilsLabelEXT) {
            vkCmdEndDebugUtilsLabelEXT(cmdBuf_);
        }

        VK_CHECK(vkEndCommandBuffer(cmdBuf_));

        VkFence asyncFence;
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, 0};
        VK_CHECK(vkCreateFence(device_->vk(), &fenceInfo, nullptr, &asyncFence));

        VkSubmitInfo submitInfo{
            VK_STRUCTURE_TYPE_SUBMIT_INFO,
            nullptr,
            0, nullptr, nullptr,
            1, &cmdBuf_,
            0, nullptr
        };

        VK_CHECK(vkQueueSubmit(device_->computeQueue(), 1, &submitInfo, asyncFence));
        return SubmitHandle{asyncFence, VK_NULL_HANDLE};
    }

    void ComputeProgram::submitAndWait(bool addHostBarrier) {
        SubmitHandle handle = submitAsync(addHostBarrier, false);
        device_->wait(handle);
    }

    void ComputeProgram::teardown() {
        if (tornDown_) return;

        if (device_ && device_->vk() != VK_NULL_HANDLE) {
            if (timestampQueryPool_ != VK_NULL_HANDLE) {
                vkDestroyQueryPool(device_->vk(), timestampQueryPool_, nullptr);
                timestampQueryPool_ = VK_NULL_HANDLE;
            }

            if (fence_ != VK_NULL_HANDLE) {
                vkDestroyFence(device_->vk(), fence_, nullptr);
                fence_ = VK_NULL_HANDLE;
            }

            if (cmdBuf_ != VK_NULL_HANDLE && cmdPool_ != VK_NULL_HANDLE) {
                vkFreeCommandBuffers(device_->vk(), cmdPool_, 1, &cmdBuf_);
                cmdBuf_ = VK_NULL_HANDLE;
            }

            if (cmdPool_ != VK_NULL_HANDLE) {
                vkDestroyCommandPool(device_->vk(), cmdPool_, nullptr);
                cmdPool_ = VK_NULL_HANDLE;
            }

            if (pipeline_ != VK_NULL_HANDLE) {
                vkDestroyPipeline(device_->vk(), pipeline_, nullptr);
                pipeline_ = VK_NULL_HANDLE;
            }

            if (layout_ != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(device_->vk(), layout_, nullptr);
                layout_ = VK_NULL_HANDLE;
            }

            if (dsp_ != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(device_->vk(), dsp_, nullptr);
                dsp_ = VK_NULL_HANDLE;
            }

            for (auto layout : setLayouts_) {
                if (layout != VK_NULL_HANDLE) {
                    vkDestroyDescriptorSetLayout(device_->vk(), layout, nullptr);
                }
            }
            setLayouts_.clear();

            if (shader_ != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device_->vk(), shader_, nullptr);
                shader_ = VK_NULL_HANDLE;
            }
        }

        tornDown_ = true;
    }

    // -------- Debug utilities ---------------------------------------------------
    void setObjectName(Instance &inst, Device &dev, uint64_t objectHandle, VkObjectType type, const char *name) {
        if (!inst.debugUtilsEnabled() || !name) return;

        VkDebugUtilsObjectNameInfoEXT nameInfo{
            VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
            nullptr, type, objectHandle, name
        };

        if (vkSetDebugUtilsObjectNameEXT) {
            vkSetDebugUtilsObjectNameEXT(dev.vk(), &nameInfo);
        }
    }

    // -------- Utility functions -------------------------------------------------
    std::vector<uint32_t> readSpirv(const char *filename) {
        std::ifstream fin(filename, std::ios::binary | std::ios::ate);
        if (!fin.is_open()) {
#ifdef EASYVK_NO_EXCEPTIONS
            evk_log("Failed to open SPIR-V file: %s\n", filename);
            return {};
#else
            throw std::runtime_error(std::string("failed opening file ") + filename + " for reading");
#endif
        }

        const auto stream_size = static_cast<unsigned>(fin.tellg());
        fin.seekg(0);

        if (stream_size % 4 != 0) {
#ifdef EASYVK_NO_EXCEPTIONS
            evk_log("SPIR-V file has invalid size: %u (not multiple of 4 bytes)\n", stream_size);
            return {};
#else
            throw std::runtime_error(std::string("SPIR-V file ") + filename +
                " has invalid size " + std::to_string(stream_size) + " (not multiple of 4 bytes)");
#endif
        }

        std::vector<uint32_t> ret((stream_size + 3) / 4, 0);
        fin.read(reinterpret_cast<char *>(ret.data()), stream_size);
        return ret;
    }
} // namespace easyvk
