#include <moss/extensions/vulkan.hpp>
#include <moss/extensions/imgui.hpp>
#include <moss/meta/logs.hpp>
#include <moss/moss.hpp>

#include "sim.hpp"

int main() {
    App app;
    app.init()
        .mount<FluidContext>()
        .build()
        .run()
        .exit();
}
