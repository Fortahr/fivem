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
	float3 normal   : NORMAL;
	float4 color    : COLOR;
	float2 uv       : TEXCOORD;
};

struct VSOutput
{
	float4 color    : COLOR;
	float2 uv       : TEXCOORD;
	float4 position : SV_POSITION;
};

struct PSInput
{
	float4 color    : COLOR;
	float2 uv       : TEXCOORD;
};

void vs(in VSInput input, out VSOutput output)
{
	output.position = mul(float4(input.position, 1.0f), cameraViewProj);
	output.color = input.color;
	output.uv = input.uv;
}

float4 ps(in PSInput input) : SV_Target
{
	float alpha = 1.0 - abs(input.uv.x);
	input.color.a *= alpha * alpha;
	return input.color;
}

technique10 v {
	pass p {
		SetVertexShader(CompileShader(vs_4_0, vs()));
		SetPixelShader(CompileShader(ps_4_0, ps()));
	}
}
