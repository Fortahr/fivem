#include <StdInc.h>
#include <Hooking.h>

#include <ScriptEngine.h>

#include <Resource.h>
#include <fxScripting.h>
#include <ICoreGameInit.h>
#include <rageVectors.h>
#include <MinHook.h>
#include <scrEngine.h>
#include <RageParser.h>
#include <CfxRGBA.h>
#include <DrawCommands.h>
#include <GamePrimitives.h>
#include <fiAssetManager.h>

#include <fstream>
#include <filesystem>
#include <sstream>

#define D3D11_CUSTOM_CODE false // direct D3D rendering, i.e.: no rage calls
#define AIM_AT_CURSOR true // false will project it from the weapon not where you are aiming

#if !D3D11_CUSTOM_CODE
static rage::grmShaderFx* g_shader;
static int g_shaderTechnique;
static rage::grcRenderTarget* tempRenderTarget;
#else
static ID3D11Device* g_d3dDevice = nullptr;
static ID3D11DeviceContext* g_d3dContext = nullptr;

static ID3D11VertexShader* g_vs = nullptr;
static ID3D11PixelShader* g_ps = nullptr;
static ID3D11InputLayout* g_inputLayout = nullptr;
static ID3D11BlendState* g_blendState = nullptr;
static ID3D11DepthStencilState* g_depthStencilState = nullptr;

static ID3D11Buffer* g_cameraBuffer = nullptr;
static ID3D11Buffer* g_vertexBuffer = nullptr;
#endif

struct VertexData
{
	DirectX::XMFLOAT3 position;

#if D3D11_CUSTOM_CODE
	uint32_t color;

	static HRESULT CreateInputLayout(ID3D11Device* d3dDevice, std::string_view vertexShaderByteData, ID3D11InputLayout*& inputLayout)
	{
		D3D11_INPUT_ELEMENT_DESC inputMeta[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};

		return d3dDevice->CreateInputLayout(inputMeta, std::size(inputMeta), vertexShaderByteData.data(), vertexShaderByteData.size(), &g_inputLayout);
	}
#endif
};

struct ShapeTestData
{
	char pad_0[2];
	bool hasHit;
	char pad_3[5];
	Vector3* hitPosition;
	// hitNormal ?
};

class CWeaponComponentLaserSightInfo
{
private:
	char pad_0000[72];

public:
	float coronaSize;
	float coronaIntensity;
	uint16_t laserSightBone;
	char pad_0052[2];
	uint32_t laserSightColor;
};
class CWeaponComponentLaserSight
{
public:
	virtual ~CWeaponComponentLaserSight() = default;

public:
	CWeaponComponentLaserSightInfo* pLaserSightInfo; // 0x0008
	class CWeapon* pParentWeapon; // 0x0010
	void* pObject; // 0x0018 CObject
	char pad_0020[16];
	uint32_t boneIndex;
	float offset;
	float targetOffset;
	ShapeTestData* shapeTest;
	Vector3* lastPosition;
	bool hasLastPosition;
	char pad_007D[3];
};

static hook::cdecl_stub<void(void*, Vector3*, float, uint32_t, float, float, Vector3*, float, float, float, uint16_t)> addLightCorona([]()
{
	return hook::get_call(hook::get_pattern("F3 0F 11 44 24 28 F3 0F 11 7C 24 20 E8 ? ? ? ? E8", 12));
});

static hook::cdecl_stub<void(float*, float*, uint32_t)> grcDrawSolidBox([]()
{
	return (void*)0x141313B64;
});

struct LightCoronaData
{
	char m_data[48];
};

struct LightCoronaMgr
{
	LightCoronaData m_coronas[960];
	uint32_t m_numCoronas;

	void Add(Vector3* position, float size, uint32_t color, float intensity, float unk1, Vector3* direction, float unk2, float unk3, float unk4, uint16_t flags)
	{
		addLightCorona(this, position, size, color, intensity, unk1, direction, unk2, unk3, unk4, flags);
	}
};

