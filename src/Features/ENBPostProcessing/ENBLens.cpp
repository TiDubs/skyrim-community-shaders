#include "ENBLens.h"
#include "EffectManager.h"
#include <imgui.h>

void ENBLens::Execute()
{
	// Get common textures for input/output
	auto& effectManager = EffectManager::GetSingleton();

	auto textureHDRTemp = effectManager.GetCommonTexture("TextureBloomLensTemp");
	auto textureLens = effectManager.GetCommonTexture("TextureLens");

	// Set dowsampled texture, typically the one used (use 1024x1024 mip)
	auto& downsampler = effectManager.GetDownsampler();
	auto& sharedTexture = effectManager.GetSharedDownsampleTexture();

	Texture downsampledInput{};
	downsampledInput.srv = downsampler.GetTexture(sharedTexture);

	ExecuteTechniqueSequence(GetSelectedTechnique(), downsampledInput, *textureLens, *textureHDRTemp);
}

void ENBLens::UpdateEffectVariables()
{
	if (!effect)
		return;

	// Set dowsampled texture, typically the one used (use 1024x1024 mip)
	auto& effectManager = EffectManager::GetSingleton();
	auto& downsampler = effectManager.GetDownsampler();
	auto& sharedTexture = effectManager.GetSharedDownsampleTexture();
	SetShaderResourceVariable("TextureDownsampled", downsampler.GetTexture(sharedTexture));

	// Set original texture, not typically used due to aliasing
	SetShaderResourceVariable("TextureOriginal", globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN].SRV);
}