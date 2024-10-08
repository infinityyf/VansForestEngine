#version 450
 layout( location = 0 ) in vec3 frag_uv;
 layout( set = 1, binding = 0 ) uniform samplerCube Cubemap;
 layout( location = 0 ) out vec4 frag_color;
 void main() 
 {
    frag_color = texture( Cubemap, frag_uv );
 }