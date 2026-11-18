#pragma once

#include "params.hpp"
#include "probe_data.hpp"

#include <moss/extensions/imgui.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <vector>

// The panel: raw ImGui calls that edit FluidParams in place
class FluidUi : public System {
public:
    void tick(const Key<key::READ>& key) override {
        auto [params, state, probe] = cmd::Query<With<FluidParams, FluidState, FluidProbe>>::init(key).single();

        shortcuts(params);

        ImGui::SetNextWindowPos(ImVec2(24.0f, 24.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(340.0f, 0.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Fluid");

        ImGui::TextDisabled("%.0f fps", ImGui::GetIO().Framerate);

        simulationSection(params);
        flowSection(params);
        obstacleSection(params);
        viewSection(params);

        // After every section, so the block sees a viscosity that matches this frame's edits
        deriveViscosity(params);
        readoutSection(params, state);
        probeSection(params, probe);

        ImGui::Separator();
        if (ImGui::Button("reset parameters")) {
            params = FluidParams{};
            params.resetRequested = false;   // parameters only, the simulation keeps running
        }

        ImGui::End();
    }

private:
    /* ---------------------------- Helpers ---------------------------- */

    static f32 diameter(const FluidParams& params) { return 2.0f * params.obstacleRadius; }

    static void shortcuts(FluidParams& params) {
        if (ImGui::GetIO().WantTextInput) return;
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) params.paused = !params.paused;
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) params.resetRequested = true;
        if (params.paused && ImGui::IsKeyPressed(ImGuiKey_Period)) params.stepOnce = true;
    }

    static void deriveViscosity(FluidParams& params) {
        if (params.useReynolds)
            params.viscosity = params.inflow * diameter(params) / params.reynolds;
    }

    /* ---------------------------- Sections ---------------------------- */

    static void simulationSection(FluidParams& params) {
        ImGui::SeparatorText("Simulation");
        if (ImGui::Button("restart")) params.resetRequested = true;
        ImGui::SameLine();
        ImGui::Checkbox("pause", &params.paused);
        ImGui::SameLine();
        ImGui::BeginDisabled(!params.paused);
        if (ImGui::Button("step")) params.stepOnce = true;
        ImGui::EndDisabled();
        ImGui::TextDisabled("space: pause   R: restart   . : step");
    }

    static void flowSection(FluidParams& params) {
        static const char* const advectionNames[ADVECT_COUNT] = {
            "bilinear, 1 backtrace", "cubic (Catmull-Rom), RK2 backtrace"
        };
        static constexpr f32 presets[] = { 20.0f, 47.0f, 100.0f, 200.0f };

        ImGui::SeparatorText("Flow");
        ImGui::SliderFloat("inflow (cells/s)", &params.inflow, 0.0f, 120.0f);

        ImGui::Checkbox("set viscosity from Reynolds number", &params.useReynolds);
        if (params.useReynolds) {
            ImGui::SliderFloat("Reynolds number", &params.reynolds, 10.0f, 1000.0f, "%.0f",
                               ImGuiSliderFlags_Logarithmic);
            for (const f32 preset : presets) {
                char label[16];
                std::snprintf(label, sizeof(label), "Re %.0f", preset);
                if (ImGui::Button(label)) params.reynolds = preset;
                if (preset != presets[std::size(presets) - 1]) ImGui::SameLine();
            }
            ImGui::TextDisabled("viscosity = %.3f cells^2/s", params.viscosity);
        } else {
            ImGui::SliderFloat("viscosity (cells^2/s)", &params.viscosity, 0.0f, 50.0f, "%.3f",
                               ImGuiSliderFlags_Logarithmic);
        }

        ImGui::SliderFloat("timestep (s)", &params.dt, 0.002f, 0.03f, "%.4f");
        ImGui::Combo("advection", &params.advection, advectionNames, ADVECT_COUNT);

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
        ImGui::SeparatorText("Readouts");

        if (params.viscosity > 0.0f)
            ImGui::Text("Re nominal: %.0f", params.inflow * diameter(params) / params.viscosity);
        else
            ImGui::Text("Re nominal: inf (no viscosity)");

        // Linear interpolation shifted by a fraction f of a cell spreads the signal by f(1-f) cells^2
        // per step, i.e. a viscosity of f(1-f) / (2 dt). Exact for uniform flow, an estimate in the wake.
        // Only valid for the bilinear scheme
        if (params.advection == ADVECT_BILINEAR) {
            const f32 shift = params.inflow * params.dt;
            const f32 fraction = shift - std::floor(shift);
            const f32 numerical = params.dt > 0.0f ? 0.5f * fraction * (1.0f - fraction) / params.dt : 0.0f;
            const f32 effective = params.viscosity + numerical;
            if (effective > 0.0f)
                ImGui::Text("Re effective (estimate): %.0f", params.inflow * diameter(params) / effective);
            ImGui::TextDisabled("numerical viscosity ~ %.2f cells^2/s", numerical);
        } else {
            ImGui::TextDisabled("numerical viscosity: not estimated for cubic advection");
        }

        // Jacobi on (I - alpha laplacian) u = b shrinks the error by 4a/(1+4a) per sweep
        const f32 alpha = params.viscosity * params.dt;
        const f32 rate = 4.0f * alpha / (1.0f + 4.0f * alpha);
        const f32 remaining = std::pow(rate, f32(VISCOSITY_ITERATIONS));
        if (remaining > VISCOSITY_ERROR_LIMIT)
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.3f, 1.0f),
                               "viscosity solve: %.0f %% error left (raise VISCOSITY_ITERATIONS)", 100.0f * remaining);
        else
            ImGui::TextDisabled("viscosity solve: %.1f %% error left", 100.0f * remaining);

