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

// easyvk.h - Minimal, safe, and consistent C++11 Vulkan compute helper
// -------------------------------------------------------------------
// Design:
//  - Flat, developer-friendly API: *Info structs, no nested classes.
//  - Frozen creation-time invariants (pipeline baked in constructor).
//  - RAII mapping with correct non-coherent alignment + clamping.
//  - Safe default barrier (Compute -> Host) in dispatch() for readback.
//  - Robustness toggles (robustBufferAccess / VK_EXT_robustness2).
//  - Programmatic validation / debug utils helpers.
//
// Build: C++11, depends on <volk.h>. Define VK_NO_PROTOTYPES in your build.
// License: MIT (or match your project)

#ifndef EASYVK_H
#define EASYVK_H

#include <vector>
#include <string>
#include <cstdint>
#include <stdexcept>

#include <volk.h>

namespace easyvk {
    // -------- Version ------------------------------------------------------------
#define EASYVK_VERSION_MAJOR 2
#define EASYVK_VERSION_MINOR 0
#define EASYVK_VERSION_PATCH 0

    // -------- Public compile options --------------------------------------------
    // Switch to a non-throwing mode (methods return bool/Result, store lastError).
    // #define EASYVK_NO_EXCEPTIONS
    // Default-on convenience toggles (implementations soft-fail if unsupported):
    // #define EASYVK_DEFAULT_ENABLE_DEBUG_UTILS 1
    // #define EASYVK_DEFAULT_ENABLE_ROBUSTNESS2 1

#ifdef EASYVK_NO_EXCEPTIONS
    struct Result {
        bool ok;
        std::string message;

        Result(bool o = true, const std::string &m = std::string()) : ok(o), message(m) {
        }

        operator bool() const { return ok; }
    };
#endif

    // -------- Exception class ---------------------------------------------------
    class VulkanError : public std::runtime_error {
    public:
        VulkanError(VkResult result, const std::string &message, const std::string &file, int line)
            : std::runtime_error(message), result_(result), file_(file), line_(line) {}

        VkResult getResult() const { return result_; }
        const std::string &getFile() const { return file_; }
        int getLine() const { return line_; }

    private:
        VkResult result_;
        std::string file_;
        int line_;
    };

    // -------- Small enums / handles ---------------------------------------------
    struct SubmitHandle {
        VkFence fence;
        VkCommandBuffer cmdBuf; // transient CB allocated for this submission

        SubmitHandle() : fence(VK_NULL_HANDLE), cmdBuf(VK_NULL_HANDLE) {}
        explicit SubmitHandle(VkFence f, VkCommandBuffer cb = VK_NULL_HANDLE) : fence(f), cmdBuf(cb) {}
    };

    enum class HostAccess { None, Write, Read, ReadWrite };

    enum class BufferUsage { Storage, Uniform, Staging, TransferSrc, TransferDst };

    // -------- Instance -----------------------------------------------------------
    struct InstanceInfo {
        bool enableValidationLayers;               // request VK_LAYER_KHRONOS_validation
        bool enableDebugUtils;                     // request VK_EXT_debug_utils + messenger
        bool enableLayerSettings;                  // request VK_EXT_layer_settings
        const char *applicationName;               // optional
        uint32_t applicationVersion;               // optional
        uint32_t apiVersion;                       // e.g. VK_API_VERSION_1_3
        std::vector<const char *> extraExtensions; // deduplicated internally
        std::vector<const char *> extraLayers;     // deduplicated internally
        // Optional: programmatic layer settings for VK_EXT_layer_settings
        std::vector<VkLayerSettingEXT> layerSettings; // if non-empty and extension present, chained into pNext

        InstanceInfo()
            : enableValidationLayers(false),
#if defined(EASYVK_DEFAULT_ENABLE_DEBUG_UTILS)
              enableDebugUtils(true),
#else
              enableDebugUtils(false),
#endif
              enableLayerSettings(false),
              applicationName("easyvk"),
              applicationVersion(1),
              apiVersion(VK_API_VERSION_1_3) {
        }
    };

    class Instance {
    public:
        Instance(); // defaults (no validation)
        explicit Instance(const InstanceInfo &info);
#ifndef EASYVK_NO_EXCEPTIONS
        explicit Instance(bool enableValidationLayers);
#endif
        ~Instance() noexcept;

        Instance(const Instance &) = delete;
        Instance &operator=(const Instance &) = delete;
        Instance(Instance &&) noexcept;
        Instance &operator=(Instance &&) noexcept;

