// The pressure image and its neighbour rule, shared by pressure.comp and project.comp.
// Include common.glsl first and define PRESSURE_BINDING and OBSTACLE_BINDING before it.

layout(rgba16f, binding = PRESSURE_BINDING) uniform readonly image2D uPressure;

// Pressure of a neighbour cell
float pressureAt(ivec2 cell, float centre, ivec2 size) {
    if (cell.x >= size.x) return 0.0;                     // outflow edge: pressure fixed to 0
    cell = clamp(cell, ivec2(0), size - 1);               // other walls: copy the edge cell
    if (isSolid(cell)) return centre;                     // obstacle: zero gradient
    return imageLoad(uPressure, cell).x;
}
