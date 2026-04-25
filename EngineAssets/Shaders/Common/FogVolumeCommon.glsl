#ifndef FOG_VOLUME_COMMON_GLSL
#define FOG_VOLUME_COMMON_GLSL

// ============================================================================
// Frustum-aligned voxel grid fog — shared constants & helpers
//
// Grid layout:
//   XY : one voxel per TILE_SIZE × TILE_SIZE pixel tile on screen
//        → gridX = ceil(screenW / TILE_SIZE),  gridY = ceil(screenH / TILE_SIZE)
//   Z  : VOXEL_GRID_Z slices with exponential depth distribution
//
// Updated every frame from the camera's perspective:
//   VoxelToWorld() reconstructs world positions via InverseProjection / View.
//   Static scene fog lives in a world-space AABB ("fog box");
//   the injection pass tests each voxel against it to query density.
// ============================================================================

#define TILE_SIZE      8
#define VOXEL_GRID_Z  256

// Near/far for the fog depth distribution are taken from the FogVolumeParams UBO
// (uVolume.volumeNear / uVolume.volumeFar) so that they are artist-controllable
// at runtime.  Each shader that includes this header must pass them explicitly
// to SliceToDepth / DepthToSlice / SliceThickness / ScreenToFrustumUVW etc.

// ---------------------------------------------------------------------------
// Power-curve depth distribution
//   depth(z) = mix(near, far, pow((z + 0.5) / gridZ, powerCoeff))
//
//   powerCoeff > 1  concentrates slices near the camera (default ~2.0)
//   powerCoeff = 1  gives a uniform (linear) distribution
// ---------------------------------------------------------------------------
float SliceToDepth(float z, float near, float far, float gridZ, float powerCoeff)
{
    float x = (z + 0.5) / gridZ;
    float t = pow(x, powerCoeff);
    return mix(near, far, t);
}

int DepthToSlice(float depth, float near, float far, float gridZ, float powerCoeff)
{
    float t = clamp((depth - near) / (far - near), 0.0, 1.0);
    float x = pow(t, 1.0 / powerCoeff);
    return clamp(int(floor(x * gridZ)), 0, int(gridZ) - 1);
}

float SliceThickness(int z, float near, float far, float gridZ, float powerCoeff)
{
    float x0 = float(z) / gridZ;
    float x1 = float(z + 1) / gridZ;

    float d0 = mix(near, far, pow(x0, powerCoeff));
    float d1 = mix(near, far, pow(x1, powerCoeff));
    return d1 - d0;
}

// ---------------------------------------------------------------------------
// Voxel → world position   (requires CameraData.glsl in the including file)
//
//   id       : integer voxel coordinate
//   gridSize : ivec3(tilesX, tilesY, VOXEL_GRID_Z)
//   near/far : fog volume depth range
// ---------------------------------------------------------------------------
vec3 VoxelToWorld(ivec3 id, ivec3 gridSize, float near, float far, float powerCoeff)
{
    // Tile centre → screen UV
    vec2 uv  = (vec2(id.xy) + 0.5) / vec2(gridSize.xy);

    // Screen UV → NDC  (Vulkan Y-flip)
    vec2 ndc = uv * 2.0 - 1.0;
    ndc.y   *= -1.0;

    // View-space ray direction (NOT normalized — preserves Z component for planar depth)
    vec3 viewRay = normalize((InverseProjectionMatrix * vec4(ndc, 1.0, 1.0)).xyz);

    // Power-curve depth for this Z slice
    float depth = SliceToDepth(float(id.z), near, far, float(gridSize.z), powerCoeff);

    // Scale ray so view-space Z equals depth (planar, not spherical)
    vec3 viewPos = viewRay * (depth / abs(viewRay.z));
    return (InverseViewMatrix * vec4(viewPos, 1.0)).xyz;
}