        VkInstance vk() const { return instance_; }
        bool validationEnabled() const { return validationEnabled_; }
        bool debugUtilsEnabled() const { return debugUtilsEnabled_; }

        std::vector<VkPhysicalDevice> physicalDevices() const;

#ifdef EASYVK_NO_EXCEPTIONS
        const std::string &lastError() const { return lastError_; }
#endif

    private:
        VkInstance instance_;
        VkDebugUtilsMessengerEXT debugMessenger_;
        bool validationEnabled_;
        bool debugUtilsEnabled_;
        bool tornDown_;
#ifdef EASYVK_NO_EXCEPTIONS
        std::string lastError_;
#endif

        void teardown();
    };

    // -------- Device -------------------------------------------------------------
    struct DeviceInfo {
        int preferredIndex;            // -1: pick best discrete > integrated > cpu
        bool enableRobustBufferAccess; // core robustBufferAccess
        bool enableRobustness2;        // VK_EXT_robustness2 features (if supported)
        bool enableDebugMarkers;       // VK_EXT_debug_marker (optional)

        DeviceInfo()
            : preferredIndex(-1),
#ifdef EASYVK_DEFAULT_ENABLE_ROBUSTNESS2
              enableRobustBufferAccess(true),
                  enableRobustness2(true),
#else
              enableRobustBufferAccess(true),
              enableRobustness2(false),
#endif
              enableDebugMarkers(false) {
        }
    };

    class Device {
    public:
        explicit Device(Instance &inst, const DeviceInfo &info);
#ifndef EASYVK_NO_EXCEPTIONS
        explicit Device(Instance &inst, int preferredIndex = -1);
#endif
        ~Device() noexcept;

        Device(const Device &) = delete;
        Device &operator=(const Device &) = delete;
        Device(Device &&) noexcept;
        Device &operator=(Device &&) noexcept;

        VkDevice vk() const { return device_; }
        VkPhysicalDevice physical() const { return phys_; }
        VkQueue computeQueue() const { return queue_; }
        uint32_t computeQueueFamilyIndex() const { return queueFamilyIndex_; }
        const VkPhysicalDeviceLimits &limits() const { return limits_; }

        // Wait for an async fence (copy/dispatch). Consumes & destroys the fence and frees
        // the transient command buffer if present. Returns true on VK_SUCCESS.
        bool wait(const SubmitHandle &h, uint64_t timeoutNs = UINT64_C(0xFFFFFFFFFFFFFFFF));

        bool robustAccessEnabled() const { return robustAccessEnabled_; }
        bool robustness2Enabled() const { return robustness2Enabled_; }

        uint32_t selectMemory(uint32_t memoryTypeBits, VkMemoryPropertyFlags flags);
        uint32_t subgroupSize() const;
        const char *vendorName() const;
        bool supportsTimestamps() const { return supportsTimestamps_; }
        double timestampPeriod() const { return timestampPeriod_; }

#ifdef EASYVK_NO_EXCEPTIONS
        const std::string &lastError() const { return lastError_; }
#endif

    private:
        Instance *instance_;
        VkPhysicalDevice phys_;
        VkDevice device_;
        VkQueue queue_;
        uint32_t queueFamilyIndex_;
        VkPhysicalDeviceLimits limits_;
        VkCommandPool transferCmdPool_; // internal one-time submit pool
        bool robustAccessEnabled_;
        bool robustness2Enabled_;
        bool debugMarkersEnabled_;
        bool supportsTimestamps_;
        double timestampPeriod_;
        bool tornDown_;

#ifdef EASYVK_NO_EXCEPTIONS
        std::string lastError_;
#endif

        void teardown();
        friend class Buffer;
        friend class ComputeProgram;
        friend void setObjectName(Instance &, Device &, uint64_t, VkObjectType, const char *);
    };

    // -------- Buffer -------------------------------------------------------------
    struct BufferInfo {
        VkDeviceSize sizeBytes;
        BufferUsage usage;
        HostAccess host;

        explicit BufferInfo(VkDeviceSize s = 0, BufferUsage u = BufferUsage::Storage, HostAccess h = HostAccess::None)
            : sizeBytes(s), usage(u), host(h) {}
    };

    class Buffer; // fwd for BufferMapping

    // RAII mapping for host-visible memory. For non-coherent memory:
    //  - mapWrite: dtor FLUSHES the aligned mapped subrange
    //  - mapRead : caller INVALIDATES after mapping (see mapRead()), dtor does nothing
    // All mappings use an aligned superset to satisfy nonCoherentAtomSize.
    class BufferMapping {
    public:
        BufferMapping();
        ~BufferMapping() noexcept;

