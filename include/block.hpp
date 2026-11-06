#pragma once

#include <moss/extensions/vulkan.hpp>
#include "params.hpp"


// Copies FluidParams into the push block of every pass. Connect it after the
// UI (an edit applies in the same frame) and before PassSystem
class FluidBlockSystem : public System {
public:
    void tick(const Key<key::READ>& key) override {
        vk::RenderCore& core = cmd::Resource<vk::RenderCore>::init(key).get();
        auto [params, state] = cmd::Query<With<FluidParams, FluidState>>::init(key).single();

        // Paused means dt = 0, except for the one frame a step was asked for
        const bool frozen = params.paused && !params.stepOnce;

        if (params.resetRequested) state.time = 0.0;

        FluidBlock block;
        block.dt = frozen ? 0.0f : params.dt;
        block.inflow = params.inflow;
        block.obstacleX = params.obstacleX;
        block.obstacleY = params.obstacleY;
        block.obstacleRadius = params.obstacleRadius;
        block.gain = params.gain;
        block.view = params.view;
        // The draw image follows the window, so read its size every frame
        block.windowWidth = static_cast<f32>(core.drawImage.imageExtent.width);
        block.windowHeight = static_cast<f32>(core.drawImage.imageExtent.height);
        block.viscosity = params.viscosity;
        block.dyeStripe = params.dyeStripe;
        block.reset = params.resetRequested ? 1 : 0;
        block.palette = params.palette;
        block.time = static_cast<f32>(state.time);
        block.perturbation = params.seedAmplitude;

        // One-shot requests: the shaders see them this frame, then they are cleared
        params.resetRequested = false;
        params.stepOnce = false;
        if (!frozen) state.time += params.dt;

        cmd::Query<With<vk::PushConstants>>::init(key).each(
            [&](Entity, vk::PushConstants& pc) { pc.as<FluidBlock>() = block; }
        );
    }
};
