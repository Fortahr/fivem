cbuffer rage_matrices
{
	row_major float4x4 gWorld; // world/model matrix is stored here?
	row_major float4x4 cameraView;
	row_major float4x4 cameraViewProj;
	row_major float4x4 cameraViewInverse;
}

struct VSInput
{
	float3 position : POSITION;
	float4 color    : COLOR;
};

struct VSOutput
{
	float4 color    : COLOR;
	float2 uv       : TEXCOORD;
	float4 position : SV_POSITION;
};

VSOutput main(VSInput input, uint index : SV_VertexID)
{
	VSOutput output;

	output.position = mul(float4(input.position, 1.0), cameraViewProj);
	output.color = input.color;
	output.uv.x = float(index & 1) * 2.0 - 1.0;
	
	return output;
}
