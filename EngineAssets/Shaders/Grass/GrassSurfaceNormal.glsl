#ifndef GRASS_SURFACE_NORMAL_GLSL
#define GRASS_SURFACE_NORMAL_GLSL
vec3 GrassUnitVector(vec3 value,vec3 fallback)
{
    float lengthSquared=dot(value,value);
    return lengthSquared>1e-12?value*inversesqrt(lengthSquared):fallback;
}
vec3 GrassTransformNormal(mat3 deformation,vec3 normal)
{
    // 余子式给出逆转置的方向，避免缩放/混合骨骼的剪切把法线当位置旋转。
    mat3 cofactor=mat3(cross(deformation[1],deformation[2]),
        cross(deformation[2],deformation[0]),cross(deformation[0],deformation[1]));
    float orientation=determinant(deformation)<0.0?-1.0:1.0;
    return GrassUnitVector(cofactor*normal*orientation,GrassUnitVector(normal,vec3(0,1,0)));
}
mat3 GrassTangentFrame(vec3 normal,vec3 dpdx,vec3 dpdy,vec2 duvdx,vec2 duvdy)
{
    vec3 N=GrassUnitVector(normal,vec3(0,1,0));
    float determinant=duvdx.x*duvdy.y-duvdx.y*duvdy.x;
    vec3 T=dpdx*duvdy.y-dpdy*duvdx.y;
    vec3 authoredB=dpdy*duvdx.x-dpdx*duvdy.x;
    T-=N*dot(N,T);
    if(abs(determinant)<1e-10 || dot(T,T)<1e-12)
    {
        T=normalize(cross(abs(N.y)<0.99?vec3(0,1,0):vec3(1,0,0),N));
        return mat3(T,cross(N,T),N);
    }
    T=normalize(T)*sign(determinant);
    authoredB*=sign(determinant);
    // UV 行列式还包含屏幕坐标的手性，不能直接拿它当切线空间手性。
    float handedness=dot(cross(N,T),authoredB)<0.0?-1.0:1.0;
    return mat3(T,cross(N,T)*handedness,N);
}
vec3 GrassMappedNormal(mat3 frame,vec3 encoded,float strength,bool backFace)
{
    // 当前纹理输入为 linear RGB XYZ 法线，正反面共用同一张薄片法线贴图。
    vec3 detail=encoded*2.0-1.0;
    detail.xy*=strength;
    vec3 normal=GrassUnitVector(frame*GrassUnitVector(detail,vec3(0,0,1)),frame[2]);
    return backFace?-normal:normal;
}
bool GrassIsBackFace(vec3 authoredNormal,vec3 dpdx,vec3 dpdy,vec3 viewDirection)
{
    // 用变形后的实际叶面确定可见侧，不假设导入模型的法线与管线绕序一致。
    vec3 geometricNormal=GrassUnitVector(cross(dpdx,dpdy),authoredNormal);
    if(dot(geometricNormal,viewDirection)<0.0) geometricNormal=-geometricNormal;
    return dot(authoredNormal,geometricNormal)<0.0;
}
#endif
