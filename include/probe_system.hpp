#pragma once

#include "params.hpp"
#include "probe_analysis.hpp"
#include "probe_data.hpp"

#include <moss/extensions/vulkan.hpp>
#include <moss/extensions/vulkan/logs.hpp>
#include <moss/meta/logs.hpp>

// Samples the velocity image at the probe cells every simulated frame.
//
// SimBuffers are GPU-only, so this system owns host-visible staging buffers, one per
// frame in flight. Each frame it (1) reads the slot it filled FRAME_OVERLAP frames ago,
// whose fence FrameBegin has already waited on, then (2) records a copy of the current
// velocity texels into that same slot. Connect it right after vk::PassSystem
class ProbeSystem : public System {
public:
    void build(const Key<key::WRITE>& key) override {
        vk::RenderCore& core = cmd::Resource<vk::RenderCore>::init(key).get();
        m_allocator = core.allocator;
        VmaAllocator allocator = m_allocator;

        for (Slot& slot : m_slots) {
            VkBufferCreateInfo bufferInfo = {};
            bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size = PROBE_READBACK_BYTES;
            bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VmaAllocationCreateInfo allocInfo = {};
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

            VmaAllocationInfo mapped = {};
            VK_CHECK(vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &slot.buffer, &slot.allocation, &mapped));
            M_FATAL_IF(mapped.pMappedData == nullptr, "ProbeSystem: the readback buffer is not host mapped");
            slot.mapped = static_cast<const f32*>(mapped.pMappedData);

            core.dqueue.push([allocator, buffer = slot.buffer, allocation = slot.allocation]() {
                vmaDestroyBuffer(allocator, buffer, allocation);
            });
        }
    }

    void tick(const Key<key::READ>& key) override {
        vk::RenderCore& core = cmd::Resource<vk::RenderCore>::init(key).get();
        vk::GpuResources& gpu = cmd::Resource<vk::GpuResources>::init(key).get();
        auto [params, state, probe] = cmd::Query<With<FluidParams, FluidState, FluidProbe>>::init(key).single();

        Slot& slot = m_slots[core.frame.frameNumber % vk::RenderCore::FrameData::FRAME_OVERLAP];

        collect(slot, probe);
        record(core, gpu, params, state, slot);

        if (core.frame.frameNumber % ANALYSIS_INTERVAL == 0)
            probe.result = analyseProbe(probe.samples, probe.analyseFrom, probe.config.inflow, 2.0f * probe.config.radius);
    }

private:
    struct Slot {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        const f32* mapped = nullptr;

        // What the pending copy in this slot belongs to
        bool pending = false;
        double time = 0.0;
        ProbeConfig config;
    };

    // Moves the finished copy of this slot into the sample history
    void collect(Slot& slot, FluidProbe& probe) {
        if (!slot.pending) return;
        slot.pending = false;

        // The mapping may be non-coherent: make the device writes visible to the host
        vmaInvalidateAllocation(m_allocator, slot.allocation, 0, VK_WHOLE_SIZE);

        // A change of physics or a restart starts a fresh history
        if (!(slot.config == probe.config)) {
            probe.samples.clear();
            probe.result = ProbeResult{};
            probe.config = slot.config;
        }

        ProbeSample sample;
        sample.time = slot.time;
        for (u32 i = 0; i < PROBE_COUNT; i++) {
            const f32* texel = slot.mapped + i * (PROBE_STRIDE_BYTES / sizeof(f32));
            sample.velocity[i][0] = texel[0];
            sample.velocity[i][1] = texel[1];
        }

        probe.samples.push_back(sample);
        while (probe.samples.size() > PROBE_CAPACITY) probe.samples.pop_front();
    }

    // Records the copy of this frame's final velocity into the slot
    void record(vk::RenderCore& core, vk::GpuResources& gpu, const FluidParams& params, const FluidState& state, Slot& slot) {
        if (!state.stepped) return;   // paused: nothing new to sample

        auto found = gpu.images.find("velocity");
        M_FATAL_IF(found == gpu.images.end(), "ProbeSystem needs a SimImage named 'velocity'");

        // PassSystem already ran, so readSide is the side the last writing pass produced
        const vk::AllocatedImage& source = found->second.sides[found->second.readSide];

        VkBufferImageCopy regions[PROBE_COUNT] = {};
        for (u32 i = 0; i < PROBE_COUNT; i++) {
            const ProbeCell cell = probeCell(params.obstacleX, params.obstacleY, params.obstacleRadius, i);
            regions[i].bufferOffset = i * PROBE_STRIDE_BYTES;
            regions[i].imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            regions[i].imageOffset = { i32(cell.x), i32(cell.y), 0 };
            regions[i].imageExtent = { 1, 1, 1 };
        }

        VkCommandBuffer cmdBuf = core.frame.cmd;
        vk::utils::syncAll(cmdBuf);   // the passes' shader writes before the copy reads
        vkCmdCopyImageToBuffer(cmdBuf, source.image, VK_IMAGE_LAYOUT_GENERAL, slot.buffer, PROBE_COUNT, regions);

        slot.pending = true;
        slot.time = state.time;
        slot.config = makeProbeConfig(params, state);
    }

    VmaAllocator m_allocator = nullptr;
    Slot m_slots[vk::RenderCore::FrameData::FRAME_OVERLAP];
};