        ImGui::Text("t U / D = %.1f", f32(state.time) * params.inflow / diameter(params));
        ImGui::TextDisabled("D = %.0f cells, blockage %.1f %%", diameter(params), 100.0f * diameter(params) / f32(GRID_HEIGHT));
        ImGui::TextDisabled("wake length %.1f D", (f32(GRID_WIDTH) - params.obstacleX) / diameter(params));
    }

    // Shedding frequency from the velocity sampled behind the obstacle (see ProbeSystem)
    static void probeSection(const FluidParams& params, FluidProbe& probe) {
        ImGui::SeparatorText("Wake probe");

        ImGui::SliderFloat("analyse from t U / D", &probe.analyseFrom, 0.0f, 300.0f, "%.0f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Samples before this time are ignored, so the start-up transient\n"
                              "does not pollute the frequency. The history clears on a restart\n"
                              "or when any flow parameter changes.");

        // Last stretch of the transverse velocity at the primary probe, in units of U
        static std::vector<f32> trace;
        trace.clear();
        const size_t count = std::min<size_t>(probe.samples.size(), 600);
        const f32 inflow = std::max(params.inflow, 1.0f);
        for (size_t i = probe.samples.size() - count; i < probe.samples.size(); i++)
            trace.push_back(probe.samples[i].velocity[PROBE_PRIMARY][1] / inflow);
        ImGui::PlotLines("##trace", trace.data(), int(trace.size()), 0, "v / U at the probe",
                         FLT_MAX, FLT_MAX, ImVec2(0.0f, 80.0f));

        const ProbeResult& result = probe.result;
        switch (result.status) {
        case ProbeStatus::OK:
            ImGui::Text("St (FFT): %.4f", result.strouhal);
            if (result.strouhalCrossings > 0.0f)
                ImGui::Text("St (zero crossings): %.4f  (%u periods)", result.strouhalCrossings, result.periods);
            else
                ImGui::TextDisabled("St (zero crossings): too few periods");
            ImGui::TextDisabled("f = %.3f Hz, rms v/U = %.3f, window %.1f D/U",
                                result.frequency, result.amplitude, result.window);
            break;
        case ProbeStatus::NO_SHEDDING:
            ImGui::TextDisabled("no shedding detected (rms v/U = %.4f)", result.amplitude);
            break;
        case ProbeStatus::INVALID:
            ImGui::TextDisabled("needs inflow > 0 and a longer window");
            break;
        case ProbeStatus::COLLECTING:
            ImGui::TextDisabled("collecting samples: %u of %zu", result.samples, size_t(ANALYSIS_MIN_SAMPLES));
            break;
        }

        const ProbeCell cell = probeCell(params.obstacleX, params.obstacleY, params.obstacleRadius, PROBE_PRIMARY);
        ImGui::TextDisabled("primary probe: cell (%u, %u), %.1f D downstream",
                            cell.x, cell.y, PROBE_OFFSETS[PROBE_PRIMARY].x);

        if (ImGui::Button("export csv")) probe.exportStatus = exportProbeCsv(probe);
        ImGui::SameLine();
        if (ImGui::Button("clear history")) {
            probe.samples.clear();
            probe.result = ProbeResult{};
        }
        if (!probe.exportStatus.empty()) ImGui::TextDisabled("%s", probe.exportStatus.c_str());
    }
};
