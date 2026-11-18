#pragma once

#include "block.hpp"
#include "params.hpp"
#include "probe_data.hpp"
#include "probe_system.hpp"
#include "ui.hpp"

#include <moss/extensions/imgui.hpp>
#include <moss/extensions/vulkan.hpp>

#include <string>

inline std::string shader(const char* file) {
    return std::string("build/shader/") + file;
}

// Storage formats of the sim images. Keep in step with FIELD_VEC2 / FIELD_SCALAR / FIELD_DYE
// in shaders/common.glsl (rg32f / r32f / rgba16f)
constexpr VkFormat FORMAT_VEC2   = VK_FORMAT_R32G32_SFLOAT;
constexpr VkFormat FORMAT_SCALAR = VK_FORMAT_R32_SFLOAT;
constexpr VkFormat FORMAT_DYE    = VK_FORMAT_R16G16B16A16_SFLOAT;

class FluidContext : public Context {
public:
    void init(Mirror& mirror) override {
        connectSystems(mirror);
        createWindowAndParams(mirror);
        createFields(mirror);
        createPasses(mirror);
    }

private:
    /* ---------------------------- Helpers ---------------------------- */

    // One entity per sim image, all the same size
    static void addField(Mirror& mirror, const char* name, VkFormat format, bool doubleBuffered) {
        mirror.create()
            .attach<vk::SimImage>({.name = name, .format = format})
            .attach<vk::ImageExtent>({.width = GRID_WIDTH, .height = GRID_HEIGHT});
        if (doubleBuffered) mirror.attach<vk::DoubleBuffered>();
    }

    // One compute pass over the size of `sizedBy`, run `repeat` times per frame
    template <class... Binds>
    static void addCompute(Mirror& mirror, const char* file, const char* sizedBy, u32 repeat, Binds... binds) {
        const FluidBlock initial {};   // overwritten every frame by FluidBlockSystem

        mirror.create()
            .template attach<vk::ComputeShader>({.path = shader(file)})
            .template attach<vk::Bind>(binds...)
            .template attach<vk::Dispatch>(sizedBy);
        if (repeat > 1) mirror.attach<vk::Repeat>({.count = repeat});
        mirror.attach<vk::PushConstants>(initial);
    }

    /* ---------------------------- Setup ---------------------------- */

    // ProbeSystem copies the finished velocity, so it sits right after PassSystem
    static void connectSystems(Mirror& mirror) {
        mirror.connect<
            vk::Core, vk::FrameBegin, vk::ImageSystem,
            ig::Begin, FluidUi, FluidBlockSystem,
            vk::PassSystem, ProbeSystem, ig::End, vk::FrameEnd
        >();
    }

    // Window, renderer and the values the panel edits
    static void createWindowAndParams(Mirror& mirror) {
        mirror.create()
            .attach<vk::WindowSettings>({.title = "fluid simulation", .resize = true})
            .attach<vk::RendererSettings>()
            .attach<vk::HotReload>({.pollMs = 500})
            .attach<ig::Settings>({.saveLayout = true, .darkTheme = true, .docking = true, .dockspace = true});
        mirror.create().attach<FluidParams>().attach<FluidState>().attach<FluidProbe>();
    }

    // Velocity, pressure and dye are read and rewritten in the same pass, so they are
    // double-buffered. velocitySource holds the advected velocity, the fixed right-hand
    // side of the viscosity solve. obstacle holds the signed distance to the solid;
    // residual holds the divergence left after the projection
    static void createFields(Mirror& mirror) {
        addField(mirror, "velocity",       FORMAT_VEC2,   true);
        addField(mirror, "pressure",       FORMAT_SCALAR, true);
        addField(mirror, "dye",            FORMAT_DYE,    true);
        addField(mirror, "divergence",     FORMAT_SCALAR, false);
        addField(mirror, "residual",       FORMAT_SCALAR, false);
        addField(mirror, "velocitySource", FORMAT_VEC2,   false);
        addField(mirror, "obstacle",       FORMAT_SCALAR, false);
    }

    // Passes run in creation order
    static void createPasses(Mirror& mirror) {
        // Signed distance to the obstacle: negative inside. Read by every pass below
        addCompute(mirror, "paint_obstacle.comp.spv", "obstacle", 1,
            vk::write(0, "obstacle"));

        // Advect velocity along itself, apply inflow, obstacle and wake seed.
        // On a restart it writes the uniform inflow state instead
        addCompute(mirror, "advect_velocity.comp.spv", "velocity", 1,
            vk::sample(0, "velocity"), vk::write(1, "velocity"), vk::write(2, "velocitySource"),
            vk::sample(3, "obstacle"));

        // Viscosity: Jacobi sweeps of (I - nu*dt*laplacian) u = source
        addCompute(mirror, "viscosity.comp.spv", "velocity", VISCOSITY_ITERATIONS,
            vk::read(0, "velocity"), vk::read(1, "velocitySource"), vk::write(2, "velocity"),
            vk::sample(3, "obstacle"));

        // Divergence of the diffused velocity
        addCompute(mirror, "divergence.comp.spv", "divergence", 1,
            vk::sample(0, "velocity"), vk::write(1, "divergence"), vk::sample(2, "obstacle"));

        // Pressure: Jacobi sweeps solving for q = dt * p. The sides swap after each one
        addCompute(mirror, "pressure.comp.spv", "pressure", JACOBI_ITERATIONS,
            vk::read(0, "pressure"), vk::read(1, "divergence"), vk::write(2, "pressure"),
            vk::sample(3, "obstacle"));

        // Project: subtract the pressure gradient from the velocity
        addCompute(mirror, "project.comp.spv", "velocity", 1,
            vk::sample(0, "velocity"), vk::read(1, "pressure"), vk::write(2, "velocity"),
            vk::sample(3, "obstacle"));

        // Divergence of the projected velocity: what the solver failed to remove
        addCompute(mirror, "divergence.comp.spv", "residual", 1,
            vk::sample(0, "velocity"), vk::write(1, "residual"), vk::sample(2, "obstacle"));

        // Advect dye with the final velocity
        addCompute(mirror, "advect_dye.comp.spv", "dye", 1,
            vk::sample(0, "velocity"), vk::sample(1, "dye"), vk::write(2, "dye"),
            vk::sample(3, "obstacle"));

        // Display into the draw image
        const FluidBlock initial {};
        mirror.create()
            .attach<vk::VertexShader>({.path = shader("fullscreen.vert.spv")})
            .attach<vk::FragmentShader>({.path = shader("display.frag.spv")})
            .attach<vk::Bind>(
                vk::sample(0, "dye"), vk::sample(1, "velocity"),
                vk::sample(2, "pressure"), vk::sample(3, "divergence"),
                vk::sample(4, "residual"), vk::sample(5, "obstacle")
            )
            .attach<vk::PushConstants>(initial);
    }
};
