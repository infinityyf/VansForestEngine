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

#define TILE_SIZE      16
#define VOXEL_GRID_Z  256

// Minimum near distance for the fog exponential depth distribution.
// Camera near plane is often tiny (0.01m) which wastes almost all 128 slices
// on the first few centimetres, leaving no resolution for actual fog distances.
// Clamping to 0.5m ensures slices have meaningful thickness.
#define FOG_DEPTH_MIN_NEAR 0.5

// Maximum far distance for fog — beyond this the grid has negligible density anyway.
#define FOG_DEPTH_MAX_FAR 1000.0

// Use these instead of raw NearPlane / FarPlane for all fog depth calculations.
// Requires CameraData.glsl.
float FogNear() { return max(NearPlane, FOG_DEPTH_MIN_NEAR); }
float FogFar()  { return min(FarPlane, FOG_DEPTH_MAX_FAR); }

// ---------------------------------------------------------------------------
// Exponential depth distribution  (Frostbite / DICE style)
//   depth(z) = near × pow(far/near, (z + 0.5) / gridZ)
// ---------------------------------------------------------------------------
float SliceToDepth(float z, float near, float far, float gridZ)
{
    return near * pow(far / near, (z + 0.5) / gridZ);
}

float DepthToSlice(float depth, float near, float far, float gridZ)
{
    return gridZ * log(depth / near) / log(far / near);
}

float SliceThickness(int z, float near, float far, float gridZ)
{
    float d0 = near * pow(far / near, float(z)     / gridZ);
    float d1 = near * pow(far / near, float(z + 1) / gridZ);
    return d1 - d0;
}

// ---------------------------------------------------------------------------
// Voxel → world position   (requires CameraData.glsl in the including file)
//
//   id       : integer voxel coordinate
//   gridSize : ivec3(tilesX, tilesY, VOXEL_GRID_Z)
//   near/far : fog volume depth range
// ---------------------------------------------------------------------------
vec3 VoxelToWorld(ivec3 id, ivec3 gridSize, float near, float far)
{
    // Tile centre → screen UV
    vec2 uv  = (vec2(id.xy) + 0.5) / vec2(gridSize.xy);

    // Screen UV → NDC  (Vulkan Y-flip)
    vec2 ndc = uv * 2.0 - 1.0;
    ndc.y   *= -1.0;

    // View-space ray direction (NOT normalized — preserves Z component for planar depth)
    vec3 viewRay = normalize((InverseProjectionMatrix * vec4(ndc, 1.0, 1.0)).xyz);

    // Exponential depth for this Z slice
    float depth = SliceToDepth(float(id.z), near, far, float(gridSize.z));

    // Scale ray so view-space Z equals depth (planar, not spherical)
    vec3 viewPos = viewRay * (depth / abs(viewRay.z));
    return (InverseViewMatrix * vec4(viewPos, 1.0)).xyz;
}

// ---------------------------------------------------------------------------
// Screen pixel + distance → UVW for sampling the frustum-aligned 3D texture.
//
//   pixelCoord : full-resolution pixel position (float)
//   dist       : radial distance from camera  (length(posWS - camPos))
//   screenSize : full-res screen dimensions   (ScreenParams.xy)
//   near/far   : fog volume depth range
// ---------------------------------------------------------------------------
vec3 ScreenToFrustumUVW(vec2 pixelCoord, float dist,
                        vec2 screenSize, float near, float far)
{
    vec2 uv = (pixelCoord + 0.5) / screenSize;
    float z = DepthToSlice(max(dist, near), near, far, float(VOXEL_GRID_Z));
    float w = clamp(z / float(VOXEL_GRID_Z), 0.0, 1.0);
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
vec4 WorldToLastFrameUVW(vec3 worldPos, float near, float far, vec2 screenSize)
{
    // Project into previous frame clip space
    vec4 lastClip = LastVPMatrix * vec4(worldPos, 1.0);
    vec3 lastNDC  = lastClip.xyz / lastClip.w;

    // NDC → screen UV  (undo Vulkan Y-flip)
    vec2 uv = lastNDC.xy * 0.5 + 0.5;
    uv.y    = 1.0 - uv.y;

    // Linearise clip depth → view-space distance for slice lookup
    // lastNDC.z is in [0,1] for Vulkan; reconstruct view-space depth
    // via inverse projection.  Z = near * far / (far - ndc.z * (far - near))
    float linearDepth = near * far / (far - lastNDC.z * (far - near));
    float z = DepthToSlice(max(linearDepth, near), near, far, float(VOXEL_GRID_Z));
    float w = clamp(z / float(VOXEL_GRID_Z), 0.0, 1.0);

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
