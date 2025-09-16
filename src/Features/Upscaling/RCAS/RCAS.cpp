#include "RCAS.h"

#include "../../../Deferred.h"
#include "../../../Hooks.h"
#include "../../../State.h"
#include "../../../Util.h"
#include "../../Upscaling.h"

struct RCASConfig
{
	float sharpness;
	float3 pad;
};

void RCAS::Initialize()
{
	logger::info("[Upscaling] Creating RCAS resources");

	CreateComputeShader();

	// Create constant buffer for RCAS configuration
	rcasConfigCB = new ConstantBuffer(ConstantBufferDesc<RCASConfig>());
}

void RCAS::CreateComputeShader()
{
	logger::debug("[Upscaling] Compiling RCAS compute shader");

	std::vector<std::pair<const char*, const char*>> defines;

	rcasComputeShader.attach((ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\Upscaling\\RCAS\\RCAS.hlsl", defines, "cs_5_0"));

	logger::debug("[Upscaling] RCAS compute shader compiled successfully");
}

void RCAS::ApplySharpen(ID3D11ShaderResourceView* inputSRV, ID3D11UnorderedAccessView* outputUAV, float sharpness)
{
	auto state = globals::state;
	auto context = globals::d3d::context;

	state->BeginPerfEvent("RCAS Sharpening");

	uint32_t screenWidth = (uint32_t)state->screenSize.x;
	uint32_t screenHeight = (uint32_t)state->screenSize.y;

	// Set up RCAS configuration
	RCASConfig config;
	config.sharpness = sharpness;

	// Update constant buffer
	rcasConfigCB->Update(config);
	auto bufferArray = rcasConfigCB->CB();

	// Set compute shader and resources
	context->CSSetShader(rcasComputeShader.get(), nullptr, 0);
	context->CSSetConstantBuffers(0, 1, &bufferArray);

	// Set sampler
	auto deferred = globals::deferred;
	ID3D11SamplerState* samplers[] = { deferred->linearSampler };
	context->CSSetSamplers(0, 1, samplers);

	// Set SRV
	ID3D11ShaderResourceView* srvs[] = { inputSRV };
	context->CSSetShaderResources(0, 1, srvs);

	// Set UAV
	ID3D11UnorderedAccessView* uavs[] = { outputUAV };
	context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

	// Dispatch compute shader (8x8 thread groups to match RCAS.hlsl)
	uint32_t dispatchX = (screenWidth + 7) / 8;
	uint32_t dispatchY = (screenHeight + 7) / 8;
	context->Dispatch(dispatchX, dispatchY, 1);

	// Cleanup
	ID3D11ShaderResourceView* nullSRVs[] = { nullptr };
	context->CSSetShaderResources(0, 1, nullSRVs);

	ID3D11UnorderedAccessView* nullUAVs[] = { nullptr };
	context->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);

	context->CSSetShader(nullptr, nullptr, 0);

	state->EndPerfEvent();
}