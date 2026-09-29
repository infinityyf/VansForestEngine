#ifndef VANS_BRDF_HAIR_GLSL
#define VANS_BRDF_HAIR_GLSL
// d'Eon/Chiang 圆柱纤维模型；公式参考 PBRT 4e §9.9。
// 发片没有逐根纤维 h，使用三点 Gauss-Legendre 对横截面积分。
// 返回 BSDF * |cos(theta_i)|，调用方不可再乘表面 NdotL。
const float HAIR_PI = 3.14159265359;
struct HairData
{
    vec3 tangentWS;
    vec3 viewDirWS;
    vec3 albedo;
    float longitudinalRoughness;
    float azimuthalRoughness;
    float cuticleTilt;
    vec3 absorption;
    vec4 variance;
    vec4 sinOutgoing;
    vec4 cosOutgoing;
    float azimuthalScale;
    float sinView;
    float cosView;
};
// Material/view terms are prepared once per surviving fragment, outside every light loop.
void PrepareHairScattering(inout HairData hair)
{
    float bm=hair.longitudinalRoughness, bn=hair.azimuthalRoughness;
    float width=0.726*bm+0.812*bm*bm+3.7*pow(bm,20.0);
    hair.variance=max(width*width,1e-5)*vec4(1.0,0.25,4.0,4.0);
    hair.azimuthalScale=0.626657069*(0.265*bn+1.194*bn*bn+5.372*pow(bn,22.0));
    float fit=5.969-0.215*bn+2.532*bn*bn-10.73*pow(bn,3.0)+5.574*pow(bn,4.0)+0.245*pow(bn,5.0);
    vec3 logColor=log(clamp(hair.albedo,vec3(1e-4),vec3(1.0)))/fit;
    hair.absorption=logColor*logColor;
    hair.sinView=clamp(dot(hair.tangentWS,hair.viewDirWS),-1.0,1.0);
    hair.cosView=sqrt(max(0.0,1.0-hair.sinView*hair.sinView));
    vec4 shifts=hair.cuticleTilt*vec4(-2.0,1.0,4.0,0.0);
    hair.sinOutgoing=hair.sinView*cos(shifts)+hair.cosView*sin(shifts);
    hair.cosOutgoing=abs(hair.cosView*cos(shifts)-hair.sinView*sin(shifts));
}
float HairLogI0(float x)
{
    if (x > 12.0) return x - 0.5*log(2.0*HAIR_PI*x) + log(1.0 + 1.0/(8.0*x));
    float sum=1.0, term=1.0;
    for(int k=1;k<=16;++k) { term *= x*x/(4.0*float(k*k)); sum+=term; }
    return log(sum);
}
float HairM(float si,float ci,float so,float co,float variance)
{
    float a=ci*co/variance, b=si*so/variance, inv=1.0/variance;
    // log(2 v sinh(1/v)) 的稳定形式，同时覆盖小/大粗糙度。
    float logDenom=log(variance)+inv+log(max(1.0-exp(-2.0*inv),1e-8));
    return exp(HairLogI0(a)-b-logDenom);
}
float HairN(float phi,float scale)
{
    float d=mod(phi+HAIR_PI,2.0*HAIR_PI)-HAIR_PI;
    float e=exp(-abs(d)/scale);
    float normalization=(1.0-exp(-HAIR_PI/scale))/(1.0+exp(-HAIR_PI/scale));
    return e/(scale*(1.0+e)*(1.0+e)*normalization);
}
float HairFresnel(float cosine)
{
    const float eta=1.55;
    float ct=sqrt(max(0.0,1.0-(1.0-cosine*cosine)/(eta*eta)));
    float rs=(cosine-eta*ct)/max(cosine+eta*ct,1e-6);
    float rp=(eta*cosine-ct)/max(eta*cosine+ct,1e-6);
    return 0.5*(rs*rs+rp*rp);
}
vec3 EvaluateHairScattering(HairData hair,vec3 L)
{
    float si=clamp(dot(hair.tangentWS,L),-1.0,1.0);
    float so=hair.sinView;
    float ci=sqrt(max(0.0,1.0-si*si)), co=hair.cosView;
    vec3 lp=L-si*hair.tangentWS, vp=hair.viewDirWS-so*hair.tangentWS;
    float phi=atan(dot(hair.tangentWS,cross(vp,lp)),dot(vp,lp)+1e-10);
    float scale=hair.azimuthalScale;
    vec4 m;
    for(int p=0;p<4;++p)
        m[p]=HairM(si,ci,hair.sinOutgoing[p],hair.cosOutgoing[p],hair.variance[p]);
    vec3 sigmaA=hair.absorption;
    float cosT=sqrt(max(1e-6,1.0-so*so/(1.55*1.55)));
    vec3 result=vec3(0.0);
    const float offsets[3]=float[3](-0.7745966692,0.0,0.7745966692);
    const float weights[3]=float[3](0.2777777778,0.4444444444,0.2777777778);
    for(int q=0;q<3;++q)
    {
        float h=offsets[q], go=asin(h), gt=asin(h*co/sqrt(1.55*1.55-so*so));
        float f=HairFresnel(co*sqrt(1.0-h*h));
        vec3 tr=exp(-sigmaA*(2.0*cos(gt)/cosT));
        vec3 a0=vec3(f), a1=(1.0-f)*(1.0-f)*tr, a2=a1*tr*f;
        vec3 residual=a2*tr*f/max(vec3(1e-6),vec3(1.0)-tr*f);
        result+=weights[q]*(m.x*a0*HairN(phi+2.0*go,scale)
            +m.y*a1*HairN(phi-2.0*gt+2.0*go-HAIR_PI,scale)
            +m.z*a2*HairN(phi-4.0*gt+2.0*go-2.0*HAIR_PI,scale)
            +m.w*residual/(2.0*HAIR_PI));
    }
    return result;
}
// 未解析发群的间接光：方向性 R 反射 + 有效发色控制的低频体散射。
// SigmaAFromReflectance 的输入是多次散射后的颜色，不能把由它反推的
// 单纤维 TT 透射率当作发群反照率，否则深棕色会被抬成浅灰色。
// 此处采用有界发群近似：R 占 F，体散射占 (1-F)*basecolor；白色保持单位能量。
vec3 EvaluateHairEnvironment(HairData hair,vec3 position,vec3 normal)
{
    vec3 T=hair.tangentWS;
    float so=hair.sinView, co=hair.cosView;
    vec3 radial=hair.viewDirWS-T*so;
    radial=dot(radial,radial)>1e-10?normalize(radial):normal;
    vec3 ortho=normalize(cross(T,radial));
    vec3 lowFrequency=0.5*(SampleHairLowFrequency(position,normal)+
        SampleHairLowFrequency(position,-normal));
    vec3 result=vec3(0.0);
    const float offsets[3]=float[3](-0.7745966692,0.0,0.7745966692);
    const float weights[3]=float[3](0.2777777778,0.4444444444,0.2777777778);
    for(int q=0;q<3;++q)
    {
        float h=offsets[q], go=asin(h);
        float f=HairFresnel(co*sqrt(1.0-h*h));
        float si=clamp(-hair.sinOutgoing.x,-1.0,1.0);
        float phi=-2.0*go;
        vec3 L=T*si+sqrt(max(0.0,1.0-si*si))*(radial*cos(phi)+ortho*sin(phi));
        float rough=max(hair.longitudinalRoughness,hair.azimuthalRoughness);
        result+=weights[q]*(f*SampleHairEnvironmentRadiance(position,normal,L,rough)
            +(1.0-f)*clamp(hair.albedo,vec3(0.0),vec3(1.0))*lowFrequency);
    }
    return result;
}
#endif
