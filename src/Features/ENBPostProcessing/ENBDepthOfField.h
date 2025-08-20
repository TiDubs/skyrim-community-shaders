#pragma once

#include "Effect.h"
#include <d3d11.h>
#include <unordered_map>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

class ENBDepthOfField : public Effect
{
public:
	virtual std::string GetName() const override { return "enbdepthoffield.fx"; }

	virtual void Execute() override;
	virtual void UpdateEffectVariables() override;

protected:
	// Override virtual texture creation function
	void CreateEffectTextures() override;
};