#pragma once

#include <moss/moss.hpp>

using namespace moss;

/* ------------------------------ Constants ------------------------------ */

// Fixed at build time: sim images do not follow the window
constexpr u32 GRID_WIDTH  = 512;
constexpr u32 GRID_HEIGHT = 256;

// Sweeps per frame. A Repeat count is fixed at build time,
// so these are constants, not imgui sliders
constexpr u32 JACOBI_ITERATIONS = 20;      // pressure solve
constexpr u32 VISCOSITY_ITERATIONS = 20;   // viscosity solve

// inflow * dt above this many cells per step smears the semi-Lagrangian advection
constexpr f32 STEP_LIMIT = 1.0f;

// How long the wake seed acts after a restart, in D/U. Same value as SEED_DURATION in common.glsl
constexpr f32 SEED_DURATION = 2.0f;

/* ------------------------------ Enums ------------------------------ */

// Same order as the VIEW_* / PALETTE_* constants in common.glsl. Only append
enum ViewMode : i32 {
    VIEW_DYE, VIEW_SPEED, VIEW_PRESSURE, VIEW_DIVERGENCE, VIEW_VORTICITY, VIEW_RESIDUAL, VIEW_COUNT
};
enum Palette : i32 { PALETTE_MONO, PALETTE_EMBER, PALETTE_ICE, PALETTE_INK, PALETTE_COUNT };

/* ------------------------------ Components ------------------------------ */

// What the user edits (plain data on an entity). Grid units: cells, cells/s, s
struct FluidParams {
    bool paused = false;                   // dt is sent as 0: the picture freezes
    bool stepOnce = false;                 // while paused: run one frame, then clear
    bool resetRequested = true;            // restart the simulation; true = on the first frame
    f32 dt = 1.0f / 60.0f;                 // timestep; fixed, not the frame time
    f32 inflow = 40.0f;                    // inflow speed at the left edge
    f32 viscosity = 0.0f;                  // kinematic viscosity nu, cells^2 / s
    f32 seedAmplitude = 0.05f;             // wake seed strength in U^2 / D; 0 = perfectly symmetric
    f32 obstacleX = GRID_WIDTH * 0.25f;    // circle centre, in cells
    f32 obstacleY = GRID_HEIGHT * 0.5f;
    f32 obstacleRadius = 20.0f;            // in cells
    f32 dyeStripe = 16.0f;                 // height of one dye stripe at the inflow, in cells
    i32 view = VIEW_DYE;
    i32 palette = PALETTE_EMBER;
    f32 gain = 0.1f;                       // brightness of the signed views
};

// What the simulation has done so far. Written by FluidBlockSystem, read by the UI
struct FluidState {
    double time = 0.0;                     // simulated seconds since the last restart
};

// What the shaders read. Same member order as `Push` in common.glsl (60 bytes).
// Members are only ever appended
struct FluidBlock {
    f32 dt = 1.0f / 60.0f;
    f32 inflow = 40.0f;
    f32 obstacleX = 128.0f;
    f32 obstacleY = 128.0f;
    f32 obstacleRadius = 20.0f;
    f32 gain = 0.1f;
    i32 view = VIEW_DYE;
    f32 windowWidth = 1280.0f;
    f32 windowHeight = 720.0f;
    f32 viscosity = 0.0f;
    f32 dyeStripe = 16.0f;
    i32 reset = 0;
    i32 palette = PALETTE_EMBER;
    f32 time = 0.0f;
    f32 perturbation = 0.05f;
};

static_assert(sizeof(FluidBlock) == 60, "FluidBlock must match the Push block in common.glsl");
