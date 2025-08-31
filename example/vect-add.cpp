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

#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

#include <easyvk.h>

static constexpr uint32_t kSize = 1024u * 16u;
static constexpr uint32_t kLocalSize = 64u; // specialization: local_size_x

int main() {
    // 1) Instance & device ------------------------------------------------------
    easyvk::Instance instance(/*enableValidationLayers=*/true);
    easyvk::Device device(instance, /*preferredIndex=*/0);

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(device.physical(), &props);
    std::cout << "Using device: " << props.deviceName
        << " [" << device.vendorName() << "]\n";

    // 2) Host-mappable storage buffers -----------------------------------------
    const VkDeviceSize bytes = kSize * sizeof(float); // 4B either way
    easyvk::Buffer aBuf(device, bytes, easyvk::BufferUsage::Storage, easyvk::HostAccess::ReadWrite);
    easyvk::Buffer bBuf(device, bytes, easyvk::BufferUsage::Storage, easyvk::HostAccess::ReadWrite);
    easyvk::Buffer cBuf(device, bytes, easyvk::BufferUsage::Storage, easyvk::HostAccess::Read);

    // Initialize A[i]=i (UINT), B[i]=i+1 (FLOAT) on the CPU
    {
        auto mapA = aBuf.mapWrite(0, aBuf.size());
        auto mapB = bBuf.mapWrite(0, bBuf.size());
        auto *A = mapA.as<uint32_t>();
        auto *B = mapB.as<float>();
        for (uint32_t i = 0; i < kSize; ++i) {
            A[i] = i;                         // uints, not floats
            B[i] = static_cast<float>(i + 1); // floats
        }
        // (EasyVK should flush/unmap on scope-exit for non-coherent memory)
    }

    // 3) Load SPIR-V and configure the compute program --------------------------
#ifdef USE_EMBEDDED_SPIRV
    std::vector<uint32_t> spvCode =
#include "vect-add.cinit"
        ;
#else
    std::vector<uint32_t> spvCode = easyvk::readSpirv("vect-add.spv");
#endif

    easyvk::ComputeBindings binds;
    binds.addStorage(0, aBuf); // binding = 0 (uint32_t*)
    binds.addStorage(1, bBuf); // binding = 1 (float*)
    binds.addStorage(2, cBuf); // binding = 2 (float*)

    easyvk::ComputeProgramCreateInfo ci;
    ci.spirv = &spvCode;
    ci.localX = kLocalSize; // specialization constants 0/1/2
    ci.localY = 1;
    ci.localZ = 1;
    // Some clspv builds declare a small push-constant block;
    ci.pushConstantBytes = 16;
    ci.entryPointName = "litmus_test";
    ci.bindings = binds;

    easyvk::ComputeProgram program(device, ci);

    struct PC {
        uint32_t region_offset[3];
    };
    PC pc{{0, 0, 0}}; // typical case
    program.setPushConstants(pc);

    constexpr uint32_t groupsX = (kSize + kLocalSize - 1u) / kLocalSize;
    program.setWorkgroups(groupsX, 1, 1);

    // 4) Dispatch (with optional timestamp timing) ------------------------------
    std::cout << "Running program...\n";
    double ns = program.supportsTimestamps() ? program.dispatchWithTimingNs() : (program.dispatch(), 0.0);
    if (ns > 0.0) std::cout << "Completed in " << (ns / 1e6) << " ms\n";

    // 5) Read back and validate -------------------------------------------------
    std::vector<float> out(kSize);
    {
        auto mapC = cBuf.mapRead(0, cBuf.size());
        std::memcpy(out.data(), mapC.data(), cBuf.size());
    }

    for (uint32_t i = 0; i < kSize; ++i) {
        const float expect = static_cast<float>(i) + static_cast<float>(i + 1); // = 2*i + 1
        assert(std::fabs(out[i] - expect) < 1e-6f);
    }
    std::cout << "Validation passed!\n";
    return 0;
}
