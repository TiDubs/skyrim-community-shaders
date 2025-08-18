#pragma once

#include "Effect.h"

class ENBEffectPostPass : public Effect
{
public:
	virtual std::string GetName() const override { return "enbeffectpostpass.fx"; }

	virtual LPCSTR GetSourceTexture() const override { return "TextureColor"; }

	virtual void Execute() override;

	void UpdateEffectVariables();
};