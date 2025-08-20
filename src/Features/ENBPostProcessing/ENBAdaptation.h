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

	virtual void Execute() override;
	virtual void UpdateEffectVariables() override;

protected:
	// Override virtual texture creation function
	void CreateEffectTextures() override;
};