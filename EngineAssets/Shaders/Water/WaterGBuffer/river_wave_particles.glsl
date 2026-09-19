// 世界空间波包由 WaterSystem 独立推进；按空间桶查询，不扫描整个粒子池。
layout(std430,set=1,binding=7) readonly buffer RiverWaveParticles {vec4 riverWaveData[];};

float RiverWaveBound(vec2 worldXZ)
{
    vec4 p=PcgRiverProperties(worldXZ);
    float distanceFade=1-smoothstep(96,112,distance(worldXZ,params.waterCameraPosition.xz));
    return min(params.riverRendering.y,min(p.w*.8,p.z*.25))*p.x*distanceFade;
}

// geometryFilter.x 为零时输出几何波形；否则只补充被该几何 LOD 过滤的频率。
vec3 RiverWaveHeightGradient(vec2 worldXZ,float footprint,vec2 geometryFilter)
{
    // 像素已经比几何采样更粗时，没有可补回的短波，跳过粒子遍历。
    float coarseFootprint=geometryFilter.x*(geometryFilter.y>0?params.geometryScale.w:1);
    if(geometryFilter.x>0 && footprint>=coarseFootprint)return vec3(0);
    vec4 grid=riverWaveData[0];
    if(grid.w<1)return vec3(0);
    ivec2 cell=ivec2(floor((worldXZ-grid.xy)*grid.z));
    if(any(lessThan(cell,ivec2(0))) || any(greaterThanEqual(cell,ivec2(grid.w))))return vec3(0);
    vec4 bucket=riverWaveData[2+cell.y*int(grid.w)+cell.x];
    vec3 wave=vec3(0);
    for(int i=0;i<int(bucket.y);++i)
    {
        int at=int(bucket.x)+i*4;
        vec4 packet=riverWaveData[at],carrier=riverWaveData[at+1];
        vec2 relative=worldXZ-packet.xy;
        // 紧支撑椭圆包络：缩短波峰横向的相干长度，不改变沿流向的载波波长。
        const float transverseAspect=.45;
        vec2 transverse=vec2(-carrier.y,carrier.x);
        vec2 packetPosition=vec2(dot(relative,carrier.xy),dot(relative,transverse)/transverseAspect);
        float r=length(packetPosition),radius=packet.z;
        if(r>=radius)continue;
        float envelope=.5+.5*cos(3.14159265359*r/radius);
        vec2 radialGradient=r>1e-5?(carrier.xy*packetPosition.x+transverse*packetPosition.y/transverseAspect)/r:vec2(0);
        vec2 envelopeGradient=(-.5*3.14159265359/radius*sin(3.14159265359*r/radius))*radialGradient;
        const float bandStrength[3]=float[3](.32,.20,.12);
        for(int band=0;band<3;++band)
        {
            vec4 component=riverWaveData[at+1+band];
            float wavelength=6.28318530718/component.z;
            float frequencyWeight=smoothstep(2*footprint,4*footprint,wavelength);
            if(geometryFilter.x>0)
            {
                float child=smoothstep(2*geometryFilter.x,4*geometryFilter.x,wavelength);
                float parent=smoothstep(2*geometryFilter.x*params.geometryScale.w,4*geometryFilter.x*params.geometryScale.w,wavelength);
                frequencyWeight=max(frequencyWeight-mix(child,parent,geometryFilter.y),0);
            }
            if(frequencyWeight<=0)continue;
            float phase=dot(relative,component.xy)*component.z+component.w;
            float sine=sin(phase),cosine=cos(phase);
            // 按波数衰减短波高度，保留法线细节而不让细波斜率淹没主体。
            float amplitude=packet.w*bandStrength[band]*(carrier.z/component.z)*frequencyWeight;
            wave.x+=amplitude*envelope*sine;
            wave.yz+=amplitude*(envelopeGradient*sine+envelope*cosine*component.z*component.xy);
        }
    }
    // 平滑限制叠加振幅，岸沿高差和水深共同约束，不让浪峰重新穿过岸边。
    float bound=RiverWaveBound(worldXZ);
    float h=tanh(wave.x);
    float stepSize=uintBitsToFloat(pcgMetadata[0].x)/float(max(pcgMetadata[0].y,1u));
    vec2 boundGradient=vec2(RiverWaveBound(worldXZ+vec2(stepSize,0))-RiverWaveBound(worldXZ-vec2(stepSize,0)),
        RiverWaveBound(worldXZ+vec2(0,stepSize))-RiverWaveBound(worldXZ-vec2(0,stepSize)))/(2*stepSize);
    return vec3(bound*h,bound*(1-h*h)*wave.yz+h*boundGradient);
}
