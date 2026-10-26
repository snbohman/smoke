#include <moss/extensions/vulkan.hpp>
#include <moss/extensions/imgui.hpp>
#include <moss/meta/logs.hpp>
#include <moss/moss.hpp>

#include <string>

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

std::string shader(const char* file) {
    return std::string("build/shader/") + file;
}

/* ------------------------------ Parameters ------------------------------ */

enum ViewMode : i32 { VIEW_DYE, VIEW_SPEED, VIEW_PRESSURE, VIEW_DIVERGENCE, VIEW_COUNT };
enum Palette : i32 { PALETTE_MONO, PALETTE_EMBER, PALETTE_ICE, PALETTE_INK, PALETTE_COUNT };

// What the user edits (plain data on an entity). Grid units: cells, cells/s, s
struct FluidParams {
    bool paused = false;                   // dt is sent as 0: the picture freezes
    bool stepOnce = false;                 // while paused: run one frame, then clear
    bool resetRequested = true;            // restart the simulation; true = on the first frame
    f32 dt = 1.0f / 60.0f;                 // timestep; fixed, not the frame time
    f32 inflow = 40.0f;                    // inflow speed at the left edge
    f32 viscosity = 0.0f;                  // kinematic viscosity nu, cells^2 / s
    f32 obstacleX = GRID_WIDTH * 0.25f;    // circle centre, in cells
    f32 obstacleY = GRID_HEIGHT * 0.5f;
    f32 obstacleRadius = 20.0f;            // in cells
    f32 dyeStripe = 16.0f;                 // height of one dye stripe at the inflow, in cells
    i32 view = VIEW_DYE;
    i32 palette = PALETTE_EMBER;
    f32 gain = 0.1f;                       // brightness of the pressure and divergence views
};

// What the shaders read. Same member order as `Push` in the fluid shaders (52 bytes).
// Members are only ever appended: shaders that stop at an earlier member stay valid
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
};

static_assert(sizeof(FluidBlock) == 52, "FluidBlock must match the Push block of the shaders");

/* ------------------------------ Systems ------------------------------ */

