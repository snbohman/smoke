#pragma once

#include "params.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <string>

/* ------------------------------ Constants ------------------------------ */

// Probe positions relative to the obstacle centre, in obstacle diameters (D)
struct ProbeOffset { f32 x; f32 y; };
constexpr u32 PROBE_COUNT = 3;
constexpr u32 PROBE_PRIMARY = 1;   // the probe the Strouhal number is computed from
constexpr ProbeOffset PROBE_OFFSETS[PROBE_COUNT] = { {1.5f, 0.5f}, {3.0f, 0.5f}, {5.0f, 0.5f} };

constexpr u32 PROBE_CAPACITY = 16384;      // samples kept (one per simulated frame)
constexpr u32 ANALYSIS_INTERVAL = 60;      // frames between two analyses

// One copied texel. Sized for rgba32f so it also holds an rg32f velocity texel
constexpr u32 PROBE_STRIDE_BYTES = 16;
constexpr u32 PROBE_READBACK_BYTES = PROBE_COUNT * PROBE_STRIDE_BYTES;

/* ------------------------------ Components ------------------------------ */

struct ProbeSample {
    double time = 0.0;                     // simulated seconds since the last restart
    f32 velocity[PROBE_COUNT][2] = {};     // (u, v) in cells/s
};

// Everything that changes the physics. The sample history is cleared when it changes
struct ProbeConfig {
    u32 epoch = 0;                         // restart counter
    f32 inflow = 0.0f;
    f32 radius = 0.0f;
    f32 obstacleX = 0.0f;
    f32 obstacleY = 0.0f;
    f32 viscosity = 0.0f;
    f32 dt = 0.0f;
    i32 advection = 0;

    bool operator==(const ProbeConfig&) const = default;
};

enum class ProbeStatus { COLLECTING, NO_SHEDDING, OK, INVALID };

struct ProbeResult {
    ProbeStatus status = ProbeStatus::COLLECTING;
    u32 samples = 0;                       // samples inside the analysis window
    f32 window = 0.0f;                     // window length in D/U
    f32 amplitude = 0.0f;                  // rms of v / U
    f32 frequency = 0.0f;                  // Hz, FFT peak
    f32 strouhal = 0.0f;                   // from the FFT peak
    f32 strouhalCrossings = 0.0f;          // from the mean upward zero-crossing period; 0 = too few
    u32 periods = 0;                       // periods the crossing estimate averaged over
};

// Wake probe state on the same entity as FluidParams
struct FluidProbe {
    std::deque<ProbeSample> samples;
    ProbeConfig config;
    ProbeResult result;
    f32 analyseFrom = 40.0f;               // start of the analysis window, in t U / D
    std::string exportStatus;
};

/* ------------------------------ Helpers ------------------------------ */

struct ProbeCell { u32 x; u32 y; };

inline ProbeCell probeCell(f32 obstacleX, f32 obstacleY, f32 radius, u32 index) {
    const f32 diameter = 2.0f * radius;
    const f32 x = std::round(obstacleX + PROBE_OFFSETS[index].x * diameter);
    const f32 y = std::round(obstacleY + PROBE_OFFSETS[index].y * diameter);
    return {
        u32(std::clamp(x, 0.0f, f32(GRID_WIDTH - 1))),
        u32(std::clamp(y, 0.0f, f32(GRID_HEIGHT - 1)))
    };
}

inline ProbeConfig makeProbeConfig(const FluidParams& params, const FluidState& state) {
    return {
        .epoch = state.epoch,
        .inflow = params.inflow,
        .radius = params.obstacleRadius,
        .obstacleX = params.obstacleX,
        .obstacleY = params.obstacleY,
        .viscosity = params.viscosity,
        .dt = params.dt,
        .advection = params.advection
    };
}

// Writes every stored sample to probe_Re<N>.csv in the working directory.
// Returns a message for the panel: a failed export is not worth killing the run
inline std::string exportProbeCsv(const FluidProbe& probe) {
    const ProbeConfig& c = probe.config;
    const f32 diameter = 2.0f * c.radius;

    char name[64];
    if (c.viscosity > 0.0f)
        std::snprintf(name, sizeof(name), "probe_Re%.0f.csv", c.inflow * diameter / c.viscosity);
    else
        std::snprintf(name, sizeof(name), "probe_Reinf.csv");

    std::FILE* file = std::fopen(name, "w");
    if (!file) return std::string("could not open ") + name;

    std::fprintf(file, "# inflow_cells_per_s=%g diameter_cells=%g viscosity=%g dt=%g advection=%d\n",
                 c.inflow, diameter, c.viscosity, c.dt, c.advection);
    for (u32 i = 0; i < PROBE_COUNT; i++) {
        const ProbeCell cell = probeCell(c.obstacleX, c.obstacleY, c.radius, i);
        std::fprintf(file, "# probe %u at cell (%u, %u)\n", i, cell.x, cell.y);
    }
    std::fprintf(file, "# St = f D / U, with f in 1/s of simulated time\n");

    std::fprintf(file, "time_s,tU_D");
    for (u32 i = 0; i < PROBE_COUNT; i++) std::fprintf(file, ",u%u,v%u", i, i);
    std::fprintf(file, "\n");

    for (const ProbeSample& s : probe.samples) {
        std::fprintf(file, "%.6f,%.4f", s.time, s.time * c.inflow / diameter);
        for (u32 i = 0; i < PROBE_COUNT; i++)
            std::fprintf(file, ",%.6g,%.6g", s.velocity[i][0], s.velocity[i][1]);
        std::fprintf(file, "\n");
    }
    std::fclose(file);

    return "wrote " + std::to_string(probe.samples.size()) + " samples to " + name;
}
