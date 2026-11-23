struct Material
{
    // albedo, -, -, -, -, -, -, -
    uint32_t hasMapMask;

    uint32_t albedoTexture;
    uint32_t albedoLayer;
};

struct ObjectData
{
    float4x4 modelMatrix;
    Material material;
};

StructuredBuffer<ObjectData> ObjectDataBuffer : register(t0, space0);

cbuffer FrameConstants : register(b0, space1)
{
    float4x4 viewProj;
};

Texture2DArray<float4> TextureBuckets[8] : register(t0, space2);
SamplerState Samplers[8] : register(s0, space2);

struct VSInput
{
    float3 Position : POSITION;
    float4 Colour : COLOR0;
    float2 UV : TEXCOORD0;
    uint InstanceID : SV_InstanceID;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float4 Colour : COLOR0;
    float2 UV : TEXCOORD0;
    nointerpolation Material Material : MATERIAL;
};

float3 LinearToSRGB(float3 linearRGB)
{
    float3 cutoff = step(float3(0.0031308, 0.0031308, 0.0031308), linearRGB);
    float3 higher = 1.055 * pow(max(linearRGB, 0.0), 1.0 / 2.4) - 0.055;
    float3 lower = linearRGB * 12.92;
    return lerp(lower, higher, cutoff);
}

PSInput VSMain(VSInput input)
{
    PSInput output;

    ObjectData objectData = ObjectDataBuffer[input.InstanceID];
    float4x4 modelMatrix = objectData.modelMatrix;

    float4 worldPosition = mul(modelMatrix, float4(input.Position, 1.0f));

    output.Position = mul(viewProj, worldPosition);
    output.Colour = input.Colour;
    output.UV = input.UV;
    output.Material = objectData.material;

    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float4 colour = input.Colour;

    if ((input.Material.hasMapMask & 0x1) != 0)
    {
        float4 albedo = TextureBuckets[input.Material.albedoTexture].Sample(Samplers[input.Material.albedoTexture], float3(input.UV, input.Material.albedoLayer));
        colour *= albedo;
    }

    return float4(LinearToSRGB(colour.rgb), colour.a);
}