// The panel: raw ImGui calls that edit FluidParams in place
class FluidUi : public System {
public:
    void tick(const Key<key::READ>& key) override {
        auto [params] = cmd::Query<With<FluidParams>>::init(key).single();

        static const char* const viewNames[VIEW_COUNT] = {
            "dye", "speed", "pressure", "divergence (before projection)"
        };
        static const char* const paletteNames[PALETTE_COUNT] = {
            "mono", "ember", "ice", "ink"
        };

        ImGui::SetNextWindowPos(ImVec2(24.0f, 24.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(340.0f, 0.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Fluid");

        ImGui::TextDisabled("%.0f fps", ImGui::GetIO().Framerate);

        ImGui::SeparatorText("Simulation");
        if (ImGui::Button("restart")) params.resetRequested = true;
        ImGui::SameLine();
        ImGui::Checkbox("pause", &params.paused);
        ImGui::SameLine();
        ImGui::BeginDisabled(!params.paused);
        if (ImGui::Button("step")) params.stepOnce = true;
        ImGui::EndDisabled();

        ImGui::SeparatorText("Flow");
        ImGui::SliderFloat("inflow (cells/s)", &params.inflow, 0.0f, 120.0f);
        ImGui::SliderFloat("viscosity (cells^2/s)", &params.viscosity, 0.0f, 50.0f, "%.3f",
                           ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("timestep (s)", &params.dt, 0.002f, 0.03f, "%.4f");

        const f32 step = params.inflow * params.dt;
        if (step > STEP_LIMIT)
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.3f, 1.0f), "inflow x dt = %.2f cells per step (smearing)", step);
        else
            ImGui::TextDisabled("inflow x dt = %.2f cells per step", step);

        // Re = U * D / nu, with D the obstacle diameter
        if (params.viscosity > 0.0f)
            ImGui::Text("Reynolds number: %.0f", params.inflow * 2.0f * params.obstacleRadius / params.viscosity);
        else
            ImGui::Text("Reynolds number: inf (no viscosity)");

        ImGui::SeparatorText("Obstacle");
        ImGui::SliderFloat("x (cells)", &params.obstacleX, 0.0f, f32(GRID_WIDTH));
        ImGui::SliderFloat("y (cells)", &params.obstacleY, 0.0f, f32(GRID_HEIGHT));
        ImGui::SliderFloat("radius (cells)", &params.obstacleRadius, 2.0f, 80.0f);

        ImGui::SeparatorText("View");
        ImGui::Combo("show", &params.view, viewNames, VIEW_COUNT);
        ImGui::Combo("dye palette", &params.palette, paletteNames, PALETTE_COUNT);
        ImGui::SliderFloat("dye stripe (cells)", &params.dyeStripe, 4.0f, 64.0f, "%.0f");

        const bool signedView = params.view == VIEW_PRESSURE || params.view == VIEW_DIVERGENCE;
        ImGui::BeginDisabled(!signedView);
        ImGui::SliderFloat("gain", &params.gain, 0.001f, 100.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
        ImGui::EndDisabled();

        ImGui::Separator();
        if (ImGui::Button("reset parameters")) {
            params = FluidParams{};
            params.resetRequested = false;   // parameters only, the simulation keeps running
        }

        ImGui::End();
    }
};

// Copies FluidParams into the push block of every pass. Connect it after the
// UI (an edit applies in the same frame) and before PassSystem
class FluidBlockSystem : public System {
public:
    void tick(const Key<key::READ>& key) override {
        vk::RenderCore& core = cmd::Resource<vk::RenderCore>::init(key).get();
        auto [params] = cmd::Query<With<FluidParams>>::init(key).single();

        // Paused means dt = 0, except for the one frame a step was asked for
        const bool frozen = params.paused && !params.stepOnce;

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

        // One-shot requests: the shaders see them this frame, then they are cleared
        params.resetRequested = false;
        params.stepOnce = false;

        cmd::Query<With<vk::PushConstants>>::init(key).each(
            [&](Entity, vk::PushConstants& pc) { pc.as<FluidBlock>() = block; }
        );
    }
};

/* ------------------------------ Context ------------------------------ */

// One entity per sim image, all the same size
void addField(Mirror& mirror, const char* name, bool doubleBuffered) {
    mirror.create()
        .attach<vk::SimImage>({.name = name})
        .attach<vk::ImageExtent>({.width = GRID_WIDTH, .height = GRID_HEIGHT});
    if (doubleBuffered) mirror.attach<vk::DoubleBuffered>();
}

class FluidContext : public Context {
public:
    void init(Mirror& mirror) override {
        const FluidBlock initial {};   // overwritten every frame by FluidBlockSystem

        mirror.connect<
            vk::Core, vk::FrameBegin, vk::ImageSystem,
            ig::Begin, FluidUi, FluidBlockSystem,
            vk::PassSystem, ig::End, vk::FrameEnd
        >();

        // Window, renderer and the values the panel edits
        mirror.create()
            .attach<vk::WindowSettings>({.title = "fluid simulation", .resize = true})
            .attach<vk::RendererSettings>()
            .attach<vk::HotReload>({.pollMs = 500})
            .attach<ig::Settings>({.saveLayout = true, .darkTheme = true, .docking = true, .dockspace = true});
        mirror.create().attach<FluidParams>();

        // Sim images. Velocity, pressure and dye are read and rewritten in
        // the same pass, so they are double-buffered. velocitySource holds the
        // advected velocity, the fixed right-hand side of the viscosity solve
        addField(mirror, "velocity",       true);
        addField(mirror, "pressure",       true);
        addField(mirror, "dye",            true);
        addField(mirror, "divergence",     false);
        addField(mirror, "velocitySource", false);

        // Advect velocity along itself, apply inflow and obstacle.
        // On a restart it writes the uniform inflow state instead
        mirror.create()
            .attach<vk::ComputeShader>({.path = shader("advect_velocity.comp.spv")})
            .attach<vk::Bind>(
                vk::sample(0, "velocity"), vk::write(1, "velocity"), vk::write(2, "velocitySource")
            )
            .attach<vk::Dispatch>("velocity")
            .attach<vk::PushConstants>(initial);

        // Viscosity: Jacobi sweeps of (I - nu*dt*laplacian) u = source
        mirror.create()
            .attach<vk::ComputeShader>({.path = shader("viscosity.comp.spv")})
            .attach<vk::Bind>(
                vk::read(0, "velocity"), vk::read(1, "velocitySource"), vk::write(2, "velocity")
            )
            .attach<vk::Dispatch>("velocity")
            .attach<vk::Repeat>({.count = VISCOSITY_ITERATIONS})
            .attach<vk::PushConstants>(initial);

        // Divergence of the diffused velocity
        mirror.create()
            .attach<vk::ComputeShader>({.path = shader("divergence.comp.spv")})
            .attach<vk::Bind>(vk::sample(0, "velocity"), vk::write(1, "divergence"))
            .attach<vk::Dispatch>("divergence")
            .attach<vk::PushConstants>(initial);

        // Pressure: Jacobi sweeps. The sides swap after each one
        mirror.create()
            .attach<vk::ComputeShader>({.path = shader("pressure.comp.spv")})
            .attach<vk::Bind>(
                vk::read(0, "pressure"), vk::read(1, "divergence"), vk::write(2, "pressure")
            )
            .attach<vk::Dispatch>("pressure")
            .attach<vk::Repeat>({.count = JACOBI_ITERATIONS})
            .attach<vk::PushConstants>(initial);

        // Project: subtract the pressure gradient from the velocity
        mirror.create()
            .attach<vk::ComputeShader>({.path = shader("project.comp.spv")})
            .attach<vk::Bind>(
                vk::sample(0, "velocity"), vk::read(1, "pressure"), vk::write(2, "velocity")
            )
            .attach<vk::Dispatch>("velocity")
            .attach<vk::PushConstants>(initial);

        // Advect dye with the final velocity
        mirror.create()
            .attach<vk::ComputeShader>({.path = shader("advect_dye.comp.spv")})
            .attach<vk::Bind>(
                vk::sample(0, "velocity"), vk::sample(1, "dye"), vk::write(2, "dye")
            )
            .attach<vk::Dispatch>("dye")
            .attach<vk::PushConstants>(initial);

        // Display into the draw image
        mirror.create()
            .attach<vk::VertexShader>({.path = shader("fullscreen.vert.spv")})
            .attach<vk::FragmentShader>({.path = shader("display.frag.spv")})
            .attach<vk::Bind>(
                vk::sample(0, "dye"), vk::sample(1, "velocity"),
                vk::sample(2, "pressure"), vk::sample(3, "divergence")
            )
            .attach<vk::PushConstants>(initial);
    }
};

int main() {
    App app;
    app.init()
        .mount<FluidContext>()
        .build()
        .run()
        .exit();
}
