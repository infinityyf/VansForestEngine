#ifndef BRDF_REFRACTION_INCLUDED
#define BRDF_REFRACTION_INCLUDED

#include "../Common/Common.glsl"
#include "BRDFData.glsl"

// =============================================================================
// Refraction BRDF (Material ID = MATERIAL_ID_REFRACTION)
// TODO: Implement refraction-specific shading
//   - Screen-space refraction (distorted UV from back-face normal + IOR)
//   - Fresnel-weighted blend between reflection and refraction
//   - Absorption / tint based on travel distance (Beer's law)
//   - Chromatic dispersion (optional)
// =============================================================================

void DirectBRDF_Refraction(BRDFData brdf, vec3 lightDirection, inout vec3 diffuse, inout vec3 specular)
{
    // Placeholder: fallback to standard PBR
    DirectBRDF(brdf, lightDirection, diffuse, specular);
}

void AmbientBRDF_Refraction(BRDFData brdf, vec3 viewDirection, inout vec3 diffuse, inout vec3 specular)
{
    // Placeholder: fallback to standard PBR
    AmbientBRDF(brdf, viewDirection, diffuse, specular);
}

#endif // BRDF_REFRACTION_INCLUDED
