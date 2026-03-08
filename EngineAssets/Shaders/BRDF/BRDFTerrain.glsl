#ifndef BRDF_TERRAIN_INCLUDED
#define BRDF_TERRAIN_INCLUDED

#include "../Common/Common.glsl"
#include "BRDFData.glsl"

// =============================================================================
// Terrain BRDF (Material ID = MATERIAL_ID_TERRAIN)
// TODO: Implement terrain-specific shading
//   - Multi-layer blending (grass, rock, sand, snow)
//   - Heightmap-based blending weights
//   - Triplanar projection for steep surfaces
// =============================================================================

void DirectBRDF_Terrain(BRDFData brdf, vec3 lightDirection, inout vec3 diffuse, inout vec3 specular)
{
    // Placeholder: fallback to standard PBR
    DirectBRDF(brdf, lightDirection, diffuse, specular);
}

void AmbientBRDF_Terrain(BRDFData brdf, vec3 viewDirection, inout vec3 diffuse, inout vec3 specular)
{
    // Placeholder: fallback to standard PBR
    AmbientBRDF(brdf, viewDirection, diffuse, specular);
}

#endif // BRDF_TERRAIN_INCLUDED
