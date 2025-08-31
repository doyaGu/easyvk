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

#ifndef EASYVK_H
#define EASYVK_H

#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>
#include <map>
#include <stdexcept>

#ifdef __ANDROID__
#include <android/log.h>
#endif

#include <volk.h>

namespace easyvk {
    // Configuration constants
    static constexpr uint32_t DEFAULT_PUSH_CONSTANT_SIZE_BYTES = 20;
    static constexpr uint32_t INVALID_QUEUE_FAMILY = UINT32_MAX;

    // Exception class for Vulkan errors
    class VulkanError : public std::runtime_error {
    public:
        VulkanError(VkResult result, const std::string &message, std::string file, int line)
            : std::runtime_error(message), result_(result), file_(std::move(file)), line_(line) {}

        VkResult getResult() const { return result_; }
        const std::string &getFile() const { return file_; }
        int getLine() const { return line_; }

    private:
        VkResult result_;
        std::string file_;
        int line_;
    };

    class Device;
    class Buffer;

    class Instance {
    public:
        explicit Instance(bool enableValidationLayers = false);
        ~Instance();
        Instance(const Instance &) = delete;
        Instance &operator=(const Instance &) = delete;
        Instance(Instance &&other) noexcept;
        Instance &operator=(Instance &&other) noexcept;

        std::vector<VkPhysicalDevice> physicalDevices();
        void teardown();

    private:
        bool enableValidationLayers_;
        VkInstance instance_;
        VkDebugReportCallbackEXT debugReportCallback_;
        bool tornDown_;
    };

    class Device {
    public:
        Device(Instance &instance, VkPhysicalDevice physicalDevice);
        ~Device();
        Device(const Device &) = delete;
        Device &operator=(const Device &) = delete;
        Device(Device &&other) noexcept;
        Device &operator=(Device &&other) noexcept;

        VkDevice device;
        VkPhysicalDeviceProperties properties;
        uint32_t selectMemory(uint32_t memoryTypeBits, VkMemoryPropertyFlags flags);
        uint32_t computeFamilyId;
        uint32_t subgroupSize();
        const char *vendorName();
        VkQueue computeQueue;
        bool supportsAMDShaderStats;
        void teardown();

    private:
        Instance &instance_;
        VkPhysicalDevice physicalDevice_;
        bool tornDown_;
    };

    class Buffer {
    public:
        Buffer(Device &device, uint64_t sizeBytes, bool deviceLocal = false);
        ~Buffer();
        Buffer(const Buffer &) = delete;
        Buffer &operator=(const Buffer &) = delete;
        Buffer(Buffer &&other) noexcept;
        Buffer &operator=(Buffer &&other) noexcept;

        void teardown();
        void copy(Buffer &dst, uint64_t len, uint64_t srcOffset = 0, uint64_t dstOffset = 0);
        void store(const void *src, uint64_t len, uint64_t srcOffset = 0, uint64_t dstOffset = 0);
        void load(void *dst, uint64_t len, uint64_t srcOffset = 0, uint64_t dstOffset = 0);
        void clear();
        void fill(uint32_t word, uint64_t offset = 0);

        Device &device;
        VkCommandPool commandPool;
        VkCommandBuffer commandBuffer;
        VkDeviceMemory memory;
        VkBuffer buffer;
        uint64_t size;
        bool deviceLocal;

    private:
        void validateRange(uint64_t offset, uint64_t len, const char *operation) const;
        void copyInternal(VkBuffer src, VkBuffer dst, uint64_t len, uint64_t srcOffset = 0, uint64_t dstOffset = 0);
        void createVkBuffer(VkBuffer *buf, VkDeviceMemory *mem, uint64_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props);
        bool tornDown_;
    };

    typedef struct ShaderStatistics {
        std::string name;
        std::string description;
        size_t format;  // 0 -> bool, 1 -> int64, 2 -> uint64, 3 -> float64
        uint64_t value; // may need to cast this to get the right value based on the format
    } ShaderStatistics;

    /**
     * A program consists of shader code and the buffers/inputs to the shader
     * Buffers should be passed in according to their argument order in the shader.
     * Workgroup memory buffers are indexed from 0.
     */
    class Program {
    public:
        Program(Device &device, const char *filepath, std::vector<Buffer> &buffers,
                uint32_t pushConstantSizeBytes = DEFAULT_PUSH_CONSTANT_SIZE_BYTES);
        Program(Device &device, const std::vector<uint32_t> &spvCode, std::vector<Buffer> &buffers,
                uint32_t pushConstantSizeBytes = DEFAULT_PUSH_CONSTANT_SIZE_BYTES);
        ~Program();
        Program(const Program &) = delete;
        Program &operator=(const Program &) = delete;
        Program(Program &&other) noexcept;
        Program &operator=(Program &&other) noexcept;

        void initialize(const char *entryPoint = "main", VkPipelineShaderStageCreateFlags pipelineFlags = 0);
        std::vector<ShaderStatistics> getShaderStats();
        void run();
        float runWithDispatchTiming();
        void setWorkgroups(uint32_t numWorkgroups);
        void setWorkgroupSize(uint32_t workgroupSize);
        void setWorkgroupMemoryLength(uint32_t length, uint32_t index);
        void teardown();

    private:
        std::vector<Buffer> &buffers_;
        std::map<uint32_t, uint32_t> workgroupMemoryLengths_;
        VkShaderModule shaderModule_;
        Device &device_;
        VkDescriptorSetLayout descriptorSetLayout_;
        VkDescriptorPool descriptorPool_;
        VkDescriptorSet descriptorSet_;
        std::vector<VkWriteDescriptorSet> writeDescriptorSets_;
        std::vector<VkDescriptorBufferInfo> bufferInfos_;
        VkPipelineLayout pipelineLayout_;
        VkPipeline pipeline_;
        VkCommandPool commandPool_;
        uint32_t numWorkgroups_;
        uint32_t workgroupSize_;
        uint32_t pushConstantSizeBytes_;
        VkFence fence_;
        VkCommandBuffer commandBuffer_;
        VkQueryPool timestampQueryPool_;
        bool initialized_;
        bool tornDown_;
    };

    const char *vkDeviceType(VkPhysicalDeviceType type);

    // Utility functions
    void vkCheck(VkResult result, const char *file, int line);
}

#define VK_CHECK(result) easyvk::vkCheck((result), __FILE__, __LINE__)

#endif
