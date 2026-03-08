#ifndef BRDF_SKIN_INCLUDED
#define BRDF_SKIN_INCLUDED

#include "../Common/Common.glsl"
#include "BRDFData.glsl"

// =============================================================================
// Skin / Subsurface Scattering BRDF (Material ID = MATERIAL_ID_SKIN)
// TODO: Implement skin-specific shading
//   - Pre-integrated subsurface scattering (diffusion profile LUT)
//   - Dual-lobe specular (primary sharp + secondary wide)
//   - Curvature-driven SSS approximation
//   - Transmission / back-lighting for thin areas (ears, nostrils)
// =============================================================================

void DirectBRDF_Skin(BRDFData brdf, vec3 lightDirection, inout vec3 diffuse, inout vec3 specular)
{
    // Placeholder: fallback to standard PBR
    DirectBRDF(brdf, lightDirection, diffuse, specular);
}

void AmbientBRDF_Skin(BRDFData brdf, vec3 viewDirection, inout vec3 diffuse, inout vec3 specular)
{
    // Placeholder: fallback to standard PBR
    AmbientBRDF(brdf, viewDirection, diffuse, specular);
}

#endif // BRDF_SKIN_INCLUDED
