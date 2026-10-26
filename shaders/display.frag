#version 450
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;
layout(push_constant) uniform Push {
    float dt;
    float inflow;
    float obstacleX;
    float obstacleY;
    float obstacleRadius;
    float gain;
    int   view;
    float windowWidth;
    float windowHeight;
    float viscosity;
    float dyeStripe;
    int   reset;
    int   palette;
} pc;

bool isSolid(vec2 cellCentre) {
    return distance(cellCentre, vec2(pc.obstacleX, pc.obstacleY)) < pc.obstacleRadius;
}
layout(binding = 0) uniform sampler2D uDye;
layout(binding = 1) uniform sampler2D uVelocity;
layout(binding = 2) uniform sampler2D uPressure;
layout(binding = 3) uniform sampler2D uDivergence;

// Red for positive, blue for negative
vec3 signedColor(float value) {
    float x = clamp(value * pc.gain, -1.0, 1.0);
    return vec3(max(x, 0.0), 0.0, max(-x, 0.0));
}

// Dye 0..1 to a colour, picked by the palette combo (same order as Palette in fluid.cpp)
vec3 dyeColor(float dye) {
    float d = clamp(dye, 0.0, 1.0);
    if (pc.palette == 0)                                          // mono: black to white
        return vec3(d);
    if (pc.palette == 1)                                          // ember: black, red, yellow, white
        return vec3(smoothstep(0.0, 0.4, d), smoothstep(0.25, 0.8, d), smoothstep(0.7, 1.0, d));
    if (pc.palette == 2)                                          // ice: black, blue, cyan, white
        return vec3(smoothstep(0.6, 1.0, d), smoothstep(0.2, 0.8, d), smoothstep(0.0, 0.5, d));
    return mix(vec3(0.96), vec3(0.08, 0.10, 0.18), d);            // ink: dark on paper
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

    vec2 cell = g * grid;
    if (distance(cell, vec2(pc.obstacleX, pc.obstacleY)) < pc.obstacleRadius) {
        outColor = vec4(vec3(0.35), 1.0);
        return;
    }

    vec3 color;
    if (pc.view == 0) {
        color = dyeColor(texture(uDye, g).x);
    } else if (pc.view == 1) {
        float speed = length(texture(uVelocity, g).xy) / (2.0 * max(pc.inflow, 1.0));
        color = vec3(speed, speed * speed, speed * speed * speed * 0.5);
    } else if (pc.view == 2) {
        color = signedColor(texture(uPressure, g).x);
    } else {
        color = signedColor(texture(uDivergence, g).x);
    }
    outColor = vec4(color, 1.0);
}
