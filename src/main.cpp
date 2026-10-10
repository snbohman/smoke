#include <moss/extensions/vulkan.hpp>
#include <moss/moss.hpp>

class RenderContext : public moss::Context {
public:
    void init(moss::Mirror& mirror) override {
        using namespace moss;

        mirror
            .create()  // window + renderer config
                .attach<vk::WindowSettings>("moss", 1280, 720, 60, false)
                .attach<vk::RendererSettings>(true)   // validation layers
            .create().attach<vk::SimImage>("fieldA")
            .create().attach<vk::SimImage>("fieldB")
            .create()  // compute: 20 ping-pong iterations
                .attach<vk::Pass>()
                .attach<vk::Shader<vk::PipelineKind::Compute>>()
                .attach<vk::PassOrder>(0)
                .attach<vk::ComputeShader>("build/diffuse.comp.spv")
                .attach<vk::GridDispatch>(80, 45, 1)   // workgroup counts: ceil(w/16), ceil(h/16)
                .attach<vk::Bindings>(std::vector<vk::Binding>{
                    {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, "fieldA"},
                    {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, "fieldB"} })
                .attach<vk::PassRepeat>(20u)
                .attach<vk::PingPong>("fieldA", "fieldB")
            .create()  // display
                .attach<vk::Pass>()
                .attach<vk::Shader<vk::PipelineKind::Graphics>>()
                .attach<vk::PassOrder>(100)
                .attach<vk::VertexShader>("build/fullscreen.vert.spv")
                .attach<vk::FragmentShader>("build/display.frag.spv")
                .attach<vk::PipelineState>()
                .attach<vk::DrawCall>()
                .attach<vk::Bindings>(std::vector<vk::Binding>{
                    {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, "fieldA"} })
            .connect<vk::Renderer>();
    };
};


int main() {
    moss::App app;
    app.init()
        .mount<RenderContext>()
        .build()
        .run()
        .exit();
}