struct LaserSightRenderRequest
{
	Matrix4x4 m_matrix;
	DirectX::XMVECTOR m_hitPoint;
	uint32_t m_color;
};

class LaserSightRenderer
{
public:
	LaserSightRenderRequest m_requests[64];
	uint32_t m_numRequests;
};

static LaserSightRenderer g_laserSightRenderer;
static LightCoronaMgr* g_lightCoronaMgr;
static bool g_laserSightMode = false;

void EnqueueLaserRender()
{
	uintptr_t a = g_laserSightRenderer.m_numRequests;
	uintptr_t b = 0;
	g_laserSightRenderer.m_numRequests = 0;

	EnqueueGenericDrawCommand([](uintptr_t a, uintptr_t b)
	{
		const auto& viewport = *rage::spdViewport::GetCurrent();

#if D3D11_CUSTOM_CODE
		D3D11_MAPPED_SUBRESOURCE d3dMappedResource;
		g_d3dContext->Map(g_cameraBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &d3dMappedResource);
		memcpy(d3dMappedResource.pData, &viewport, sizeof(viewport));
		g_d3dContext->Unmap(g_cameraBuffer, 0);

		UINT stride = sizeof(VertexData), offset = 0;
		FLOAT prevBlendFactor[4];
		UINT prevBlendMask, prevStencilRef;
		ID3D11DepthStencilState* prevDepthStencilState;
		ID3D11BlendState* prevBlendState;
		ID3D11VertexShader* prevVSShader;
		ID3D11PixelShader* prevPSShader;
		ID3D11Buffer* prevConstantBuffer;

		g_d3dContext->OMGetBlendState(&prevBlendState, prevBlendFactor, &prevBlendMask);
		g_d3dContext->OMGetDepthStencilState(&prevDepthStencilState, &prevStencilRef);
		g_d3dContext->VSGetShader(&prevVSShader, nullptr, 0);
		g_d3dContext->PSGetShader(&prevPSShader, nullptr, 0);
		g_d3dContext->VSGetConstantBuffers(0, 1, &prevConstantBuffer);

		g_d3dContext->IASetInputLayout(g_inputLayout);
		g_d3dContext->IASetVertexBuffers(0, 1, &g_vertexBuffer, &stride, &offset);
		g_d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY::D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		g_d3dContext->VSSetConstantBuffers(0, 1, &g_cameraBuffer);

		g_d3dContext->OMSetBlendState(g_blendState, nullptr, 0xffffffff);
		g_d3dContext->OMSetDepthStencilState(g_depthStencilState, 0);
		g_d3dContext->VSSetShader(g_vs, nullptr, 0);
		g_d3dContext->PSSetShader(g_ps, nullptr, 0);
#else
		auto lastDepth = GetDepthStencilState();
		auto lastBlend = GetBlendState();
		auto lastRasterizer = GetRasterizerState();
		
		SetDepthStencilState(12); // 12
		SetBlendState(4); // 4
		SetRasterizerState(RasterizerStateDefault);

		int index = g_shader->GetParameter("gWorldViewProj"); // cache this
		g_shader->SetParameter(index, &viewport.m_viewProjection, sizeof(viewport.m_viewProjection), 1);

		g_shader->PushTechnique(g_shaderTechnique, true, 0);
		g_shader->PushPass(0);
#endif

		for (size_t i = 0; i < a; i++)
		{
			auto& request = g_laserSightRenderer.m_requests[i];

			Matrix4x4 matrix = request.m_matrix;

			constexpr float laserRadius = 0.01f;
			constexpr uint32_t lsColor = 0x9900FF00;

			const DirectX::XMVECTOR up = DirectX::XMVectorScale(*(DirectX::XMVECTOR*)matrix.m[2], laserRadius);
			const DirectX::XMVECTOR& position = *(DirectX::XMVECTOR*)matrix.m[3];
			
			auto start0 = DirectX::XMVectorSubtract(position, up);
			auto start1 = DirectX::XMVectorAdd(position, up);

			VertexData vertices[4];
			DirectX::XMStoreFloat3(&vertices[0].position, start0);
			DirectX::XMStoreFloat3(&vertices[1].position, start1);

#if !AIM_AT_CURSOR
			const DirectX::XMVECTOR& forward = *(DirectX::XMVECTOR*)matrix.m[0];
			auto distance = DirectX::XMVectorScale(forward, 100.f);

			DirectX::XMStoreFloat3(&vertices[2].position, DirectX::XMVectorAdd(start0, distance));
			DirectX::XMStoreFloat3(&vertices[3].position, DirectX::XMVectorAdd(start1, distance));
#else
			DirectX::XMStoreFloat3(&vertices[2].position, DirectX::XMVectorSubtract(request.m_hitPoint, up));
			DirectX::XMStoreFloat3(&vertices[3].position, DirectX::XMVectorAdd(request.m_hitPoint, up));
#endif

#if D3D11_CUSTOM_CODE
			vertices[0].color = vertices[1].color = vertices[2].color = vertices[3].color = lsColor;

			g_d3dContext->Map(g_vertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &d3dMappedResource);
			memcpy(d3dMappedResource.pData, vertices, sizeof(vertices));
			g_d3dContext->Unmap(g_vertexBuffer, 0);

			g_d3dContext->Draw(4, 0);
#else
			rage::grcBegin(rage::grcDrawMode::grcTriangleStrip, 4);
			rage::grcVertex(vertices[0].position.x, vertices[0].position.y, vertices[0].position.z, 0.f, 0.f, 0.f, lsColor, -1.f, 1.f);
			rage::grcVertex(vertices[1].position.x, vertices[1].position.y, vertices[1].position.z, 0.f, 0.f, 0.f, lsColor, 1.f, 1.f);
			rage::grcVertex(vertices[2].position.x, vertices[2].position.y, vertices[2].position.z, 0.f, 0.f, 0.f, lsColor, -1.f, 1.f);
			rage::grcVertex(vertices[3].position.x, vertices[3].position.y, vertices[3].position.z, 0.f, 0.f, 0.f, lsColor, 1.f, 1.f);
			rage::grcEnd();
#endif
		}

#if D3D11_CUSTOM_CODE
		g_d3dContext->PSSetShader(prevPSShader, nullptr, 0);
		g_d3dContext->VSSetShader(prevVSShader, nullptr, 0);
		g_d3dContext->OMSetBlendState(prevBlendState, prevBlendFactor, prevBlendMask);
		g_d3dContext->OMSetDepthStencilState(prevDepthStencilState, prevStencilRef);
		g_d3dContext->VSSetConstantBuffers(0, 1, &prevConstantBuffer);
#else
		g_shader->PopPass();
		g_shader->PopTechnique();

		SetDepthStencilState(lastDepth);
		SetBlendState(lastBlend);
		SetRasterizerState(lastRasterizer);
#endif
	},
	&a, &b);
}

