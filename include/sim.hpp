#pragma once

#include <moss/extensions/imgui.hpp>
#include <moss/extensions/vulkan.hpp>
#include <string>

#include "block.hpp"
#include "params.hpp"
#include "ui.hpp"


inline std::string shader(const char* file) {
    return std::string("build/shader/") + file;
}

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
    static void addField(Mirror& mirror, const char* name, bool doubleBuffered) {
        mirror.create()
            .attach<vk::SimImage>({.name = name})
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
        if (repeat > 1) mirror.template attach<vk::Repeat>({.count = repeat});
        mirror.template attach<vk::PushConstants>(initial);
    }

    /* ---------------------------- Setup ---------------------------- */

    static void connectSystems(Mirror& mirror) {
        mirror.connect<
            vk::Core, vk::FrameBegin, vk::ImageSystem,
            ig::Begin, FluidUi, FluidBlockSystem,
            vk::PassSystem, ig::End, vk::FrameEnd
        >();
    }

    // Window, renderer and the values the panel edits
    static void createWindowAndParams(Mirror& mirror) {
        mirror.create()
            .attach<vk::WindowSettings>({.title = "fluid simulation", .resize = true})
            .attach<vk::RendererSettings>()
            .attach<vk::HotReload>({.pollMs = 500})
            .attach<ig::Settings>({.saveLayout = true, .darkTheme = true, .docking = true, .dockspace = true});
        mirror.create().attach<FluidParams>().attach<FluidState>();
    }

    // Velocity, pressure and dye are read and rewritten in the same pass, so they are
    // double-buffered. velocitySource holds the advected velocity, the fixed right-hand
    // side of the viscosity solve. obstacle holds the signed distance to the solid;
    // residual holds the divergence left after the projection
    static void createFields(Mirror& mirror) {
        addField(mirror, "velocity",       true);
        addField(mirror, "pressure",       true);
        addField(mirror, "dye",            true);
        addField(mirror, "divergence",     false);
        addField(mirror, "residual",       false);
        addField(mirror, "velocitySource", false);
        addField(mirror, "obstacle",       false);
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
