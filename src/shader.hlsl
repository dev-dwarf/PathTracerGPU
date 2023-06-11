RWTexture2D<float4> outputTexture;

cbuffer Constants
{
    float iTime;
    float2 RenderDim;
};


[numthreads(16, 8, 1)]
void CSMain(uint3 threadId : SV_DispatchThreadID)
{
    float2 uv = (threadId) / RenderDim;
    float3 col = 0.5*(1.0+cos(iTime+uv.xyx+float3(0,2,4)));
    float4 gradientColor = float4(col, 1.0);
    outputTexture[threadId.xy] = gradientColor;

} 