static void (*g_origLaserSightShapeTest)(CWeaponComponentLaserSight*, void*, Vector3*, Vector3*, char);
static void LaserSightShapeTest(CWeaponComponentLaserSight* self, void* ped, Vector3* startPos, Vector3* endPos, char flags)
{
	// It seems this function is only called when the game is ready to draw laser sight.
	g_origLaserSightShapeTest(self, ped, startPos, endPos, flags);

	auto targetPos = (self->shapeTest && self->shapeTest->hasHit) ? self->shapeTest->hitPosition : self->lastPosition;

	// TODO: get/calculate default (no hit) end point
	// Enqueue laser rendering
	if (targetPos)
	{
		Matrix4x4 laserSightMtx;
		(*(void(__fastcall**)(void*, uint32_t, Matrix4x4*))(*(uint64_t*)self->pObject + 0x48))(self->pObject, self->boneIndex, &laserSightMtx);

		const DirectX::XMVECTOR& forward = *(DirectX::XMVECTOR*)laserSightMtx.m[0];
		const DirectX::XMVECTOR& back = DirectX::XMVectorNegate(forward);
		const DirectX::XMVECTOR& position = *(DirectX::XMVECTOR*)laserSightMtx.m[3];

		float range = *(float*)(*(uint64_t*)(*(uint64_t*)((char*)self + 16) + 64) + 652);
		uint32_t color = self->pLaserSightInfo->laserSightColor;

		if (g_laserSightRenderer.m_numRequests < std::size(g_laserSightRenderer.m_requests))
		{
			uint32_t index = g_laserSightRenderer.m_numRequests++;
			LaserSightRenderRequest& request = g_laserSightRenderer.m_requests[index];

			request.m_color = color;
			request.m_matrix = laserSightMtx;
			request.m_hitPoint = DirectX::XMLoadFloat3(targetPos);

			// requst to draw coronas
			if (auto laserSightInfo = self->pLaserSightInfo)
			{
				float coronaSize = laserSightInfo->coronaSize;
				float coronaIntensity = laserSightInfo->coronaIntensity;

				if (coronaSize > 0.0f && coronaIntensity > 0.0f)
				{
					// start corona
					g_lightCoronaMgr->Add((Vector3*)&position, coronaSize, color, coronaIntensity, 0.02f, (Vector3*)&forward, 1.0f, 0.0f, 90.0f, 3);

					// end corona
					g_lightCoronaMgr->Add(targetPos, coronaSize, color, coronaIntensity, 0.2f, (Vector3*)&back, 1.0f, 90.0f, 90.0f, 3);
				}
			}
		}
	}
}

