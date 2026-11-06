// Shared by every fluid shader. Include it after #version and the layout line.
//
// Define before including:
//   OBSTACLE_BINDING  binding of the sampled "obstacle" image. Enables isSolid()
//                     and applyVelocityBC(); leave it out in the pass that paints it
//
// Same member order as FluidBlock in src/params.hpp (60 bytes). Members are only appended.

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
    float time;           // simulated seconds since the last restart
    float perturbation;   // wake seed strength in U^2 / D
} pc;

// Same order as ViewMode and Palette in src/params.hpp
const int VIEW_DYE = 0;
const int VIEW_SPEED = 1;
const int VIEW_PRESSURE = 2;
const int VIEW_DIVERGENCE = 3;
const int VIEW_VORTICITY = 4;
const int VIEW_RESIDUAL = 5;

const int PALETTE_MONO = 0;
const int PALETTE_EMBER = 1;
const int PALETTE_ICE = 2;

// How long the wake seed acts after a restart, in D/U. Same value as SEED_DURATION in params.hpp
const float SEED_DURATION = 2.0;

#ifdef OBSTACLE_BINDING
// Signed distance from the cell centre to the obstacle surface, negative inside
layout(binding = OBSTACLE_BINDING) uniform sampler2D uObstacle;

bool isSolid(ivec2 cell) {
    return texelFetch(uObstacle, cell, 0).x < 0.0;
}

// Domain rules for a velocity: obstacle, slip walls, inflow edge
vec2 applyVelocityBC(vec2 velocity, ivec2 id, ivec2 size) {
    if (isSolid(id)) velocity = vec2(0.0);                       // obstacle: no slip
    if (id.y == 0 || id.y == size.y - 1) velocity.y = 0.0;       // top/bottom walls
    if (id.x == 0) velocity = vec2(pc.inflow, 0.0);              // inflow edge
    return velocity;
}
#endif
