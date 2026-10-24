#include "moss/extensions/vulkan/attachments/passes/components.hpp"
#include <moss/extensions/vulkan.hpp>
#include <moss/meta/logs.hpp>
#include <moss/moss.hpp>

using namespace moss;

struct Params {
    f32 time;
    f32 width;
    f32 height;
    f32 frame;
};

class ParamSystem : public System {
public:
    void tick(const Key<key::READ>& key) override {
        vk::RenderCore& core = cmd::Resource<vk::RenderCore>::init(key).get();
        auto rows = cmd::Query<With<vk::PushConstants>>::init(key).atlas();

        for (auto [pc] : rows) {
            Params& params = pc.as<Params>();
            params.time = static_cast<f32>(glfwGetTime());
            params.width = static_cast<f32>(core.drawImage.imageExtent.width);
            params.height = static_cast<f32>(core.drawImage.imageExtent.height);
            // At tick time frameNumber is the index of the frame being recorded
            params.frame = static_cast<f32>(core.frame.frameNumber);
        }
    }
};

class FluidContext : public Context {
public:
    void init(Mirror& mirror) override {
        const Params initial{ 0.0f, 1280.0f, 720.0f, 0.0f };

        mirror
            .connect<vk::Core, vk::FrameBegin, vk::ImageSystem, ParamSystem, vk::PassSystem, vk::FrameEnd>()
            .create()
                .attach<vk::WindowSettings>({.title = "fluid", .resize = true})
                .attach<vk::RendererSettings>()
                .attach<vk::HotReload>(500u)
            .create()
                .attach<vk::SimImage>("imageA")
                .attach<vk::ImageExtent>(256u, 256u)
            .create()
                .attach<vk::SimImage>("imageB")
                .attach<vk::ImageExtent>(256u, 256u)
            .create()
                .attach<vk::ComputeShader>("build/top.comp.spv")
                .attach<vk::Bind>(vk::write(0, "imageA"))
                .attach<vk::Dispatch>("imageA")
                .attach<vk::PushConstants>(initial)
            .create()
                .attach<vk::ComputeShader>("build/bottom.comp.spv")
                .attach<vk::Bind>(vk::write(0, "imageB"))
                .attach<vk::Dispatch>("imageB")
                .attach<vk::PushConstants>(initial)

            // Display A top half
            .create()
                .attach<vk::VertexShader>("build/top.vert.spv")
                .attach<vk::FragmentShader>("build/display.frag.spv")
                .attach<vk::Bind>(vk::sample(0, "imageA"))
                .attach<vk::PushConstants>(initial)

            // Display image B in the bottom half
            .create()
                .attach<vk::VertexShader>("build/bottom.vert.spv")
                .attach<vk::FragmentShader>("build/display.frag.spv")
                .attach<vk::Bind>(vk::sample(0, "imageB"))
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