static InitFunction initFunction([]()
{
	fx::ScriptEngine::RegisterNativeHandler("SET_LASER_SIGHT_DIRECT", [](fx::ScriptContext& context)
	{
		g_laserSightMode = context.GetArgument<bool>(0);
	});

	OnSetUpRenderBuffers.Connect([](int w, int h)
	{
		HRESULT hr = E_FAIL;
#if D3D11_CUSTOM_CODE
		// get D3D device for direct Direct3D (rage-less) rendering
		g_d3dDevice = GetD3D11Device();
		g_d3dDevice->GetImmediateContext(&g_d3dContext);

		{
			D3D11_BUFFER_DESC desc;
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			desc.ByteWidth = sizeof((*g_viewportGame)->viewport);
			desc.StructureByteStride = 0;
			desc.MiscFlags = 0;

			// Create the buffer.
			hr = g_d3dDevice->CreateBuffer(&desc, nullptr, &g_cameraBuffer);
			if (FAILED(hr))
				trace("CreateBuffer(camera) failed with %#010x\n", hr);
		}

		{
			D3D11_BUFFER_DESC desc;
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			desc.ByteWidth = sizeof(VertexData) * 4;
			desc.StructureByteStride = 0;
			desc.MiscFlags = 0;

			// Create the vertex buffer.
			hr = g_d3dDevice->CreateBuffer(&desc, nullptr, &g_vertexBuffer);
			if (FAILED(hr))
				trace("CreateBuffer(vertex) failed with %#010x\n", hr);
		}

		{
			const std::filesystem::path shaderPath = GetAbsoluteCitPath() + L"citizen/shaderz/compiled/laserbeam_vs.cso";
			std::ifstream fileStream(shaderPath, std::ios::in | std::ios::binary);
			std::string fileData = (std::stringstream() << fileStream.rdbuf()).str();

			hr = VertexData::CreateInputLayout(g_d3dDevice, fileData, g_inputLayout);
			if (FAILED(hr))
				trace("CreateInputLayout() failed with %#010x\n", hr);

			hr = g_d3dDevice->CreateVertexShader(fileData.data(), fileData.size(), nullptr, &g_vs);
			if (FAILED(hr))
				trace("CreateVertexShader() failed with %#010x\n", hr);
		}

		{
			const std::filesystem::path shaderPath = GetAbsoluteCitPath() + L"citizen/shaderz/compiled/laserbeam_ps.cso";
			std::ifstream fileStream(shaderPath, std::ios::in | std::ios::binary);
			std::string fileData = (std::stringstream() << fileStream.rdbuf()).str();

			hr = g_d3dDevice->CreatePixelShader(fileData.data(), fileData.size(), nullptr, &g_ps);
			if (FAILED(hr))
				trace("CreatePixelShader() failed with %#010x\n", hr);
		}

		{
			D3D11_BLEND_DESC desc;
			memset(&desc, 0, sizeof(D3D11_BLEND_DESC));

			desc.RenderTarget[0].BlendEnable = TRUE;
			desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
			desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
			desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
			desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
			desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
			desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
			desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

			hr = g_d3dDevice->CreateBlendState(&desc, &g_blendState);
			if (FAILED(hr))
				trace("CreateBlendState() failed with %#010x\n", hr);
		}

		{
			D3D11_DEPTH_STENCIL_DESC desc;
			desc.DepthEnable = true;
			desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
			desc.DepthFunc = D3D11_COMPARISON_GREATER;
			desc.StencilEnable = false;
			desc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
			desc.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
			desc.FrontFace.StencilFunc = desc.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
			desc.FrontFace.StencilDepthFailOp = desc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
			desc.FrontFace.StencilPassOp = desc.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
			desc.FrontFace.StencilFailOp = desc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
			
			hr = g_d3dDevice->CreateDepthStencilState(&desc, &g_depthStencilState);
			if (FAILED(hr))
				trace("CreateDepthStencilState() failed with %#010x\n", hr);
		}
#else
		auto assetManager = rage::fiAssetManager::GetInstance();
		assetManager->PushFolder("citizen:/shaderz/");

		auto shader = rage::grmShaderFactory::GetInstance()->Create();
		if (shader->LoadTechnique("laserbeam", nullptr, false))
		{
			hr = 0;
			g_shader = shader;
			g_shaderTechnique = shader->GetTechnique("v");
		}
		else
			trace("LoadTechnique(laserbeam) failed to load\n");

		assetManager->PopFolder();
#endif
		if (SUCCEEDED(hr))
		{
			OnDrawSceneEnd.Connect(EnqueueLaserRender);
		}
	});
});

