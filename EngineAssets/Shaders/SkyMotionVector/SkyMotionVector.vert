#version 450
#extension GL_GOOGLE_include_directive : require
#include "../Common/CameraData.glsl"

layout(location = 0) out vec4 vCurrentClipPos;
layout(location = 1) out vec4 vPreviousClipPos;

void main()
{
	vec2 positions[3] = vec2[](
		vec2(-1.0, -1.0),
		vec2( 3.0, -1.0),
		vec2(-1.0,  3.0));
	vec2 ndc = positions[gl_VertexIndex];
	vec4 view = InverseProjectionMatrix * vec4(ndc, 1.0, 1.0);
	vec3 direction = normalize((InverseViewMatrix *
		vec4(normalize(view.xyz / max(abs(view.w), 1.0e-6)), 0.0)).xyz);
	// 天空是无穷远方向，w=0 可从 View/VP 变换中消除相机平移。
	vec4 directionWS = vec4(direction, 0.0);

	gl_Position = vec4(ndc, 1.0, 1.0);
	vCurrentClipPos = UnjitteredVPMatrix * directionWS;
	vPreviousClipPos = LastUnjitteredVPMatrix * directionWS;
}