        // Move-only
        BufferMapping(BufferMapping &&other) noexcept;
        BufferMapping &operator=(BufferMapping &&other) noexcept;
        BufferMapping(const BufferMapping &) = delete;
        BufferMapping &operator=(const BufferMapping &) = delete;

        void *data() const { return ptr_; }

        template <typename T>
        T *as() const { return static_cast<T *>(ptr_); }

        VkDeviceSize offsetBytes() const { return offset_; }
        VkDeviceSize lengthBytes() const { return length_; }
        bool isWriteMapping() const { return write_; }
        bool isValid() const { return buf_ != nullptr; }

    private:
        friend class Buffer;

        BufferMapping(Buffer *b, void *p, VkDeviceSize off, VkDeviceSize len, bool w)
            : buf_(b), ptr_(p), offset_(off), length_(len), write_(w) {}

        Buffer *buf_;
        void *ptr_;
        VkDeviceSize offset_;
        VkDeviceSize length_;
        bool write_;
    };

    class Buffer {
    public:
        Buffer(Device &dev, const BufferInfo &info);

        Buffer(Device &dev, VkDeviceSize sizeBytes, BufferUsage usage, HostAccess host)
            : Buffer(dev, BufferInfo(sizeBytes, usage, host)) {}

        ~Buffer() noexcept;

        Buffer(const Buffer &) = delete;
        Buffer &operator=(const Buffer &) = delete;
        Buffer(Buffer &&) noexcept;
        Buffer &operator=(Buffer &&) noexcept;

        VkBuffer vk() const { return buffer_; }
        VkDeviceSize size() const { return size_; }
        Device &device() const { return *device_; }

        // Map for CPU writes (host->device). On non-coherent memory, dtor flushes.
        BufferMapping mapWrite(VkDeviceSize offsetBytes, VkDeviceSize lengthBytes);
        // Map for CPU reads (device->host). For non-coherent memory, INVALIDATE is done after mapping.
        BufferMapping mapRead(VkDeviceSize offsetBytes, VkDeviceSize lengthBytes);

        // One-shot synchronous copy via internal command buffer.
        bool copyTo(Buffer &dst, VkDeviceSize bytes, VkDeviceSize srcOffset = 0, VkDeviceSize dstOffset = 0);

        // Asynchronous copy; returns a fence to wait on.
        SubmitHandle copyToAsync(Buffer &dst, VkDeviceSize bytes, VkDeviceSize srcOffset = 0, VkDeviceSize dstOffset = 0);

#ifdef EASYVK_NO_EXCEPTIONS
        const std::string &lastError() const { return lastError_; }
#endif

    private:
        Device *device_;
        VkBuffer buffer_;
        VkDeviceMemory memory_;
        VkDeviceSize size_;
        VkMemoryPropertyFlags memFlags_;
        HostAccess hostAccess_;
        bool tornDown_;

#ifdef EASYVK_NO_EXCEPTIONS
        std::string lastError_;
#endif

        void teardown();
        void validateRange(VkDeviceSize offset, VkDeviceSize len, const char *operation) const;
        void createVkBuffer(VkBuffer *buf, VkDeviceMemory *mem, VkDeviceSize sizeBytes,
                            VkBufferUsageFlags usage, VkMemoryPropertyFlags props);
        void flushRange(VkDeviceSize offset, VkDeviceSize sizeBytes);
        void invalidateRange(VkDeviceSize offset, VkDeviceSize sizeBytes);

        friend class BufferMapping;
    };

    // -------- Compute pipeline (program) -----------------------------------------
    struct ComputeBindingEntry {
        uint32_t binding;
        VkDescriptorType type; // VK_DESCRIPTOR_TYPE_STORAGE_BUFFER or UNIFORM_BUFFER
        VkBuffer buffer;
        VkDeviceSize offset; // must satisfy device min alignment for the type
        VkDeviceSize range;  // VK_WHOLE_SIZE allowed (resolved at init)
    };

    struct ComputeBindings {
        std::vector<ComputeBindingEntry> entries;

        void addStorage(uint32_t binding, const Buffer &buf, VkDeviceSize offset = 0, VkDeviceSize range = VK_WHOLE_SIZE);
        void addUniform(uint32_t binding, const Buffer &buf, VkDeviceSize offset = 0, VkDeviceSize range = VK_WHOLE_SIZE);
    };

