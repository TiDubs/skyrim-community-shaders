#pragma once

#include "Effect.h"

class ENBEffectPrePass : public Effect
{
public:
	virtual std::string GetName() const override { return "enbeffectprepass.fx"; }

	virtual LPCSTR GetSourceTexture() const override { return "TextureColor"; }

	virtual void Execute() override;

	void UpdateEffectVariables();
};