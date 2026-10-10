#version 450

layout(location = 0) out vec2 uv;

// Fullscreen triangle from gl_VertexIndex alone: DrawCall(3, 1), no vertex buffers.
void main() {
    uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
