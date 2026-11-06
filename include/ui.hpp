#pragma once

#include <moss/extensions/imgui.hpp>
#include <cmath>

#include "params.hpp"


// The panel: raw ImGui calls that edit FluidParams in place
class FluidUi : public System {
public:
    void tick(const Key<key::READ>& key) override {
        auto [params, state] = cmd::Query<With<FluidParams, FluidState>>::init(key).single();

        ImGui::SetNextWindowPos(ImVec2(24.0f, 24.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(340.0f, 0.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Fluid");

        ImGui::TextDisabled("%.0f fps", ImGui::GetIO().Framerate);

        simulationSection(params);
        flowSection(params);
        obstacleSection(params);
        viewSection(params);
        readoutSection(params, state);

        ImGui::Separator();
        if (ImGui::Button("reset parameters")) {
            params = FluidParams{};
            params.resetRequested = false;   // parameters only, the simulation keeps running
        }

        ImGui::End();
    }

private:
    static void simulationSection(FluidParams& params) {
        ImGui::SeparatorText("Simulation");
        if (ImGui::Button("restart")) params.resetRequested = true;
        ImGui::SameLine();
        ImGui::Checkbox("pause", &params.paused);
        ImGui::SameLine();
        ImGui::BeginDisabled(!params.paused);
        if (ImGui::Button("step")) params.stepOnce = true;
        ImGui::EndDisabled();
    }

    static void flowSection(FluidParams& params) {
        ImGui::SeparatorText("Flow");
        ImGui::SliderFloat("inflow (cells/s)", &params.inflow, 0.0f, 120.0f);
        ImGui::SliderFloat("viscosity (cells^2/s)", &params.viscosity, 0.0f, 50.0f, "%.3f",
                           ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("timestep (s)", &params.dt, 0.002f, 0.03f, "%.4f");

        ImGui::SliderFloat("wake seed (U^2/D)", &params.seedAmplitude, 0.0f, 0.2f, "%.3f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Transverse push behind the obstacle for the first %.0f D/U after a restart.\n"
                              "0 keeps the flow perfectly symmetric: shedding may start very late or never.",
                              SEED_DURATION);

        const f32 step = params.inflow * params.dt;
        if (step > STEP_LIMIT)
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.3f, 1.0f), "inflow x dt = %.2f cells per step (smearing)", step);
        else
            ImGui::TextDisabled("inflow x dt = %.2f cells per step", step);
    }

    static void obstacleSection(FluidParams& params) {
        ImGui::SeparatorText("Obstacle");
        ImGui::SliderFloat("x (cells)", &params.obstacleX, 0.0f, f32(GRID_WIDTH));
        ImGui::SliderFloat("y (cells)", &params.obstacleY, 0.0f, f32(GRID_HEIGHT));
        ImGui::SliderFloat("radius (cells)", &params.obstacleRadius, 2.0f, 80.0f);
    }

    static void viewSection(FluidParams& params) {
        static const char* const viewNames[VIEW_COUNT] = {
            "dye", "speed", "pressure (q = dt * p)", "divergence (before projection)",
            "vorticity (x D/U)", "divergence (after projection)"
        };
        static const char* const paletteNames[PALETTE_COUNT] = {
            "mono", "ember", "ice", "ink"
        };

        ImGui::SeparatorText("View");
        ImGui::Combo("show", &params.view, viewNames, VIEW_COUNT);
        ImGui::Combo("dye palette", &params.palette, paletteNames, PALETTE_COUNT);
        ImGui::SliderFloat("dye stripe (cells)", &params.dyeStripe, 4.0f, 64.0f, "%.0f");

        const bool signedView = params.view != VIEW_DYE && params.view != VIEW_SPEED;
        ImGui::BeginDisabled(!signedView);
        ImGui::SliderFloat("gain", &params.gain, 0.001f, 100.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
        ImGui::EndDisabled();
    }

    // Everything here is derived from the parameters: no GPU readback involved
    static void readoutSection(const FluidParams& params, const FluidState& state) {
        const f32 diameter = 2.0f * params.obstacleRadius;

        // Linear interpolation shifted by a fraction f of a cell spreads the signal by f(1-f) cells^2
        // per step, i.e. a viscosity of f(1-f) / (2 dt). Exact for uniform flow, an estimate in the wake
        const f32 shift = params.inflow * params.dt;
        const f32 fraction = shift - std::floor(shift);
        const f32 numerical = params.dt > 0.0f ? 0.5f * fraction * (1.0f - fraction) / params.dt : 0.0f;
        const f32 effective = params.viscosity + numerical;

        ImGui::SeparatorText("Readouts");

        if (params.viscosity > 0.0f)
            ImGui::Text("Re nominal: %.0f", params.inflow * diameter / params.viscosity);
        else
            ImGui::Text("Re nominal: inf (no viscosity)");

        if (effective > 0.0f)
            ImGui::Text("Re effective (estimate): %.0f", params.inflow * diameter / effective);
        ImGui::TextDisabled("numerical viscosity ~ %.2f cells^2/s", numerical);

        ImGui::Text("t U / D = %.1f", f32(state.time) * params.inflow / diameter);
        ImGui::TextDisabled("D = %.0f cells, blockage %.1f %%", diameter, 100.0f * diameter / f32(GRID_HEIGHT));
        ImGui::TextDisabled("wake length %.1f D", (f32(GRID_WIDTH) - params.obstacleX) / diameter);
    }
};
