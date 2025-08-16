#pragma once

#include "Effect11.h"

class ENBEffect : public Effect11
{
public:
    virtual std::string GetEffectType() const override { return "enbeffect"; }
	virtual LPCSTR GetSourceTexture() const override { return "TextureColor"; }

    virtual void Execute(RE::BSGraphics::RenderTargetData& input, 
                        RE::BSGraphics::RenderTargetData& swap, 
                        RE::BSGraphics::RenderTargetData& output) override;

	void UpdateEffectVariables();
};