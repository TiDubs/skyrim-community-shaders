#pragma once

#include "Effect.h"
#include "EffectManager.h"

class ENBEffect : public Effect
{
public:
	virtual std::string GetName() const override { return "enbeffect.fx"; }

	virtual LPCSTR GetSourceTexture() const override { return "TextureColor"; }

	virtual void Execute() override;

	void UpdateEffectVariables();
};