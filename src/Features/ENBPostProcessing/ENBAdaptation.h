#pragma once

#include "Downsampler.h"
#include "Effect.h"
#include <d3d11.h>
#include <unordered_map>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

class ENBAdaptation : public Effect
{
public:
	virtual std::string GetName() const override { return "enbadaptation.fx"; }

	virtual LPCSTR GetSourceTexture() const override { return "TextureColor"; }

	virtual void Execute() override;
	virtual void UpdateEffectVariables() override;

	// Override Apply to create adaptation-specific textures
	bool Apply() override;

private:
	void CreateAdaptationTextures();
};