#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

// binding 0 = COMBINED_IMAGE_SAMPLER pointing at the sim field (e.g. "fieldA")
layout(set = 0, binding = 0) uniform sampler2D field;

void main() {
    outColor = vec4(texture(field, uv).rrr, 1.0);
}
