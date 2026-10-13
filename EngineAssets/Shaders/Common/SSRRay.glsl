#ifndef VANS_SSR_RAY_GLSL
#define VANS_SSR_RAY_GLSL

const uint SSR_TILE_RECORD_WORDS = 3u;

// Shared by classification and trace so the compact ray mask always matches
// the direction and PDF used by the actual Hi-Z traversal.
void SSRPrepareRay(ivec2 pixelCoord, vec3 normal, vec3 position, float roughness,
                   out vec3 rayDirection, out float rayPdf)
{
    vec3 viewDirection = normalize(cameraPosition.xyz - position);
    if (roughness > 0.1)
    {
        uint seed = PixelSeedHash(pixelCoord);
        vec2 random = R2Sequence(int((seed + uint(FrameIndex)) % 256u));
        vec4 pdfRandom = ImportanceSampleGGX(random, roughness);
        rayPdf = pdfRandom.w;
        vec3 jitterNormal = normalize(TangentToWorld(pdfRandom.xyz, normal));
        rayDirection = reflect(-viewDirection, jitterNormal);
    }
    else
    {
        rayPdf = 1.0;
        rayDirection = reflect(-viewDirection, normal);
    }
}

#endif