    struct ComputeProgramInfo {
        // SPIR-V (uint32_t words) of the compute shader.
        const std::vector<uint32_t> *spirv;

        // Specialization constants for local workgroup size (≥1).
        uint32_t localX, localY, localZ;

        // Optional: (index, bytes) declarations for threadgroup (shared) memory.
        std::vector<std::pair<uint32_t, uint32_t>> localMemory;

        // Push-constant capacity in bytes (must be >0, multiple of 4, <= maxPushConstantsSize).
        uint32_t pushConstantBytes;

        // Name of the SPIR-V OpEntryPoint for this compute shader (default "main")
        const char *entryPointName;

        // Resource bindings snapshot (copied at initialization).
        ComputeBindings bindings;

        ComputeProgramInfo() : spirv(nullptr), localX(1), localY(1), localZ(1), pushConstantBytes(0), entryPointName("main") {}
    };

    // -------- Push constant configuration ---------------------------------------
    struct PushConstantConfig {
        uint32_t sizeBytes = 0;          // 0 = no push constants
        uint32_t offset    = 0;          // must be multiple of 4

        PushConstantConfig() = default;
        explicit PushConstantConfig(uint32_t s, uint32_t o = 0) : sizeBytes(s), offset(o) {}
    };

    class ComputeProgram {
    public:
        ComputeProgram(); // invalid placeholder
        ComputeProgram(Device &dev, const ComputeProgramInfo &info);
        ~ComputeProgram() noexcept;

        ComputeProgram(const ComputeProgram &) = delete;
        ComputeProgram &operator=(const ComputeProgram &) = delete;
        ComputeProgram(ComputeProgram &&) noexcept;
        ComputeProgram &operator=(ComputeProgram &&) noexcept;

        // Runtime mutables:
        //  - setPushConstantConfig(...) : configure push constant layout
        //  - setPushConstants(...) : data within declared capacity; 4-byte aligned
        //  - setWorkgroups(...)    : dispatch counts (≥1)
        void setPushConstantConfig(const PushConstantConfig &config);
        void setPushConstants(const void *data, uint32_t bytes, uint32_t offset = 0);

        template <typename T>
        void setPushConstants(const T &pod, uint32_t offset = 0) {
            setPushConstants(&pod, (uint32_t) sizeof(T), offset);
        }

        void setWorkgroups(uint32_t x, uint32_t y = 1, uint32_t z = 1);

        // Submit with default Compute->Host barrier for safe CPU readback.
        void dispatch();

        // Submit without the final Host barrier. Use for GPU->GPU chains.
        void dispatchNoHostBarrier();

        // Timestamped dispatch; returns time in nanoseconds if supported.
        bool supportsTimestamps() const;
        double dispatchWithTimingNs();

#ifdef EASYVK_NO_EXCEPTIONS
        const std::string &lastError() const { return lastError_; }
#endif

    private:
        Device *device_;
        VkPipelineLayout layout_;
        VkPipeline pipeline_;
        VkDescriptorSetLayout dsl_;
        VkDescriptorPool dsp_;
        VkDescriptorSet ds_;
        VkShaderModule shader_;
        VkCommandPool cmdPool_;
        VkCommandBuffer cmdBuf_;
        VkFence fence_;
        VkQueryPool timestampQueryPool_;

        uint32_t pcCapacityBytes_;
        PushConstantConfig pcCfg_;
        uint32_t groupsX_, groupsY_, groupsZ_;
        uint32_t localX_, localY_, localZ_;
        std::vector<uint8_t> pcData_;
        bool initialized_;
        bool tornDown_;

#ifdef EASYVK_NO_EXCEPTIONS
        std::string lastError_;
#endif

        void teardown();
        void submitAndWait(bool addHostBarrier);
    };

    // -------- Utility functions -------------------------------------------------
    void vkCheck(VkResult result, const char *file, int line);
    const char *vkDeviceType(VkPhysicalDeviceType type);
    std::vector<uint32_t> readSpirv(const char *filename);

    // Alignment utilities
    VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment);
    VkDeviceSize alignDown(VkDeviceSize value, VkDeviceSize alignment);

    // Debug naming utility (requires debug utils enabled)
    void setObjectName(Instance &inst, Device &dev, uint64_t objectHandle, VkObjectType type, const char *name);
} // namespace easyvk

// Macro for error checking
#define VK_CHECK(result) easyvk::vkCheck((result), __FILE__, __LINE__)

#endif // EASYVK_H
