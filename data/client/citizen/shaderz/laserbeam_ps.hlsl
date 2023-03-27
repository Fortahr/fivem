struct PSInput
{
	float4 color : COLOR;
	float2 uv    : TEXCOORD;
};

float4 main(PSInput input) : SV_TARGET
{
	float alpha = 1.0 - abs(input.uv.x);
	input.color.a *= alpha * alpha;
	return input.color;
}