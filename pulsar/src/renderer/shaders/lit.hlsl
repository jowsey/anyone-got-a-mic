struct ObjectData
{
    float4x4 modelMatrix;
};

StructuredBuffer<ObjectData> ModelMatrices : register(t0, space0);

cbuffer FrameConstants : register(b0, space1)
{
    float4x4 viewProj;
};

struct VSInput
{
    float3 Position : POSITION;
    float4 Colour : COLOR0;
    uint InstanceID : SV_InstanceID;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float4 Colour : COLOR0;
};

PSInput VSMain(VSInput input)
{
    PSInput output;

    ObjectData objectData = ModelMatrices[input.InstanceID];
    float4x4 modelMatrix = objectData.modelMatrix;

    float4 worldPosition = mul(modelMatrix, float4(input.Position, 1.0f));
    output.Position = mul(viewProj, worldPosition);
    output.Colour = input.Colour;

    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return input.Colour;
}
