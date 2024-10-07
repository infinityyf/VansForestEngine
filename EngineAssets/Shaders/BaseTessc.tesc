#version 450
//指定输出的patch顶点数
//对每个输出的顶点都会执行control shader
layout( vertices = 3 ) out;
//逐顶点进行计算
void main() 
{
    //current vertex is available in the built-in gl_InvocationID variable
    if( 0 == gl_InvocationID ) 
    {
        //不同图元需要设置的数组数量不同
        //四边形就需要两个in 四个out
        //specifies how the internal part of the patch is tessellated
        gl_TessLevelInner[0] = 3.0;
        //how the outer edges of the patches are tessellated
        gl_TessLevelOuter[0] = 3.0;
        gl_TessLevelOuter[1] = 4.0;
        gl_TessLevelOuter[2] = 5.0;
    }
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
}

//Determine the amount of tessellation that a primitive should have.
//Perform any special transformations on the input patch data.