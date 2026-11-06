#version 450
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;
#define OBSTACLE_BINDING 5
#include "common.glsl"

layout(binding = 0) uniform sampler2D uDye;
layout(binding = 1) uniform sampler2D uVelocity;
layout(binding = 2) uniform sampler2D uPressure;
layout(binding = 3) uniform sampler2D uDivergence;
layout(binding = 4) uniform sampler2D uResidual;

// Red for positive, blue for negative
vec3 signedColor(float value) {
    float x = clamp(value * pc.gain, -1.0, 1.0);
    return vec3(max(x, 0.0), 0.0, max(-x, 0.0));
}

// Dye 0..1 to a colour, picked by the palette combo
vec3 dyeColor(float dye) {
    float d = clamp(dye, 0.0, 1.0);
    if (pc.palette == PALETTE_MONO)                               // black to white
        return vec3(d);
    if (pc.palette == PALETTE_EMBER)                              // black, red, yellow, white
        return vec3(smoothstep(0.0, 0.4, d), smoothstep(0.25, 0.8, d), smoothstep(0.7, 1.0, d));
    if (pc.palette == PALETTE_ICE)                                // black, blue, cyan, white
        return vec3(smoothstep(0.6, 1.0, d), smoothstep(0.2, 0.8, d), smoothstep(0.0, 0.5, d));
    return mix(vec3(0.96), vec3(0.08, 0.10, 0.18), d);            // ink: dark on paper
}

// Out-of-plane vorticity dv/dx - du/dy by central differences, border cells copy the edge
float vorticity(ivec2 cell) {
    ivec2 size = textureSize(uVelocity, 0);
    ivec2 hi = size - 1;
    float v1 = texelFetch(uVelocity, clamp(cell + ivec2(1, 0), ivec2(0), hi), 0).y;
    float v0 = texelFetch(uVelocity, clamp(cell - ivec2(1, 0), ivec2(0), hi), 0).y;
    float u1 = texelFetch(uVelocity, clamp(cell + ivec2(0, 1), ivec2(0), hi), 0).x;
    float u0 = texelFetch(uVelocity, clamp(cell - ivec2(0, 1), ivec2(0), hi), 0).x;
    return 0.5 * ((v1 - v0) - (u1 - u0));
}

void main() {
    vec2 grid = vec2(textureSize(uDye, 0));
    vec2 window = vec2(pc.windowWidth, pc.windowHeight);

    // Fit the grid into the window, keep its aspect ratio (bars around it)
    float scale = min(window.x / grid.x, window.y / grid.y);
    vec2 gridPixels = grid * scale;
    vec2 offset = 0.5 * (window - gridPixels);
    vec2 g = (uv * window - offset) / gridPixels;   // 0..1 inside the picture

    if (any(lessThan(g, vec2(0.0))) || any(greaterThan(g, vec2(1.0)))) {
        outColor = vec4(vec3(0.08), 1.0);
        return;
    }

    // The solid as the simulation sees it: whole cells
    ivec2 cell = min(ivec2(g * grid), ivec2(grid) - 1);
    if (isSolid(cell)) {
        outColor = vec4(vec3(0.35), 1.0);
        return;
    }

    vec3 color;
    if (pc.view == VIEW_DYE) {
        color = dyeColor(texture(uDye, g).x);
    } else if (pc.view == VIEW_SPEED) {
        float speed = length(texture(uVelocity, g).xy) / (2.0 * max(pc.inflow, 1.0));
        color = vec3(speed, speed * speed, speed * speed * speed * 0.5);
    } else if (pc.view == VIEW_PRESSURE) {
        color = signedColor(texture(uPressure, g).x);
    } else if (pc.view == VIEW_DIVERGENCE) {
        color = signedColor(texture(uDivergence, g).x);
    } else if (pc.view == VIEW_VORTICITY) {
        // Scaled by D/U, so the gain does not depend on the inflow or the obstacle size
        color = signedColor(vorticity(cell) * 2.0 * pc.obstacleRadius / max(pc.inflow, 1.0));
    } else {
        color = signedColor(texture(uResidual, g).x);
    }
    outColor = vec4(color, 1.0);
}
