// ============================================================
//  AdrenaProxy — Vulkan compute reprojection (VK_EXT_shader_image_atomic_int64)
//
//  Produces one intermediate frame by warping the current frame's
//  colour along its motion vector by a fractional time step t.
//  The depth test is implemented as a 64-bit atomicMin on a depth
//  image so that multiple threads writing to the same destination
//  pixel deterministically keep the closest sample (standard
//  forward-reprojection trick; requires VK_EXT_shader_image_atomic_int64
//  for the atomicMin on an r64 image to be correct).
//
//  Currently compiled only when glslc is available on the build host
//  (see plugins/vulkan_mfg/CMakeLists.txt).  The plugin DLL still
//  loads and publishes capability info even when this shader is absent.
// ============================================================
#version 460
#extension GL_EXT_shader_image_int64     : require
#extension GL_EXT_shader_atomic_int64    : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(binding = 0, rgba8) uniform readonly  image2D  uColor;
layout(binding = 1, rg16f) uniform readonly  image2D  uMotion;
layout(binding = 2, rgba8) uniform writeonly image2D  uOutput;
layout(binding = 3, r64ui) uniform coherent  uimage2D uDepthAtomic;

layout(push_constant) uniform PushConstants {
    vec2  uInvDim;   // 1 / (outputW, outputH)
    float uT;        // reprojection factor (0..1)
    uint  uDim;      // (width << 16) | height
} pc;

void main() {
    uint  w  = pc.uDim >> 16u;
    uint  h  = pc.uDim & 0xFFFFu;
    uvec2 id = gl_GlobalInvocationID.xy;
    if (id.x >= w || id.y >= h) return;

    vec2  srcUV = (vec2(id) + vec2(0.5)) * pc.uInvDim;
    vec2  mv    = imageLoad(uMotion, ivec2(id)).rg;
    vec2  dstUV = clamp(srcUV + mv * pc.uT, vec2(0.0), vec2(1.0));
    ivec2 dst   = ivec2(dstUV * vec2(w, h));

    vec4 color = imageLoad(uColor, ivec2(id));

    // Pack (depth<<32)|colorRGB into a 64-bit occlusion value so the
    // atomicMin below deterministically keeps the *closest* sample.
    // We approximate depth by the squared motion magnitude — works
    // surprisingly well for short reprojection steps and avoids
    // needing a separate depth bind.
    uint64_t occ = uint64_t(floatBitsToUint(dot(mv, mv))) << 32
                 | uint64_t(packUnorm4x8(color));

    imageAtomicMin(uDepthAtomic, dst, occ);

    // Winner-writeback: only the thread whose occ value ends up being
    // the minimum gets to commit the colour.
    uint64_t current = imageLoad(uDepthAtomic, dst).x;
    if (current == occ) {
        imageStore(uOutput, dst, color);
    }
}
