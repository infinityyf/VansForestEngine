#include "../Common/CameraData.glsl"
layout(location = 0) in vec2 frag_uv;
layout(set = 4, binding = 0) uniform sampler2D grassAlbedo;
void main()
{
    if (texture(grassAlbedo, frag_uv, MaterialMipBias).a < 0.5) discard;
}
