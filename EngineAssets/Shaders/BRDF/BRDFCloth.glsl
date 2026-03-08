#ifndef BRDF_CLOTH_INCLUDED
#define BRDF_CLOTH_INCLUDED

#include "../Common/Common.glsl"
#include "BRDFData.glsl"

// =============================================================================
// Cloth BRDF (Material ID = MATERIAL_ID_CLOTH)
// TODO: Implement cloth-specific shading
//   - Charlie / Ashikhmin sheen distribution (replaces GGX specular)
//   - Lambertian diffuse wrap with subsurface approximation
//   - Sheen color tint for fabric micro-fibers
//   - No metallic component (cloth is always dielectric)
// =============================================================================

void DirectBRDF_Cloth(BRDFData brdf, vec3 lightDirection, inout vec3 diffuse, inout vec3 specular)
{
    // Placeholder: fallback to standard PBR
    DirectBRDF(brdf, lightDirection, diffuse, specular);
}

void AmbientBRDF_Cloth(BRDFData brdf, vec3 viewDirection, inout vec3 diffuse, inout vec3 specular)
{
    // Placeholder: fallback to standard PBR
    AmbientBRDF(brdf, viewDirection, diffuse, specular);
}

#endif // BRDF_CLOTH_INCLUDED
