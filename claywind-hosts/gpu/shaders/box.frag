// SPDX-License-Identifier: MIT
// metal.c's fbox: ThorVG's box pass (raster_effects.cee's boxPass), the sum of 2d + 1 pixels along an axis, edges
// extended, truncated.

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D source;

uvec4 toU(vec4 c) {
    return uvec4(clamp(c, 0.0, 1.0) * 255.0 + 0.5);
}

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    int d = pc.box.y;
    int end = (pc.box.x == 0 ? pc.box.z : pc.box.w) - 1;
    uvec4 acc = uvec4(0u);
    for (int j = -d; j <= d; j++) {
        ivec2 q = p;
        if (pc.box.x == 0) {
            q.x = clamp(p.x + j, 0, end);
        } else {
            q.y = clamp(p.y + j, 0, end);
        }
        acc += toU(texelFetch(source, q, 0));
    }
    precise float iarr = 1.0 / float(d + d + 1);
    precise vec4 v = vec4(acc) * iarr;
    outColor = vec4(uvec4(v)) / 255.0;
}