static HookFunction hookFunction([]()
{
	static_assert(sizeof(LightCoronaData) == 48);
	static_assert(sizeof(CWeaponComponentLaserSightInfo) == 88);
	static_assert(offsetof(CWeaponComponentLaserSightInfo, laserSightColor) == 0x54);

	{
		g_lightCoronaMgr = hook::get_address<LightCoronaMgr*>(hook::get_pattern("F3 0F 11 44 24 28 F3 0F 11 7C 24 20 E8 ? ? ? ? E8", -4));
	}

	// Add color fields to the parser of CWeaponComponentLaserSightInfo.
	// We don't need to adjust class size as this field takes padding.
	// But not sure if we need to replace parser class functions to add field reset.
	{
		// Register "LaserSightColor" field in parser.
		auto definition = (rage::parMemberDefinition*)hook::AllocateStubMemory(sizeof(rage::parMemberDefinition));
		definition->hash = HashRageString("LaserSightColor");
		definition->offset = 0;
		definition->type = rage::parMemberType::UInt32;
		definition->structType = rage::parStructType::Inline;
		definition->pad2[0] = 0xFF;
		definition->pad2[1] = 0xFF;

		// Find structure and replace data.
		auto structure = hook::get_pattern<rage::parStructureStatic>("D2 D0 6A 4F 00 00 00 00 00 00 00");
		structure->membersData[3] = definition;
		structure->membersOffsets[3] = offsetof(CWeaponComponentLaserSightInfo, laserSightColor);
	}

	MH_Initialize();
	MH_CreateHook(hook::get_pattern("48 81 EC 50 09 00 00 0F 29 70 D8 0F 29", -31), LaserSightShapeTest, (void**)&g_origLaserSightShapeTest);
	MH_EnableHook(MH_ALL_HOOKS);
});