// ---------------------------------------------------------------------------
// Screen pixel + distance → UVW for sampling the frustum-aligned 3D texture.
//
//   pixelCoord : full-resolution pixel position (float)
//   dist       : view-space Z (planar depth, NOT radial distance)
//                must match the planar depth convention used by
//                SliceToDepth / VoxelToWorld in the injection pass.
//   screenSize : texel-aligned screen extent = vec2(gridXY) * TILE_SIZE.
//                NOT raw ScreenParams — the voxel grid may cover more
//                virtual pixels than the screen when the resolution is
//                not evenly divisible by TILE_SIZE.
//   near/far   : fog volume depth range (FogNear / FogFar)
// ---------------------------------------------------------------------------
vec3 ScreenToFrustumUVW(vec2 pixelCoord, float dist,
                        vec2 screenSize, float near, float far,
                        float powerCoeff)
{
    vec2 uv = (pixelCoord + 0.5) / screenSize;

    // Use continuous W (fractional position in [0,1]) instead of the integer
    // DepthToSlice result.  Discretising to slice indices prevents hardware
    // trilinear filtering from working across the Z dimension and causes
    // visible banding / sudden density jumps at every slice boundary.
    float rangeRcp = 1.0 / max(far - near, 1e-6);
    float t = clamp((clamp(dist, near, far) - near) * rangeRcp, 0.0, 1.0);
    float w = clamp(pow(t, 1.0 / max(powerCoeff, 1e-4)), 0.0, 1.0);

    return vec3(uv, w);
}

// ---------------------------------------------------------------------------
// Static fog box query
// ---------------------------------------------------------------------------
bool IsInsideFogBox(vec3 worldPos, vec3 boxMin, vec3 boxMax)
{
    return all(greaterThanEqual(worldPos, boxMin)) &&
           all(lessThanEqual(worldPos, boxMax));
}

// ---------------------------------------------------------------------------
// Temporal reprojection: world position → previous frame frustum UVW
//   Returns vec4(uvw, valid)  where valid > 0.5 means the sample is usable.
//   Requires CameraData.glsl for LastVPMatrix.
// ---------------------------------------------------------------------------
vec4 WorldToLastFrameUVW(vec3 worldPos, float near, float far, vec2 screenSize,
                         float powerCoeff)
{
    // Project into previous frame clip space
    vec4 lastClip = LastVPMatrix * vec4(worldPos, 1.0);
    vec3 lastNDC  = lastClip.xyz / lastClip.w;

    // NDC → screen UV  (undo Vulkan Y-flip)
    vec2 uv = lastNDC.xy * 0.5 + 0.5;
    uv.y    = 1.0 - uv.y;

    // Linearise clip depth → view-space distance for slice lookup.
    // Vulkan NDC z ∈ [0,1]: viewZ = near * far / (far - ndcZ * (far - near))
    // Guard against near == far and degenerate NDC values to prevent NaN/Inf.
    float ndcZ        = clamp(lastNDC.z, 0.0, 1.0);
    float denom       = max(far - ndcZ * (far - near), 1e-6);
    float linearDepth = (near * far) / denom;

    // Continuous W — same power-curve inverse as ScreenToFrustumUVW so that
    // both lookups use identical non-discretised coordinates and trilinear
    // interpolation is applied consistently.
    float rangeRcp = 1.0 / max(far - near, 1e-6);
    float t = clamp((clamp(linearDepth, near, far) - near) * rangeRcp, 0.0, 1.0);
    float w = clamp(pow(t, 1.0 / max(powerCoeff, 1e-4)), 0.0, 1.0);

    // Validity: UV in [0,1] and depth in fog range
    float valid = (all(greaterThanEqual(uv, vec2(0.0))) &&
                   all(lessThanEqual(uv, vec2(1.0)))    &&
                   linearDepth >= near && linearDepth <= far) ? 1.0 : 0.0;

    return vec4(uv, w, valid);
}

// ---------------------------------------------------------------------------
// Halton sequence (base 2 & 3) — low-discrepancy jitter for temporal AA
// ---------------------------------------------------------------------------
float Halton(int index, int base)
{
    float f = 1.0;
    float r = 0.0;
    int   i = index;
    while (i > 0)
    {
        f = f / float(base);
        r = r + f * float(i % base);
        i = i / base;
    }
    return r;
}

#endif // FOG_VOLUME_COMMON_GLSL
